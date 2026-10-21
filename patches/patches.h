#ifndef PATCHES_H
#define PATCHES_H

// A function in .recomp_patch replaces the game's function of the same name (patches.toml's strict_patch_mode).
#define RECOMP_PATCH __attribute__((section(".recomp_patch")))

#include <ultra64.h>
#include "constants.h"
#include "types.h"

// Natives (syms.ld, src/pd_natives.cpp).

// The segments syms/relocs.txt lists offsets in.
#define PD_SEG_LIB     0
#define PD_SEG_DATA    1
#define PD_SEG_INFLATE 2
#define PD_SEG_GAME    3

// Shifts the pointer words syms/relocs.txt lists in a segment, once the game has unzipped it into RDRAM, from the
// retail ROM's TLB-mapped addresses to the relink's.
void recomp_pd_relocate(s32 segment);
// Stops the program with a message.
void recomp_pd_crash(const char *message);
// The word of the player's ROM at an offset into it.
void recomp_pd_read_rom(u32 offset, u32 *dst);

#endif
