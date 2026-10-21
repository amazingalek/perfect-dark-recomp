# Perfect Dark: Recompiled

> **Made with AI.** Almost all of the code, documentation and tests in this repo were written by AI agents
> (Claude), directed and play-tested by a person. None of it is offered to the upstream projects it builds on, which
> don't take AI-written code.

A port of the N64 version of *Perfect Dark* (USA, revision 1) to PC and to the web, made by static recompilation
([N64Recomp](https://github.com/N64Recomp/N64Recomp) + [N64ModernRuntime](https://github.com/N64Recomp/N64ModernRuntime)
+ [RT64](https://github.com/rt64/rt64)): the game's own MIPS code translated to C and run natively, with RT64 drawing
it. Its symbols come from the [Perfect Dark decompilation](https://github.com/n64decomp/perfect_dark) (MIT), pinned
at `lib/perfect_dark`. It is not an emulator and it contains no game data: you supply your own ROM.

**Status: nothing runs yet.** The decompilation builds from the ROM and matches it, and a relink of it without the
N64's TLB gives the symbols (`syms/dump.toml`) and relocation list (`syms/relocs.txt`) the recompilation will be made
from, checked against the ROM ([docs/building.md](docs/building.md)). The recompilation and its runtime come next.

## The ROM

*Perfect Dark (U) (V1.1)*, the USA release's second revision (`NPDE`, version 01), as a big-endian `.z64` of 32 MiB
with MD5 `e03b088b6ac9e0080440efed07c1e40f` (the decompilation's `ntsc-final`). Other regions and revisions are not
supported.

## License

GPL-3.0 ([LICENSE](LICENSE)). The decompilation at `lib/perfect_dark` is a submodule under its own MIT license.
