# The web page

The game plays in a browser: built with Emscripten on N64Recomp-browser's `frontend/` into
`PerfectDarkRecompiled.html` (the launcher, the recomp's menus, SDL2's audio and keyboard, the ROM from Load ROM, and
the saves kept in IndexedDB; `lib/N64Recomp-browser/frontend/CMakeLists.txt`'s header says what each part does), drawn
through WebGPU. The page is built from your ROM like the desktop program, so it is never built in CI and never
published with the game in it.

What is the game's own stays here: the `EMSCRIPTEN` branches of `CMakeLists.txt` that make the game a page,
`tools/build_web.sh`, the prebuilt shader list (`src/rt64_shaders.txt`, recorded by `tools/shader_plan.json`; the
list's header says how to record it again), the play plans (`tools/play_plan.json`, `tools/play_plan_extended.json`)
and the survey that drives both (`patches/shader_survey.c`, `tools/shader_survey.js`). The rest is
N64Recomp-browser's: `docs/porting.md` (WebGPU and the shaders, prebuilt shader lists, Emscripten), `docs/playing.md`
(playing and measuring a page), `docs/forks.md` and `docs/findings.md`. Every play of this game's page is a line in
N64Recomp-browser's `plays.jsonl` (game `pd`), never a record here.

## Build it

After `tools/build_decomp.sh` ([building.md](building.md)), on Linux with Docker:

```bash
tools/build_web.sh                  # [cpus] [cmake arguments]
```

It builds `build/web/PerfectDarkRecompiled.html` with its `.js`, `.wasm` (about 16 MB) and `.data` (about 1.4 MB: the
recomp's fonts and menus, nothing of the game) in `tools/docker/Dockerfile.web`, the image every N64Recomp-browser page
is built in, with ccache in `build/ccache-web`, so a rebuild after a patch takes under a minute. The script's header
says each step. Two things differ from the desktop build:

- The patches library (`patches/`, built for MIPS) is recompiled by the native N64Recomp that `tools/recompile.sh`
  builds, not the runtime's, which Emscripten would build for the browser. Its C, and the recompiled code's, is
  compiled with implicit declarations an error: a call to an undeclared function links to a trap in WebAssembly.
- RT64 builds its pipelines from `src/rt64_shaders.txt` (`RT64_PREBUILT_SHADER_LIST`), in sections: the boot's before
  the game starts, and a level's the first time the level draws.

Serve `build/web` over HTTP from localhost (any static server; the page brings its own `coi_serviceworker.js` for the
headers threads need) and open `PerfectDarkRecompiled.html` in Chrome or Firefox with WebGPU, then Load ROM.

## The survey

A play plan or a shader recording needs the game in each level without playing its menus by hand.
`patches/shader_survey.c` is a struct in RDRAM with two magic words, which `tools/shader_survey.js` finds from the page;
the script asks for a solo mission by writing its index, and the game starts it between frames as mission select's
Accept does. It also says which stage plays, whether the player is in control, and the level's clock, which the plans
hold their waits on (`["clock", "clock()", 240]`), and it keeps the player invincible. Its header says each field. It
does nothing unless a script asks.

## The plans

`tools/play_plan.json` (about 2 minutes) boots, waits for the prebuilt shaders, shoots the intro and the file menu,
starts *dataDyne Central - Defection* on Agent, skips its opening cutscene and plays a minute of the game's time in it:
walking, strafing, turning, firing and aiming. `tools/play_plan_extended.json` (about 4 minutes) plays Defection,
then *Villa* and *Chicago*. Play them with N64Recomp-browser's `tools/play_page.mjs` (its `docs/playing.md`).

## The shader list

`src/rt64_shaders.txt` holds 647 shaders, which RT64 builds as 270 pipelines: 37 for the boot, then a section a solo
mission (21 sections of 29-125, a shader in the section of every mission that draws it). It was recorded in headless
Chrome at 1256x645 by `tools/record_shaders.sh`, a page load a mission, about 25 minutes in all; its header says what
each section played and what it leaves out. The plans' plays then missed 18 more, added by hand to Defection's and
Villa's sections: 9 in Chrome, different ones each run, and 9 that Firefox draws each time Defection starts and Chrome
never has. With them the regular plan misses none in either browser. Firefox waits about 3 s for the boot's section
before the first frame and about 3 s for Defection's as it starts; Chrome builds the whole list behind the launcher in
about 8 s.

## Frame rate

The game ticks at most once a retrace with at most two graphics tasks out, and counts each frame's length in VIs (the
decompilation's `src/lib/main.c` and `src/game/timing.c`), so it draws up to 60 a second wherever it can. What it draws
on the page, read from the scheduler (`g_Sched`, `g_MainNumGfxTasks`) a millisecond at a time and from Chrome traces of
every thread, in headless Chrome on a desktop GPU:

- The boot's logos: 60, a tick every VI.
- The title and the file menu: 30, capped or not. This is the game's own code. PD's scheduler sends the main loop a
  retrace at every RDP done as well as at every VI (`__scHandleTasks`, `src/lib/sched.c`), so a tick can start between
  VIs. A tick that finds no controller sample newer than the last tick's (`joy_debug_joy`, `src/lib/joy.c`) stops and
  restarts the controller polling to read one, and the scheduler answers each of those at its next retrace: the tick
  takes two VIs, and the retraces queued meanwhile start the next tick at once, again before the next sample.
- A level: capped at 60 presents a second, 18 frames in 20 VIs at Defection's start, each graphics task done 1-2 ms
  after it was sent. Uncapped, or capped where the display's rate is high (headless Chrome's animation frames come
  about 174 a second), 30: render to RAM's late copy (`src/main.cpp`) lands when the next display list starts, Chrome
  queues the copy's readback behind every present, and the list waits for it (85% of the Gfx thread's samples were in
  `landRAMCopies`), so both graphics tasks are out at every VI. That cost is the shared support's, not the game's, and
  is N64Recomp-browser's to fix. The CPU wrote to no framebuffer with a late copy pending in the shader recording's
  21 levels or in the plays.

`frametime_calculate` (`src/game/timing.c`) spins on `osGetCount` with no call that lets another thread run, but the
main loop's half-frame wait before a tick leaves it little to spin: in the logos 66 of 599 calls spun, 9.7 ms in 10 s
and 6.9 ms at most, and in Defection none. No sound was short in those plays.
