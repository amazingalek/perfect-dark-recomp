// Evaluated in the page by N64Recomp-browser's tools/record_shaders.mjs (tools/shader_plan.json) and play_page.mjs
// (tools/play_plan.json): finds patches/shader_survey.c's struct in RDRAM by its magic words and offers
// play(stageindex, ms), which starts that solo mission (its index in mission select's list, 0 Defection) on Agent as
// mission select's Accept does and resolves true once the game has taken the request and the stage is the one playing,
// false after ms; stage(), the stage playing (g_StageNum: 0x30 Defection, 0x5a the title); clock(), the level's clock,
// 240 a second of the game's own time (g_Vars.lvframe240), for a plan's ["clock", "clock()", 240]; and
// setInvincible(), which keeps the player alive for as long as the page runs, as the Invincible cheat does.
(() => {
  if (!window.__survey) {
    for (let a = 0x80000000; a < 0x81800000; a += 4) {
      if (n64r(a) === 0x53484452 && n64r(a + 4) === 0x53525659) { window.__survey = a; break; }
    }
  }
  const s = window.__survey;
  const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));
  window.stage = () => n64r(s + 24) | 0;
  window.clock = () => n64r(s + 28) | 0;
  window.play = async (stageindex, ms) => {
    const before = n64r(s + 8);
    n64w(s + 16, stageindex);
    n64w(s + 12, 1);
    const end = Date.now() + ms;
    while (n64r(s + 8) === before && Date.now() < end) await sleep(50);
    if (n64r(s + 8) === before) return false;
    const target = n64r(s + 32) | 0;
    while (window.stage() !== target && Date.now() < end) await sleep(100);
    return window.stage() === target;
  };
  window.setInvincible = () => { n64w(s + 20, 1); return 'stage 0x' + window.stage().toString(16); };
  return s ? 'survey at ' + s.toString(16) : 'no survey struct: a page built without patches/shader_survey.c';
})()
