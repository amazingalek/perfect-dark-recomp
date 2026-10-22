// The game's own frame rate, told to RT64.
//
// The main loop ticks at most once a retrace (g_Vars.mininc60, 1 in every build) and each tick advances the game by
// the time it took, counted in 1/60 s and 1/240 s (frametime_calculate, src/game/timing.c): on a console Perfect Dark
// draws at 15-30 a second, here at 30, every frame two VIs (its controller reads, docs/web.md, Frame rate). RT64
// interpolates to the display's rate from the rate a frame says it was made at (gEXSetRefreshRate), as GoldenEye's
// recompilation does, so each frame's display list starts with it.
//
// main_tick is src/lib/main.c's, as the decompilation builds it for ntsc-final (no DEBUG), with those two commands put
// at the start of the frame's display list, and the survey's tick before it (patches/shader_survey.c).

#include "patches.h"
#include "rt64_extended_gbi.h"
#include "lib/main.h"
#include "lib/profile.h"
#include "lib/sched.h"
#include "lib/vi.h"
#include "lib/joy.h"
#include "lib/mema.h"
#include "lib/lib_34d0.h"
#include "lib/rdp.h"
#include "game/gfxmemory.h"
#include "game/lv.h"
#include "game/playermgr.h"
#include "game/title.h"
#include "game/timing.h"
#include "game/stubs/game_175f50.h"
#include "game/debug.h"
#include "bss.h"
#include "data.h"

extern bool g_MainGameLogicEnabled;
extern u32 g_MainNumGfxTasks;
extern s32 g_MainChangeToStageNum;
extern s32 g_StageNum;

void shader_survey_tick(void);

RECOMP_PATCH void main_tick(void)
{
	Gfx *gdl;
	Gfx *gdlstart;
	OSScMsg msg = {OS_SC_DONE_MSG, NULL};
	s32 i;

	// @recomp: a script's request to start a mission, between frames (patches/shader_survey.c).
	shader_survey_tick();

	if (g_MainChangeToStageNum < 0 && g_MainNumGfxTasks < NUM_GFXTASKS) {
		frametime_calculate();
		profile00009a98();
		profile_reset();
		profile_set_marker(PROFILE_MAINTICK_START);
		func000034d8();
		joy_debug_joy();
		sched_set_crash_enable2(false);

		if (g_MainGameLogicEnabled) {
			gdl = gdlstart = gfx_get_master_display_list();

			// @recomp: the frame's rate, from the retraces it took (at least one).
			gEXEnable(gdl++);
			gEXSetRefreshRate(gdl++, 60 / (g_Vars.diffframe60 > 0 ? g_Vars.diffframe60 : 1));

			gDPSetTile(gdl++, G_IM_FMT_RGBA, G_IM_SIZ_16b, 0, 0x0000, G_TX_LOADTILE, 0, G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMASK, G_TX_NOLOD);
			gDPSetTile(gdl++, G_IM_FMT_RGBA, G_IM_SIZ_4b, 0, 0x0100, 6, 0, G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMASK, G_TX_NOLOD);

			lv_tick();
			playermgr_shuffle();

			if (g_StageNum < STAGE_TITLE) {
				for (i = 0; i < PLAYERCOUNT(); i++) {
					set_current_player_num(playermgr_get_player_at_order(i));

					if (g_StageNum != STAGE_TEST_OLD || !title_is_keeping_mode()) {
						vi_set_view_position(g_Vars.currentplayer->viewleft, g_Vars.currentplayer->viewtop);
						vi_set_fov_aspect_and_size(
								g_Vars.currentplayer->fovy, g_Vars.currentplayer->aspect,
								g_Vars.currentplayer->viewwidth, g_Vars.currentplayer->viewheight);
					}

					lv_tick_player();
				}
			}

			gdl = lv_render(gdl);
			func000034e0(&gdl);

			if (debug_get_profile_mode() >= 2) {
				gdl = profile_render(gdl);
			}

			gDPFullSync(gdl++);
			gSPEndDisplayList(gdl++);
		}

		if (g_MainGameLogicEnabled) {
			gfx_swap_buffers();
			vi_update_mode();
		}

		rdp_create_task(gdlstart, gdl, 0, (s32) &msg);
		g_MainNumGfxTasks++;
		mema_print();
		func0f16cf94();
		profile_set_marker(PROFILE_MAINTICK_END);
	}
}
