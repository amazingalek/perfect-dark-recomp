// The survey: lets a script start any solo mission by itself and keep the player alive in it, for the page's shader
// list (src/rt64_shaders.txt, recorded by tools/shader_plan.json) and its play plans (tools/play_plan.json), without
// anyone playing the menus. It does nothing unless a script asks.
//
// The script (tools/shader_survey.js) finds this struct in RDRAM by its two magic words. It writes the stage's index in
// the mission list (g_SoloStages, 0 is dataDyne Central - Defection) into `stageindex` and then `request`; `loaded`
// counts the requests taken, so it knows one was, and `target` is the stage the last one started. main_tick
// (patches/frame_rate.c) calls shader_survey_tick once a frame on the game's main thread, so a request is taken between
// frames: the mission is set up on Agent, alone, and started as mission select's Accept does
// (menuhandler_accept_mission), the game's own way into a level. `invincible` set keeps
// every player alive as the Invincible cheat does. Every frame the struct also carries the stage playing, how many
// stages have started (`loads`: the title's intro plays in Defection's stage, so the stage alone doesn't say a request
// started it) and the level's clock (g_Vars.lvframe240, 240 a second of the game's own time), which the plans hold
// their waits on. A request taken during the title's intro is lost: the title goes on to the file menu (stage
// STAGE_CITRAINING), where a script asks.

#include "patches.h"
#include "game/mainmenu.h"
#include "bss.h"
#include "data.h"

extern s32 g_StageNum;

struct ShaderSurvey {
	u32 magic[2];
	volatile u32 loaded;
	volatile s32 request;
	volatile s32 stageindex;
	volatile s32 invincible;
	volatile s32 stagenum;
	volatile s32 lvframe240;
	volatile s32 target;
	volatile u32 loads;
};

struct ShaderSurvey shader_survey = { { 0x53484452, 0x53525659 }, 0, 0, 0, 0, -1, 0, -1, 0 }; // "SHDR" "SRVY"

void shader_survey_tick(void)
{
	s32 i;

	if (shader_survey.request && shader_survey.stageindex >= 0 && shader_survey.stageindex < NUM_SOLOSTAGES) {
		shader_survey.request = 0;
		g_MissionConfig.difficulty = DIFF_A;
		g_MissionConfig.pdmode = false;
		g_MissionConfig.iscoop = false;
		g_MissionConfig.isanti = false;
		g_MissionConfig.stageindex = shader_survey.stageindex;
		g_MissionConfig.stagenum = g_SoloStages[shader_survey.stageindex].stagenum;
		menuhandler_accept_mission(MENUOP_CONFIRM, NULL, NULL);
		shader_survey.target = g_MissionConfig.stagenum;
		shader_survey.loaded++;
	}

	if (shader_survey.invincible && g_StageNum < STAGE_TITLE) {
		for (i = 0; i < MAX_PLAYERS; i++) {
			if (g_Vars.players[i]) {
				g_Vars.players[i]->invincible = 1;
			}
		}
	}

	if (shader_survey.stagenum != g_StageNum) {
		shader_survey.loads++;
	}

	shader_survey.stagenum = g_StageNum;
	shader_survey.lvframe240 = g_Vars.lvframe240;
}
