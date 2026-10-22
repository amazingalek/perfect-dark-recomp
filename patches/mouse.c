// Mouse look and aim, as the decompilation's PC port plays them (its port/src/input.c and src/game/bondmove.c): the
// game's side, src/mouse.cpp the program's. Only while the mouse plays (recomp_pd_mouse_playing: Mouse Sensitivity
// above 0% and the pointer locked to the game, src/mouse.cpp), and only for player 1; otherwise each function here is
// the decompilation's.
//  - Look: outside aim mode the motion turns Joanna and tilts her view, a count by 0.055 degrees at 50% (the PC port's
//    default) times the field of view over 60, so finer when zoomed. Applied after the game's input (bmove_tick) as a
//    turn of its own, so it stays out of the stick's turn speed and the crosshair's and gun's sway that follows it.
//  - Aim: in aim mode (R, the right button) the motion moves the crosshair (bgun_swivel_without_damp, which only the
//    manual aim calls), and past 0.7 of the way to the screen's edge turns the view, as the PC port's "Classic" aim.
//  - The wheel changes weapon, where and when the game's own buttons would (bmove_process_input).
//  - Look Ahead, which levels the view while she walks, is off: it would fight the mouse (bmove_set_automovecentre_enabled).
// The buttons are src/mouse.cpp's: left is Z (fire), right is R (aim).

#include "patches.h"
#include "game/bondmove.h"
#include "game/bondwalk.h"
#include "game/bondbike.h"
#include "game/bondgrab.h"
#include "game/bondcutscene.h"
#include "game/bondgun.h"
#include "game/footstep.h"
#include "game/gset.h"
#include "lib/snd.h"
#include "lib/vi.h"
#include "bss.h"
#include "data.h"

s32 bwalk_try_delta(struct coord *delta, f32 rotateamount, bool apply, f32 extrawidth, s32 types);
void bmove_process_input(bool allowc1x, bool allowc1y, bool allowc1buttons, bool ignorec2);
void bgun_swivel(f32 screenx, f32 screeny, f32 crossdamp, f32 aimdamp);

// The motion player 1's tick took, in the PC port's units (src/mouse.cpp), until the aim or the look uses it.
static f32 s_MouseDx;
static f32 s_MouseDy;
// Whether the manual aim moved the crosshair this tick, and where the mouse has it, -1 to 1 across the screen (left to
// right, top to bottom, as bgun_swivel takes it).
static bool s_MouseAimed;
static f32 s_SwivelPos[2];

static bool mouse_plays_for_current_player(void)
{
	return g_Vars.currentplayernum == 0 && recomp_pd_mouse_playing();
}

// From the edge's 0.7 to the edge itself, 0 to 1, signed.
static f32 mouse_edge(f32 pos)
{
	f32 e = (pos < 0 ? -pos : pos) - 0.7f;

	if (e <= 0) {
		return 0;
	}

	e /= 0.3f;
	return pos < 0 ? -e : e;
}

RECOMP_PATCH void bmove_set_automovecentre_enabled(bool enabled)
{
	// @recomp: Look Ahead is off while the mouse plays.
	g_Vars.currentplayer->automovecentreenabled = enabled && !recomp_pd_mouse_playing();
}

RECOMP_PATCH void bgun_swivel_without_damp(f32 screenx, f32 screeny)
{
	struct weapondef *weapon;
	f32 aimdamp;

	// @recomp: while the mouse plays, the crosshair goes where it has moved it, once it has moved or as long as it is
	// off the centre.
	if (mouse_plays_for_current_player()
			&& (s_MouseDx != 0 || s_MouseDy != 0 || s_SwivelPos[0] != 0 || s_SwivelPos[1] != 0)) {
		s_SwivelPos[0] += s_MouseDx * 0.7f * (320.0f / 1080.0f) / g_Vars.currentplayer->aspect;
		s_SwivelPos[1] += s_MouseDy * 0.7f * (240.0f / 1080.0f);
		s_SwivelPos[0] = s_SwivelPos[0] > 1 ? 1 : s_SwivelPos[0] < -1 ? -1 : s_SwivelPos[0];
		s_SwivelPos[1] = s_SwivelPos[1] > 1 ? 1 : s_SwivelPos[1] < -1 ? -1 : s_SwivelPos[1];
		s_MouseDx = 0;
		s_MouseDy = 0;
		s_MouseAimed = true;
		bgun_swivel_with_damp(s_SwivelPos[0], s_SwivelPos[1], 0.01f);
		return;
	}

	weapon = gset_get_weapondef(bgun_get_weapon_num(HAND_RIGHT));
	aimdamp = PAL ? weapon->aimsettings->aimdamppal : weapon->aimsettings->aimdamp;

	bgun_swivel(screenx, screeny, PAL ? 0.935f : 0.945f, aimdamp);
}

// Turns the view by degrees, as bwalk_update_theta turns it by the stick's speed, and tilts it.
static void mouse_turn(f32 theta, f32 verta)
{
	struct coord delta = {0, 0, 0};

	if (theta != 0) {
		bwalk_try_delta(&delta, theta * 0.0174505133f, true, 0, CDTYPE_ALL);
	}

	if (verta != 0) {
		g_Vars.currentplayer->vv_verta += verta;
		g_Vars.currentplayer->docentreupdown = false;
		bmove_update_look();
	}
}

