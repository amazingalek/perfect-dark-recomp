// What the patches call (patches/syms.ld) and the libultra functions the runtime leaves to the game.

#include <cstdio>
#include <cstdlib>

#include "recomp.h"
#include "librecomp/game.hpp"
#include "librecomp/addresses.hpp"
#include "ultramodern/error_handling.hpp"

// pd_relocs.h (CMakeLists.txt, from syms/relocs.txt and syms/data_dump.toml): pd_relocs[], each segment's
// relinked start and end, PD_SEG_*.
#include "pd_relocs.h"

namespace {

// A word that points into retail lib, inflate or game, at their TLB-mapped addresses (lib/perfect_dark/ld/pd.ld),
// moves by as much as its segment moved in the relink (docs/building.md, The relocation list).
struct Move {
    uint32_t retail_start;
    uint32_t start;
    uint32_t end;
};

constexpr Move moves[] = {
    { 0x70001050u, PD_LIB_START, PD_LIB_END },
    { 0x70200000u, PD_INFLATE_START, PD_INFLATE_END },
    { 0x7F000000u, PD_GAME_START, PD_GAME_END },
};

constexpr uint32_t segment_starts[] = { PD_LIB_START, PD_DATA_START, PD_INFLATE_START, PD_GAME_START };

} // namespace

// recomp_pd_relocate(segment): once the game has unzipped a segment, its listed words go from retail to relinked.
extern "C" void recomp_pd_relocate(uint8_t* rdram, recomp_context* ctx) {
    uint32_t segment = (uint32_t)ctx->r4;
    if (segment >= sizeof(segment_starts) / sizeof(segment_starts[0])) {
        ultramodern::error_handling::message_box("recomp_pd_relocate: no such segment");
        std::exit(EXIT_FAILURE);
    }
    int32_t base = (int32_t)segment_starts[segment];
    size_t moved = 0;
    size_t unmoved = 0;
    for (const PdReloc& reloc : pd_relocs) {
        if (reloc.segment != segment) {
            continue;
        }
        uint32_t value = (uint32_t)MEM_W(reloc.offset, base);
        bool found = false;
        for (const Move& move : moves) {
            if (value >= move.retail_start && value <= move.retail_start + (move.end - move.start)) {
                MEM_W(reloc.offset, base) = (int32_t)(value - move.retail_start + move.start);
                found = true;
                break;
            }
        }
        if (found) {
            moved++;
        }
        else {
            unmoved++;
            fprintf(stderr, "[pd] relocation %u+0x%06X holds 0x%08X, no retail address\n", segment, reloc.offset, value);
        }
    }
    fprintf(stderr, "[pd] segment %u: %zu pointers moved, %zu not\n", segment, moved, unmoved);
}

extern "C" void recomp_pd_crash(uint8_t* rdram, recomp_context* ctx) {
    char message[256];
    size_t i = 0;
    for (; i < sizeof(message) - 1; i++) {
        message[i] = (char)MEM_B(i, ctx->r4);
        if (message[i] == '\0') {
            break;
        }
    }
    message[i] = '\0';
    fprintf(stderr, "[pd] %s\n", message);
    ultramodern::error_handling::message_box(message);
    std::exit(EXIT_FAILURE);
}

// osPiReadIo(devAddr, data): N64Recomp takes it as one of the runtime's, which has only osEPiReadIo. It reads a word
// of the ROM, and the game's piracy checks compare the words they read (at 0x340, 0x454, 0xa5c, 0xdc0) with the ones
// they were built with, so it reads the player's ROM.
extern "C" void osPiReadIo_recomp(uint8_t* rdram, recomp_context* ctx) {
    uint32_t physical_addr = (uint32_t)ctx->r4 & 0x1FFFFFFF;
    if (physical_addr >= recomp::rom_base) {
        recomp::do_rom_pio(rdram, ctx->r5, physical_addr);
    }
    else {
        MEM_W(0, ctx->r5) = 0;
    }
    ctx->r2 = 0;
}
