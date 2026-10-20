// Registers the recompiled sections with the runtime, and keeps the runtime in
// step with the game's own module loader.
//
// 1080 Snowboarding keeps almost all of its code in relocatable modules
// (.uso files) that the kernel loads at addresses chosen at run time. The
// recompiler saw each module at a fixed placeholder address. Every pointer the
// recompiled code builds into a module section goes through
// section_addresses[section], so pointing those entries at the real load
// addresses (and mapping the module's functions there) is all it takes.
//
// The kernel records every loaded module in a slot table. game_sync_modules()
// reads that table and updates the runtime when a module comes or goes. The
// recompiler inserts a call to it before each kernel function-pointer call
// (see kernel_hooks in tools/gen_recomp.py), which is how the kernel enters
// module code.

#include <cstdint>
#include <cstdio>

#include "game_overlays.h"
#include "recomp_overlays.inl"

#include "librecomp/overlays.hpp"

// Defined in librecomp/src/overlays.cpp but not in its header.
void load_overlay(size_t section_table_index, int32_t ram);

namespace {
struct SectionInfo {
    int index;       // N64Recomp section index, or -1 when the module has none
    uint32_t vram;   // placeholder address the recompiler used
    uint32_t size;
};

enum { TEXT, RODATA, DATA, BSS, SECTION_COUNT };

struct ModuleInfo {
    int file_index;  // also the module's slot in the kernel's table
    const char* name;
    SectionInfo sections[SECTION_COUNT];
};

const ModuleInfo modules[] = {
#include "module_table.inl"
};

// Kernel slot table: one 0x9C-byte slot per file.
constexpr uint32_t slot_table = 0x800130A0;
constexpr uint32_t slot_stride = 0x9C;
// Load address of each section in a slot.
constexpr uint32_t slot_section_offsets[SECTION_COUNT] = {
    0x4C, // text
    0x54, // rodata
    0x50, // data
    0x58, // bss
};

constexpr size_t max_files = 0x87;
uint32_t loaded_addresses[max_files][SECTION_COUNT] = {};
size_t text_table_index[max_files];

uint32_t read_word(uint8_t* rdram, uint32_t vram) {
    return *reinterpret_cast<uint32_t*>(rdram + (vram - 0x80000000u));
}
}

void game::register_overlays() {
    recomp::overlays::overlay_section_table_data_t sections{
        .code_sections = section_table,
        .num_code_sections = ARRLEN(section_table),
        .total_num_sections = num_sections,
    };

    recomp::overlays::overlays_by_index_t overlays{
        .table = overlay_sections_by_index,
        .len = ARRLEN(overlay_sections_by_index),
    };

    recomp::overlays::register_overlays(sections, overlays);
}

extern "C" void game_sync_modules(uint8_t* rdram) {
    static bool initialized = false;
    if (!initialized) {
        // The runtime sorts section_table by ROM address when it starts, so
        // look each module's text section up by its index now.
        for (const ModuleInfo& mod : modules) {
            text_table_index[mod.file_index] = SIZE_MAX;
            for (size_t i = 0; i < ARRLEN(section_table); i++) {
                if (section_table[i].index == (uint32_t)mod.sections[TEXT].index) {
                    text_table_index[mod.file_index] = i;
                }
            }
        }
        initialized = true;
    }

    for (const ModuleInfo& mod : modules) {
        uint32_t slot = slot_table + mod.file_index * slot_stride;
        uint32_t* loaded = loaded_addresses[mod.file_index];

        uint32_t now[SECTION_COUNT];
        bool changed = false;
        for (int s = 0; s < SECTION_COUNT; s++) {
            now[s] = read_word(rdram, slot + slot_section_offsets[s]);
            changed |= now[s] != loaded[s];
        }
        if (!changed) {
            continue;
        }

        const SectionInfo& text = mod.sections[TEXT];
        if (text.index >= 0 && text.size != 0) {
            if (loaded[TEXT] != 0) {
                unload_overlays((int32_t)loaded[TEXT], text.size);
            }
            if (now[TEXT] != 0 && text_table_index[mod.file_index] != SIZE_MAX) {
                load_overlay(text_table_index[mod.file_index], (int32_t)now[TEXT]);
            }
        }

        for (int s = RODATA; s < SECTION_COUNT; s++) {
            const SectionInfo& sec = mod.sections[s];
            if (sec.index >= 0) {
                section_addresses[sec.index] = (int32_t)(now[s] != 0 ? now[s] : sec.vram);
            }
        }

        if (now[TEXT] != 0 || now[DATA] != 0) {
            fprintf(stderr, "[modules] %s loaded: text %08X rodata %08X data %08X bss %08X\n",
                mod.name, now[TEXT], now[RODATA], now[DATA], now[BSS]);
        } else {
            fprintf(stderr, "[modules] %s unloaded\n", mod.name);
        }

        for (int s = 0; s < SECTION_COUNT; s++) {
            loaded[s] = now[s];
        }
    }
}
