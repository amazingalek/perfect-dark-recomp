# Building

The recompilation is made from the [Perfect Dark decompilation](https://github.com/n64decomp/perfect_dark) (MIT,
pinned at `lib/perfect_dark`) built from your own ROM, relinked so that none of its code needs the N64's TLB. This step
makes what the recompilation is made from: `syms/` (symbols and the relocation list, tracked) and
`build/relink/stage1.elf` (the relinked build, which holds the game and is never tracked). Nothing runs yet.

## What you need

- Linux with Docker (WSL2 works), git, and about 2 GB of disk.
- The ROM: *Perfect Dark (U) (V1.1)*, a big-endian `.z64` with MD5 `e03b088b6ac9e0080440efed07c1e40f`.

## Build it

```bash
git clone --recurse-submodules https://github.com/amazingalek/perfect-dark-recomp
cd perfect-dark-recomp
tools/build_decomp.sh path/to/pd.z64        # [cpus], all of them by default
```

The first run builds the Docker image (`tools/docker/Dockerfile`: Ubuntu 24.04, the MIPS binutils, armips,
N64Recomp) in a few minutes; the decomp then builds in about a minute on 12 cores. The script's header says each step:

1. The decomp's own build of `ntsc-final`, its `make test` (every segment against `checksums.ntsc-final.md5`) and the
   built ROM's MD5 against yours. It prints `The decompilation matches the ROM.`
2. The relink: the same objects linked again with `tools/relink.ld.patch` applied to a copy of the decomp's
   `ld/pd.ld`, into `build/relink/`, and once more at other bases into `build/relink-b/`.
3. `syms/relocs.txt` from the two links, then the check that your ROM plus that list is the relink
   (`tools/relocs.py check`). It prints `OK: ...` or what differs.
4. `syms/dump.toml` and `syms/data_dump.toml`, from N64Recomp's `--dump-context` on the relink, and the check that
   they cover the code (`tools/syms.py check`).

`syms/` is tracked: after a run, `git diff syms/` is empty unless the decomp, the patch or the tools changed.

## The relink

Perfect Dark runs its lib segment and its game code behind the TLB: lib at 0x70001050, the game's .text and .rodata at
0x7f000000, paged in 4 KB zips by `src/lib/vm.s`. N64Recomp's memory macros index RDRAM by address - 0x80000000, so
the recompilation needs every address in KSEG0. `tools/relink.ld.patch` links each segment where it already sits in
physical memory on an 8 MB console:

```
segment   retail link   relink       in the ROM
preamble  (0x25e0)      0x80001000   0x1000, raw
lib       0x70001050    0x80001050   0x1050, zipped from 0x3050
.data     0x80059fe0    unchanged    zipped
.bss      0x8008ae20    unchanged    -
inflate   0x70200000    0x80200000   raw
game      0x7f000000    0x80220000   zipped page by page
```

lib was already at physical 0x80001050 (`.data` is linked at `0x80001050 + SIZEOF(.lib)`), the game's own 8 MB path
unzips game to 0x80220000 (`src/lib/vminit.c`), and `src/lib/boot.c` copies inflate to 0x70200000, physical
0x80200000. The preamble's call to `vm_boot` (`vm_boot_from_preamble`) becomes `vm_boot` itself, the same 0x80001050,
and the preamble, which the decomp's script gives no address, is linked where IPL3 runs it: its bytes are the ROM's.
Nothing is pushed to the decomp; the patch is applied to a copy in `build/`.

## The relocation list

At run time the player's ROM supplies every byte: the game's own inflate unpacks lib, `.data` and game into RDRAM,
and the words `syms/relocs.txt` names are shifted from the retail addresses to the relinked ones. A line is
`<segment> <offset in it>` (lib, data, inflate or game; hex), offsets only. A listed word holding an address in retail
lib (0x70001050 to its end) or inflate (0x70200000 on) moves up by 0x10000000, one in retail game (0x7f000000 on) by
0x01220000.

`tools/relocs.py make` finds them by linking twice, at the bases above and at each plus 0x01000000: the words outside
`.text` that differ are the pointers into lib, inflate and game. `tools/relocs.py check` inflates the ROM's segments
as the decomp's `tools/extract` does, applies the list and compares with the relink. Its result on the ROM above:

```
.lib      584 words relocated, 0 differ outside .text
.data    1589 words relocated, 0 differ
.inflate    0 words relocated, 0 differ outside .text
.game    4309 words relocated, 0 differ outside .text
preamble: the ROM's
```

6482 words, and the route holds: the ROM plus the list is the relink byte for byte outside `.text`. Inside `.text`,
672 words of lib, 72 of inflate and 18099 of game differ: instructions that carry an address (`lui`/`addiu` pairs,
`jal`). The recompiler reads code from the relink, and the game reads its own code as data only in the piracy checks
below, which sum the retail bytes they were made from.

## Symbols

