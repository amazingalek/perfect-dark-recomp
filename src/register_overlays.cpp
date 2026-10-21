// The recompiled code's sections, and pd::on_init, which loads them before the game starts; pd::on_init and
// pd::on_thread_create also set the CPU state the game's code needs (FR, below).
//
// The relink links all of the game's code where it runs (lib at 0x80001050, inflate at 0x80200000, game at
// 0x80220000), but the runtime learns where code is only from DMAs it sees: at start it loads the sections in the
// first MB of ROM (the preamble), and the game reaches lib, inflate and game by unzipping, not by a DMA of their code.
// Their rom addresses in syms/dump.toml are offsets in the relink's image, past the ROM's 32 MB for lib and game, so no
// DMA of the real ROM matches them. So every section is loaded at its own address here, before the preamble jumps to
// vm_boot. The runtime's start also takes inflate, which the image keeps in the first MB as the ROM does, for code at
// its ROM offset in RDRAM; loading it again at 0x80200000 makes calls there find it.

#include "pd.h"
#include "../RecompiledFuncs/recomp_overlays.inl"

#include "librecomp/overlays.hpp"

void pd::register_overlays() {
    recomp::overlays::overlay_section_table_data_t sections {
        .code_sections = section_table,
        .num_code_sections = ARRLEN(section_table),
        .total_num_sections = num_sections,
    };

    recomp::overlays::overlays_by_index_t overlays {
        .table = overlay_sections_by_index,
        .len = ARRLEN(overlay_sections_by_index),
    };

    recomp::overlays::register_overlays(sections, overlays);
}

// The status register's FR bit: 32 floating-point registers of 64 bits each, not 16 pairs of 32. IPL3 sets it, and so
// does the game's osCreateThread for every thread (src/lib/ultra/os/createthread.c), and its code depends on it: it keeps
// doubles in odd registers and singles in both of a pair (src/game/mtxf2lbulkasm.s), which with the runtime's default
// of FR off overlap, and every matrix the game builds comes out wrong.
static void set_fr(recomp_context* ctx) {
    cop0_status_write(ctx, cop0_status_read(ctx) | 0x04000000);
}

void pd::on_init(uint8_t* rdram, recomp_context* ctx) {
    for (const SectionTableEntry& section : section_table) {
        load_overlays(section.rom_addr, section.ram_addr, section.size);
    }

    // IPL3 writes the CIC's number here; propobj.c's piracy check compares it with 6105.
    constexpr int32_t osCicId = 0x80000310;
    MEM_W(osCicId, 0) = 6105;

    set_fr(ctx);
}

void pd::on_thread_create(uint8_t* rdram, recomp_context* ctx) {
    (void)rdram;
    set_fr(ctx);
}
