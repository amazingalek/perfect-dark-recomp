// Field of View: the player's view drawn wider than the game's 60 degrees (vertically), as the PC port's FovY does.
//
// Only what the view is drawn with changes: vi_set_fov_y and vi_set_fov_aspect_and_size keep a wider angle
// (recomp_pd_widen_fov, src/config.cpp) where the game keeps its own, and everything that is drawn and culled with it
// reads it there (the projection in vi.c, cam_set_perspective's for culling, sights and aiming). vi_get_fov_y gives the
// game back the angle it set, so what it works out from it stays the game's: the turn and look speeds, which slow down
// as it zooms in (bondmove.c). A zoomed angle widens by the same factor of its tangent, so a zoom magnifies as much.
// The title, cutscenes and the stages that aren't levels keep the game's angle, as Original does everywhere.

#include "patches.h"
#include "lib/vi.h"
#include "game/camera.h"
#include "bss.h"
#include "data.h"

extern struct rend_vidat *g_ViBackData;
extern s32 g_StageNum;

// The last angles the game set and what was kept for each, so vi_get_fov_y can give the game's back (the data is
// copied from one frame's to the next, so it is matched by value). One a player, at most four.
static f32 s_FovSet[4];
static f32 s_FovKept[4];
static s32 s_FovNext;

static f32 fov_keep(f32 fovy)
{
	f32 kept;
	s32 i;

	if (g_StageNum >= STAGE_TITLE || g_Vars.tickmode == TICKMODE_CUTSCENE) {
		return fovy;
	}

	kept = recomp_pd_widen_fov(fovy);

	if (kept != fovy) {
		for (i = 0; i < 4; i++) {
			if (s_FovKept[i] == kept) {
				s_FovSet[i] = fovy;
				return kept;
			}
		}

		s_FovSet[s_FovNext] = fovy;
		s_FovKept[s_FovNext] = kept;
		s_FovNext = (s_FovNext + 1) % 4;
	}

	return kept;
}

RECOMP_PATCH void vi_set_fov_y(f32 fovy)
{
	// @recomp: the angle drawn.
	g_ViBackData->fovy = fov_keep(fovy);

	cam_set_perspective(g_ViBackData->znear, g_ViBackData->fovy, g_ViBackData->aspect);
	cam_set_scale();
}

RECOMP_PATCH void vi_set_fov_aspect_and_size(f32 fovy, f32 aspect, s16 width, s16 height)
{
	// @recomp: the angle drawn.
	g_ViBackData->fovy = fov_keep(fovy);
	g_ViBackData->aspect = aspect;
	g_ViBackData->viewx = width;
	g_ViBackData->viewy = height;

	cam_set_screen_size(g_ViBackData->viewx, g_ViBackData->viewy);
	cam_set_perspective(g_ViBackData->znear, g_ViBackData->fovy, g_ViBackData->aspect);
	cam_set_scale();
}

RECOMP_PATCH f32 vi_get_fov_y(void)
{
	s32 i;

	// @recomp: the angle the game set.
	for (i = 0; i < 4 && g_StageNum < STAGE_TITLE && g_Vars.tickmode != TICKMODE_CUTSCENE; i++) {
		if (s_FovKept[i] != 0 && s_FovKept[i] == g_ViBackData->fovy) {
			return s_FovSet[i];
		}
	}

	return g_ViBackData->fovy;
}