`syms/dump.toml` (5686 functions) and `syms/data_dump.toml` (data symbols) are N64Recomp's `--dump-context` of
`build/relink/stage1.elf`, by N64Recomp at upstream commit 81213c1 (the one the image builds). Their
`rom` addresses are offsets in the relink's `stage1.bin` (`objcopy -O binary`), where lib, data and game sit
uncompressed past 32 MiB (`.lib` at 0x2000000, `.game` at 0x20ac170), so a recompilation reads its code from the
relink, built where the ROM is.

N64Recomp takes a symbol as a function only when it has a type and a size, which IDO's C functions have and the
decomp's assembly doesn't, so `tools/syms.py` writes N64Recomp's config with:

- a size for each of the 114 assembly functions, to the next function;
- the code no symbol names: IDO writes no ELF symbol for a static function, and some assembly calls its own local
  labels. 15 of them, found where code reaches them (12 subroutines in `modelasm.s`, one each in `lib_2f490.s`,
  `xprintf.c` and `camdraw.c`), named for their object file and address (`xprintf_8004E690`);
- a function that falls through into the next one sized to take it in, so the recompiled one runs to a return:
  `cosf` into `sinf` (as GoldenEye's), each `mtx4_*_in_place` into its `mtx4_*`, `mtx00015be0`, `func0002f560`, and
  the `modelasm` routines. `__osCleanupThread` ends in a call to `osDestroyThread`, which never returns, and isn't
  stretched.

`tools/syms.py check` then reads the dump back: every word of code that isn't a nop is in a function, and every `jal`
lands on the start of one.

## What still needs the TLB

Outside the VM code (`src/lib/vm.s`, `src/lib/vminit.c`, `src/lib/boot.c`, `src/lib/ultra/os/exceptasm.s`), which the
recompilation replaces, as of the pinned decomp commit. Two scans of the relink back the list: its code loads a
retail address (`lui` 0x7000-0x702f or 0x7f00-0x7f1f) only in `vm_boot`, `vm_handle_miss`, `vm_init` and `boot`, plus
three values that aren't addresses (a length limit in `rzip.s`, two menu colours); and of the 19 words outside code
that hold a value in those ranges (`tools/relocs.py scan`), none is an address (strings, the audio and sine tables, AI
command bytes, menu colours, microcode data).

Changed by the relink:

- `ld/pd.ld`: the lib, inflate and game addresses, and `vm_boot_from_preamble`, now `vm_boot` itself.
- `src/lib/segments.s:32-36`: `seg_inflate` jumps to `inflate1173` at its link address, so it follows inflate to
  0x80200000.

For the recompilation to handle (card 2 on):

- `src/lib/main.c:626,637-638`: `vm_init()`, then the heap runs from the end of `.bss` to `g_VmMarker`, which only
  `vm_init` sets (the start of game, 0x80220000, on 8 MB). Without `vm_init`, `g_VmMarker` has to be set.
- `src/lib/main.c:405-420`: the 4 MB path places the copyright framebuffer below the VM's pages and zip buffer. The
  8 MB path doesn't.
- `src/game/vmstats.c:13-31`: resets `g_VmNumTlbMisses` and the page counters on 4 MB, so they have to exist.
- The game segment is zipped as 4 KB pages, each with a 2-byte checksum (`lib/perfect_dark/tools/mkrom/game.c`), which
  `vm.s` and `vminit.c` unzip: whatever loads the game in their place unzips the same pages.
- lib's first 0x2000 bytes are raw in the ROM and the rest zipped (`tools/mkrom/pack.c`), and `boot.c` unzips it with
  inflate.
- The piracy checks. Twelve sum the code between two functions (`sched.c:404`, `cheats.c:407`, `bot.c:366`,
  `chr.c:2063` and `4871`, `lv.c:470`, `body.c:547`, `game_006900.c:61`, `chraction.c:7632`, `bondgun.c:3766`,
  `explosions.c:207`, `bg.c:2094`) and pass when RDRAM holds the ROM's code at those functions' relinked addresses.
  The others read the ROM at 0xa5c, 0x454 and 0xdc0 (`lv.c:1947`, `objectives.c:108`, `mtx_c.c:309`, `bg.c:1587`,
  `propobj.c:14210`), XOR loads with the ROM's word at 0x340 (`dma.c:94`), or compare `osCicId` with 6105
  (`propobj.c:18836`). None depends on an address; on failure each corrupts code or data.

Harmless or dead in `ntsc-final`: `VM_VIRTUAL_ADDR` (`src/include/lib/vm.h:27`, used only by the VM code); the
addresses in `src/game/debug2.c:436-441` (DEBUG only); `crash.c`'s range checks from `vm_boot` to the end of lib
(DEBUG only, and still right); `osVirtualToPhysical` (KSEG0 addresses never reach its TLB probe); `osMapTLB` (called
only by `vminit.c`) and `osMapTLBRdb` (`initialize.c:55`, the debugger's mapping).
