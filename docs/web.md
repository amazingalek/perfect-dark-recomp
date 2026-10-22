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
They press the keyboard's defaults (`src/mouse.cpp`): W and S walk, A and D strafe, the arrows turn.

## The enhancements

The page has the desktop program's options with the same defaults (README, Modern enhancements): Aspect Ratio Expand
with the game drawing to the window's edges, HUD Placement 16:9, Field of View Original, Mouse Sensitivity 50%. They
cost the page nothing measurable: in headless Chrome at 1256x645 on a desktop GPU, uncapped, the regular plan
presented 341.6 and 345.2 a second against 333.3 and 340.4 for the page without them, interleaved (N64Recomp-browser's
`plays.jsonl`, set `pd-ws1`), the GPU as busy, no shader missed. With Mouse Sensitivity above 0% the page asks for
pointer lock as the game starts, which headless Chrome refuses (`WrongDocumentError`, an exception in the play's
report). The mouse in a player's browser, headed, hasn't been played.

## The shader list

`src/rt64_shaders.txt` holds 648 shaders, which RT64 builds as 270 pipelines: 38 for the boot, then a section a solo
mission (21 sections of 29-125, a shader in the section of every mission that draws it). It was recorded in headless
Chrome at 1256x645 by `tools/record_shaders.sh`, a page load a mission, about 25 minutes in all; its header says what
each section played and what it leaves out. The plans' plays then missed 19 more, added by hand at the end of the
boot's, Defection's and Villa's sections: 10 in Chrome, different ones each run (the boot's as Defection loads, before
its section starts), and 9 that Firefox draws each time Defection starts and Chrome never has. With them the regular plan misses none in either browser. Firefox waits about 3 s for the boot's section
before the first frame and about 3 s for Defection's as it starts; Chrome builds the whole list behind the launcher in
about 8 s.

## Frame rate

The game ticks at most once a retrace with at most two graphics tasks out, and counts each frame's length in VIs (the
decompilation's `src/lib/main.c` and `src/game/timing.c`), so it draws up to 60 a second wherever it can. What it draws
on the page, read from the scheduler (`g_Sched`, `g_MainNumGfxTasks`) a millisecond at a time and from Chrome traces of
every thread, in headless Chrome on a desktop GPU unless a line says otherwise:

- The boot's logos: 60, a tick every VI.
- The title, the file menu and a level: 30, capped or not (the desktop build 30-38). This is the game's own code. PD's
  scheduler sends the main loop a retrace at every RDP done as well as at every VI (`__scHandleTasks`,
  `src/lib/sched.c`), and the main loop ticks at any retrace half a frame or more after the last tick started
  (`main_loop`, `src/lib/main.c`), so a tick can start between VIs. A tick that finds no controller sample newer than
  the last tick's (`joy_debug_joy`, `src/lib/joy.c`) stops and restarts the controller polling to read one, and the
  scheduler answers each of those at its next retrace (`joys_handle_retrace`): the tick takes two VIs, and the poll it
  restarted has its sample only at the VI after. The retraces queued meanwhile start the next tick as soon as this one
  ends, before that VI, so it finds no sample either. Once a tick misses, the ticks after it miss too, for as long as a tick's work
  after its controller read is shorter than a VI, as it is on a fast CPU; on a console the game draws at 15-30.
- That chain, measured on the desktop build in Defection (patches that count ticks and the messages queued at each,
  and a switch flipped in memory, 20 s each way in turn, twice, on an idle host through Dozen): as the game is, 36-38
  frames a second, 411-451 of 710-753 ticks found no new sample, as many as took two VIs, and a tick started with
  1.1-1.3 retraces already queued. With a tick that finds no sample while a poll is out put off to the next retrace,
  58-60, every frame one VI, no tick without a sample or with a retrace queued, the game's clock 0.97-1.00x real time.
  The runtime's controller polling has no part in it: `osContStartReadData` (N64ModernRuntime) posts its message at
  once, so a poll started at one VI has its sample by the next, as on a console.
- Render to RAM's late copy (`src/main.cpp`) is not what holds a level at 30: a page whose display lists go ahead with
  the previous copy still pending draws Defection at 29-31 like this one, capped and uncapped, and a Chrome trace of
  30 s there has RT64's Gfx thread idle 90% and the game's main thread all in `joy_debug_joy`, waiting in `osRecvMesg`
  for the scheduler's retraces (N64Recomp-browser's docs/findings.md, Ruled out, measured). Lists that run slow break
  the chain: with two graphics tasks out a retrace starts no tick and the queue empties, so the next tick starts at a
  VI with its sample. That fits the 55-60 of the first capped plays, without the prebuilt shader list, 18 frames in
  20 VIs at Defection's start. The CPU wrote to no framebuffer with a late copy pending in the shader recording's 21
  levels or in the plays.
- Beside GoldenEye's page from Pages, the regular plans played in turn uncapped, two of each, on an idle host: GoldenEye
  358 and 347 presents a second, held by the uncapped rate (360), and Perfect Dark 340 and 329, with no WebGPU errors
  and no short sound in either. A present is RT64's interpolated frame, so at 30 frames of the game a second in a level
  Perfect Dark's page still presents near the rate its display allows.

`frametime_calculate` (`src/game/timing.c`) spins on `osGetCount` with no call that lets another thread run, but the
main loop's half-frame wait before a tick leaves it little to spin: in the logos 66 of 599 calls spun, 9.7 ms in 10 s
and 6.9 ms at most, and in Defection none. No sound was short in those plays.