static void mouse_tick(bool allowc1x, bool allowc1y, bool allowc1buttons)
{
	struct player *player = g_Vars.currentplayer;
	f32 fovscale = vi_get_fov_y() / 60.0f;
	s32 wheel = 0;

	if (!s_MouseAimed) {
		s_SwivelPos[0] = 0;
		s_SwivelPos[1] = 0;
	}

	if (player->bondmovemode != MOVEMODE_WALK || player->isdead || player->pausemode != PAUSEMODE_UNPAUSED
			|| !(allowc1x && allowc1y)) {
		return;
	}

	if (s_MouseAimed) {
		// Past 0.7 of the way out, the crosshair turns the view, up to the game's own aim-turn speed.
		f32 ex = mouse_edge(s_SwivelPos[0]) * 0.7f * fovscale * g_Vars.lvupdate60freal * 3.5f;
		f32 ey = mouse_edge(s_SwivelPos[1]) * 0.7f * fovscale * g_Vars.lvupdate60freal * 3.5f;
		mouse_turn(ex, ey);
	} else {
		mouse_turn(s_MouseDx * fovscale * 3.5f, s_MouseDy * fovscale * 3.5f);
	}

	if (allowc1buttons && g_Vars.tickmode != TICKMODE_CUTSCENE && g_Vars.lvframenum >= 10) {
		wheel = recomp_pd_mouse_take_wheel();

		while (wheel > 0) {
			bgun_cycle_back();
			wheel--;
		}

		while (wheel < 0) {
			bgun_cycle_forward();
			wheel++;
		}
	}
}

RECOMP_PATCH void bmove_tick(bool allowc1x, bool allowc1y, bool allowc1buttons, bool ignorec2)
{
	struct chrdata *chr;
	u8 foot;
	s32 sound;
	f32 xdiff;
	f32 ydiff;
	f32 zdiff;
	f32 distance;
	bool mouse = mouse_plays_for_current_player();

	// @recomp: the mouse's motion since player 1's last tick, for the aim (bgun_swivel_without_damp) and the look.
	s_MouseDx = 0;
	s_MouseDy = 0;
	s_MouseAimed = false;

	if (mouse) {
		recomp_pd_mouse_take(&s_MouseDx, &s_MouseDy);
	}

	bmove_process_input(allowc1x, allowc1y, allowc1buttons, ignorec2);

	if (mouse) {
		mouse_tick(allowc1x, allowc1y, allowc1buttons);
	} else if (g_Vars.currentplayernum == 0) {
		s_SwivelPos[0] = 0;
		s_SwivelPos[1] = 0;
	}

	if (g_Vars.currentplayer->bondmovemode == MOVEMODE_BIKE) {
		bbike_tick();
	} else if (g_Vars.currentplayer->bondmovemode == MOVEMODE_GRAB) {
		bgrab_tick();
	} else if (g_Vars.currentplayer->bondmovemode == MOVEMODE_CUTSCENE) {
		bcutscene_tick();
	} else if (g_Vars.currentplayer->bondmovemode == MOVEMODE_WALK) {
		bwalk_tick();
	}

	// Update footstep sounds
	if ((g_Vars.currentplayer->bondmovemode == MOVEMODE_WALK || g_Vars.currentplayer->bondmovemode == MOVEMODE_GRAB)
			&& (g_Vars.currentplayer->speedforwards || g_Vars.currentplayer->speedsideways)
			&& (!g_Vars.normmplayerisrunning || PLAYERCOUNT() == 1)) {
		chr = g_Vars.currentplayer->prop->chr;

		if (g_Vars.currentplayer->cameramode == CAMERAMODE_DEFAULT
				&& g_Vars.currentplayer->bdeltapos.y >= -6.0f) {
			xdiff = g_Vars.currentplayer->bondprevpos.x - g_Vars.currentplayer->prop->pos.x;
			ydiff = g_Vars.currentplayer->bondprevpos.y - g_Vars.currentplayer->prop->pos.y;
			zdiff = g_Vars.currentplayer->bondprevpos.z - g_Vars.currentplayer->prop->pos.z;

			foot = 0;
			distance = sqrtf(xdiff * xdiff + ydiff * ydiff + zdiff * zdiff);

			g_Vars.currentplayer->footstepdist += distance;

			if (g_Vars.currentplayer->footstepdist >= 150.0f) {
				foot = 1;
				g_Vars.currentplayer->footstepdist = 0;
			}

			if (foot) {
				if (g_Vars.currentplayer->foot) {
					chr->footstep = 1;
				} else {
					chr->footstep = 2;
				}

				g_Vars.currentplayer->foot = 1 - g_Vars.currentplayer->foot;

				chr->floortype = g_Vars.currentplayer->floortype;

				sound = footstep_choose_sound(chr, distance > 10);

				if (sound != -1) {
					snd_start_extra(NULL, false, AL_VOL_FULL, AL_PAN_CENTER, sound, 1, 1, -1, true);
				}
			}
		}
	}
}
