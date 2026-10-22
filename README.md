# Perfect Dark: Recompiled

> **Made with AI.** Almost all of the code, documentation and tests in this repo were written by AI agents
> (Claude), directed and play-tested by a person. None of it is offered to the upstream projects it builds on, which
> don't take AI-written code.

A port of the N64 version of *Perfect Dark* (USA, revision 1) to PC and to the web, made by static recompilation
([N64Recomp](https://github.com/N64Recomp/N64Recomp) + [N64ModernRuntime](https://github.com/N64Recomp/N64ModernRuntime)
+ [RT64](https://github.com/rt64/rt64)): the game's own MIPS code translated to C and run natively, with RT64 drawing
it. Its symbols come from the [Perfect Dark decompilation](https://github.com/n64decomp/perfect_dark) (MIT), pinned
at `lib/perfect_dark`. It is not an emulator and it contains no game data: you supply your own ROM.

**Status: the desktop build plays a mission.** On Linux, with a desktop GPU (through Mesa's Dozen on WSL2),
*dataDyne Central - Defection* plays from its briefing to its end screen on Agent, with sound. The game's menus work:
the agent file, the Perfect Menu, mission select, the briefing, retry and next mission. A save survives a restart: the
agent file, the completed mission and the next one it unlocks. The game draws 30 frames a second, every frame two VIs,
with its clock at 1.00x real time, in its menus and in Defection's and Investigation's levels alike. RT64 interpolates
to the display's rate (see [docs/building.md](docs/building.md) for the build and its findings).

Still wrong or not yet checked:

- Once, after 22 minutes of play, the GPU device was lost (Dozen's D3D12 device removed, with an NVIDIA driver error
  in the same second). A later 17-minute play never lost it, so the cause, Dozen or the game's drawing, isn't known.
- Why every frame of a level is two VIs on the desktop isn't known. In the menus the game's own code makes it so, and
  in a level the page draws 55-60 where render to RAM's copy doesn't hold it back ([docs/web.md](docs/web.md), Frame
  rate). On a console the game draws at 15-30.
- Speech (MP3): guards' lines start and play to their end in the game's MP3 player, but nobody has listened to them.
- Clouds and water draw through a patch (the game draws them in a way RT64 doesn't), seen in Villa and Chicago but
  not compared with a console's.
- No Windows build has been linked and run, the launcher has only been skipped (`--boot`), and rumble hasn't been
  felt on a pad.

**The web page is built and played locally, not published** ([docs/web.md](docs/web.md)). In Chrome and Firefox it
boots with no WebGPU errors, builds its shaders from a prebuilt list with a section a mission, and plays Defection,
Villa and Chicago, each started by a script, its sound playing.

## The ROM

*Perfect Dark (U) (V1.1)*, the USA release's second revision (`NPDE`, version 01), as a big-endian `.z64` of 32 MiB
with MD5 `e03b088b6ac9e0080440efed07c1e40f` (the decompilation's `ntsc-final`). Other regions and revisions are not
supported.

## License

GPL-3.0 ([LICENSE](LICENSE)). The decompilation at `lib/perfect_dark` is a submodule under its own MIT license.
