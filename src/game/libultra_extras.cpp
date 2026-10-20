// libultra functions the game calls that N64ModernRuntime doesn't provide.
//
// N64Recomp treats these as runtime-provided, but the runtime only implements
// the higher-level functions that normally call them. The game still reaches a
// few directly (often through its own wrappers), so they are filled in here,
// either with what the PC equivalent is or as harmless no-ops.

#include <cstdio>

#include "ultramodern/ultra64.h"
#include "ultramodern/ultramodern.hpp"
#include "librecomp/game.hpp"
#include "librecomp/helpers.hpp"

namespace {
void note_once(const char* name) {
    // Report each stub the first time it runs, so unexpected paths show up in logs.
    static thread_local const char* last = nullptr;
    if (last != name) {
        fprintf(stderr, "[libultra] %s called\n", name);
        last = name;
    }
}
}

#define NOOP_RETURN_ZERO(name)                                           \
    extern "C" void name##_recomp(uint8_t* rdram, recomp_context* ctx) { \
        (void)rdram;                                                     \
        note_once(#name);                                                \
        _return<s32>(ctx, 0);                                            \
    }

// Hardware register access and kernel setup, all handled by the runtime.
NOOP_RETURN_ZERO(__osSiRawWriteIo)
NOOP_RETURN_ZERO(__osSpRawWriteIo)
NOOP_RETURN_ZERO(osMapTLBRdb)
NOOP_RETURN_ZERO(__osSetSR)
NOOP_RETURN_ZERO(__osPiCreateAccessQueue)
NOOP_RETURN_ZERO(__osViInit)
NOOP_RETURN_ZERO(__osTimerServicesInit)
NOOP_RETURN_ZERO(osPiWriteIo)
NOOP_RETURN_ZERO(osViGetCurrentField)

// Thread queue internals. The runtime schedules threads itself, so nothing
// should get here; the log line says so if something does.
NOOP_RETURN_ZERO(__osEnqueueThread)
NOOP_RETURN_ZERO(__osPopThread)

extern "C" void __osSiRawReadIo_recomp(uint8_t* rdram, recomp_context* ctx) {
    note_once("__osSiRawReadIo");
    u32* data = _arg<1, u32*>(rdram, ctx);
    *data = 0;
    _return<s32>(ctx, 0);
}

extern "C" void __osSpRawReadIo_recomp(uint8_t* rdram, recomp_context* ctx) {
    note_once("__osSpRawReadIo");
    u32* data = _arg<1, u32*>(rdram, ctx);
    *data = 0x1; // SP_STATUS_HALT: the RSP is idle between tasks
    _return<s32>(ctx, 0);
}

// Reads a word from the cartridge.
static void read_cart_word(uint8_t* rdram, recomp_context* ctx) {
    u32 dev_addr = _arg<0, u32>(rdram, ctx);
    gpr data_ptr = ctx->r5;
    recomp::do_rom_read(rdram, data_ptr, 0x10000000 | (dev_addr & 0x0FFFFFFF), 4);
    _return<s32>(ctx, 0);
}

extern "C" void osPiReadIo_recomp(uint8_t* rdram, recomp_context* ctx) {
    read_cart_word(rdram, ctx);
}

extern "C" void osPiRawReadIo_recomp(uint8_t* rdram, recomp_context* ctx) {
    read_cart_word(rdram, ctx);
}

extern "C" void osViGetCurrentLine_recomp(uint8_t* rdram, recomp_context* ctx) {
    (void)rdram;
    _return<u32>(ctx, 0);
}

// No Controller Pak support in the runtime yet, so report none plugged in.
extern "C" void osPfsIsPlug_recomp(uint8_t* rdram, recomp_context* ctx) {
    u8* pattern = _arg<1, u8*>(rdram, ctx);
    *pattern = 0;
    _return<s32>(ctx, 0);
}

// CRC of a Controller Pak address (libultra's __osContAddressCrc).
extern "C" void __osContAddressCrc_recomp(uint8_t* rdram, recomp_context* ctx) {
    u32 addr = _arg<0, u16>(rdram, ctx);
    u32 temp = 0;
    for (u32 bit = 0x400; bit != 0; bit >>= 1) {
        temp <<= 1;
        if (addr & bit) {
            if (temp & 0x20) {
                temp ^= 0x14;
            } else {
                temp++;
            }
        } else if (temp & 0x20) {
            temp ^= 0x15;
        }
    }
    for (int i = 0; i < 5; i++) {
        temp <<= 1;
        if (temp & 0x20) {
            temp ^= 0x15;
        }
    }
    _return<u8>(ctx, (u8)(temp & 0x1F));
}

// osPiRawStartDma(direction, devAddr, vAddr, nbytes) as the game's own copies
// use it: reads complete immediately, so status polls see an idle PI.
extern "C" void game_pi_raw_dma(uint8_t* rdram, recomp_context* ctx) {
    s32 direction = _arg<0, s32>(rdram, ctx);
    u32 dev_addr = _arg<1, u32>(rdram, ctx);
    gpr vaddr = ctx->r6;
    u32 nbytes = _arg<3, u32>(rdram, ctx);
    if (direction == 0) { // OS_READ
        recomp::do_rom_read(rdram, vaddr, 0x10000000 | (dev_addr & 0x0FFFFFFF), nbytes);
    } else {
        note_once("osPiRawStartDma write");
    }
    _return<s32>(ctx, 0);
}

// osYieldThread: deliver any pending interrupt messages and let a thread they
// woke run, waiting up to 1 ms for one so busy-wait loops don't spin a core.
extern "C" void yield_self_1ms(uint8_t* rdram);
extern "C" void game_yield_thread(uint8_t* rdram) {
    yield_self_1ms(rdram);
}
