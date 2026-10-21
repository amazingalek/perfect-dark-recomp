// The libultra functions the game calls that N64Recomp leaves to the runtime (its reimplemented and ignored lists) and
// this runtime has none of: the controller pak's internals, which the game's own pak, Game Boy pak and rumble code
// (src/lib/pfs*.c, gbpak*.c, motor.c) calls under the osPfs* level the runtime emulates; two of IDO's 64-bit
// conversions; and the TLB and RDP counters. Every pak answers that none is plugged in, as the runtime's osPfs* do.

#include <cstdint>

#include "recomp.h"

namespace {

constexpr int32_t PFS_ERR_NOPACK = 1;

void no_pak(recomp_context* ctx) {
    ctx->r2 = PFS_ERR_NOPACK;
}

} // namespace

extern "C" void __osContRamRead_recomp(uint8_t* rdram, recomp_context* ctx) { no_pak(ctx); }
extern "C" void __osContRamWrite_recomp(uint8_t* rdram, recomp_context* ctx) { no_pak(ctx); }
extern "C" void __osPfsSelectBank_recomp(uint8_t* rdram, recomp_context* ctx) { no_pak(ctx); }
extern "C" void __osPfsRWInode_recomp(uint8_t* rdram, recomp_context* ctx) { no_pak(ctx); }
extern "C" void __osPfsGetStatus_recomp(uint8_t* rdram, recomp_context* ctx) { no_pak(ctx); }
extern "C" void __osCheckPackId_recomp(uint8_t* rdram, recomp_context* ctx) { no_pak(ctx); }
extern "C" void __osRepairPackId_recomp(uint8_t* rdram, recomp_context* ctx) { no_pak(ctx); }
extern "C" void __osCheckId_recomp(uint8_t* rdram, recomp_context* ctx) { no_pak(ctx); }

// osPfsIsPlug(mq, *pattern): no controller has a pak.
extern "C" void osPfsIsPlug_recomp(uint8_t* rdram, recomp_context* ctx) {
    MEM_B(0, ctx->r5) = 0;
    ctx->r2 = 0;
}

extern "C" void __osSiGetAccess_recomp(uint8_t* rdram, recomp_context* ctx) {}
extern "C" void __osSiRelAccess_recomp(uint8_t* rdram, recomp_context* ctx) {}

// __osContAddressCrc(address): libultra's 5-bit CRC of a pak address.
extern "C" void __osContAddressCrc_recomp(uint8_t* rdram, recomp_context* ctx) {
    uint32_t addr = (uint16_t)ctx->r4;
    uint32_t ret = 0;
    for (uint32_t bit = 0x400; bit != 0; bit >>= 1) {
        ret <<= 1;
        if (addr & bit) {
            if (ret & 0x20) {
                ret ^= 0x14;
            }
            else {
                ret++;
            }
        }
        else if (ret & 0x20) {
            ret ^= 0x15;
        }
    }
    for (int i = 0; i < 5; i++) {
        ret <<= 1;
        if (ret & 0x20) {
            ret ^= 0x15;
        }
    }
    ctx->r2 = ret & 0x1F;
}

// __ll_to_d(s64) and __f_to_ull(f32), as librecomp's math_routines.cpp does the others.
extern "C" void __ll_to_d_recomp(uint8_t* rdram, recomp_context* ctx) {
    int64_t a = (int64_t)(((uint64_t)ctx->r4 << 32) | (uint32_t)ctx->r5);
    ctx->f0.d = (double)a;
}

extern "C" void __f_to_ull_recomp(uint8_t* rdram, recomp_context* ctx) {
    uint64_t ret = (uint64_t)ctx->f12.fl;
    ctx->r2 = (int32_t)(ret >> 32);
    ctx->r3 = (int32_t)(ret >> 0);
}

// No TLB: the relink has none.
extern "C" void osMapTLB_recomp(uint8_t* rdram, recomp_context* ctx) {}
// osDpGetCounters(u32 array[4]): every counter 0.
extern "C" void osDpGetCounters_recomp(uint8_t* rdram, recomp_context* ctx) {
    for (int i = 0; i < 4; i++) {
        MEM_W(i * 4, ctx->r4) = 0;
    }
}
