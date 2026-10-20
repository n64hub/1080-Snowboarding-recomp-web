#ifndef GAME_RELOC_H
#define GAME_RELOC_H

// Included by the recompiled code after recomp.h.
//
// When the game's module loader links a module, references to symbols in a
// module that is not loaded are patched to address 0 (the offset is dropped
// too). Code relies on that: genproc builds character-select objects from
// charmap.uso data and skips them when the pointer is 0, which is the case in
// modes that never load charmap. Mirror it, so a pointer into an unloaded
// module is 0 rather than an address that only existed in the recompiler.

#ifdef __cplusplus
extern "C" {
#endif
// 1 for each section whose module is currently loaded (always 1 for sections
// that are not part of a relocatable module). Kept by game_sync_modules().
extern uint8_t game_section_loaded[];
#ifdef __cplusplus
}
#endif

#define GAME_RELOC_ADDR(section_index, offset) \
    (game_section_loaded[section_index] ? (int32_t)(section_addresses[section_index] + (offset)) : 0)

#undef RELOC_HI16
#define RELOC_HI16(section_index, offset) HI16(GAME_RELOC_ADDR(section_index, offset))

#undef RELOC_LO16
#define RELOC_LO16(section_index, offset) LO16(GAME_RELOC_ADDR(section_index, offset))

#endif
