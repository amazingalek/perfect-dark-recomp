// The frames RT64 interpolates between two of the game's, across a move of the world's origin.
//
// Perfect Dark draws its world relative to an origin, the position of the room the camera is in: each frame
// player_allocate_matrices (src/game/player.c) sets it (player_set_global_draw_world_offset, from cam_room) before the
// camera's matrices are made from it, and the rooms' (room.c) and the props' are made relative to it too. So on the
// frame the camera crosses into another room, every room's, prop's and the camera's matrix jumps by the distance
// between the two rooms' positions. RT64 interpolates an untagged camera always and snaps an untagged matrix that jumps
// (its teleport check), so the frames it drew between those two drew the whole world shifted by part of that jump, at
// the room seams of every level: through a door, the room beyond gone to the clear colour for a frame.
//
// Each frame's display list starts with the two matrix groups RT64 gives untagged matrices, the model matrices' and the
// camera's, with an id that changes when the origin does: a frame's matrices are paired with the frame before's only
// with the same id, so the frame the origin moves is shown as it is, and every other frame is interpolated as before.
// As GoldenEye's recompilation for the web does (GoldenEye64Recomp-web's patches/origin_interpolation.c).

#include "patches.h"
#include "rt64_extended_gbi.h"
#include "game/room.h"
#include "bss.h"

#define ORIGIN_GROUP_ID 0x50440000 // "PD", and the origin's count in the low half

static Gfx *s_OriginGroups; // where this frame's display list has its groups
static u32 s_OriginCount;

static Gfx *origin_write_groups(Gfx *gdl)
{
	u32 id = ORIGIN_GROUP_ID | (s_OriginCount & 0xffff);

	// As RT64 has an untagged model matrix: decomposed, each component interpolated as its own checks say, paired by
	// its draw calls.
	gEXMatrixGroup(gdl++, id, G_EX_INTERPOLATE_DECOMPOSE, G_EX_NOPUSH, G_MTX_MODELVIEW, G_EX_COMPONENT_AUTO,
			G_EX_COMPONENT_AUTO, G_EX_COMPONENT_AUTO, G_EX_COMPONENT_AUTO, G_EX_COMPONENT_AUTO,
			G_EX_COMPONENT_SKIP, G_EX_COMPONENT_AUTO, G_EX_ORDER_AUTO, G_EX_EDIT_NONE, G_EX_ASPECT_AUTO,
			G_EX_COMPONENT_SKIP, G_EX_COMPONENT_AUTO);
	// As it has an untagged camera: not decomposed, every component interpolated.
	gEXMatrixGroup(gdl++, id, G_EX_INTERPOLATE_SIMPLE, G_EX_NOPUSH, G_MTX_PROJECTION, G_EX_COMPONENT_INTERPOLATE,
			G_EX_COMPONENT_INTERPOLATE, G_EX_COMPONENT_INTERPOLATE, G_EX_COMPONENT_INTERPOLATE,
			G_EX_COMPONENT_INTERPOLATE, G_EX_COMPONENT_SKIP, G_EX_COMPONENT_AUTO, G_EX_ORDER_AUTO,
			G_EX_EDIT_NONE, G_EX_ASPECT_AUTO, G_EX_COMPONENT_SKIP, G_EX_COMPONENT_AUTO);
	return gdl;
}

// Called by main_tick (patches/frame_rate.c) at the start of each frame's display list, after gEXEnable. The origin
// moves later in the frame, as each player's view is drawn, so the groups' id is written again then.
Gfx *origin_tag_frame(Gfx *gdl)
{
	s_OriginGroups = gdl;
	return origin_write_groups(gdl);
}

// src/game/player.c's, as the decompilation builds it for ntsc-final, with the groups' id moved on when the origin
// moves: lastroomforoffset is the room the player's origin was last set from (room_set_last_for_offset), each player's
// own, so split screen moves it only when one of them changes room.
RECOMP_PATCH void player_set_global_draw_world_offset(s32 room)
{
	if (room != g_Vars.currentplayer->lastroomforoffset && s_OriginGroups != NULL) {
		s_OriginCount++;
		origin_write_groups(s_OriginGroups);
	}

	room_get_pos(room, &g_Vars.currentplayer->globaldrawworldoffset);

	g_Vars.currentplayer->globaldrawworldbgoffset.x = g_Vars.currentplayer->globaldrawworldoffset.x;
	g_Vars.currentplayer->globaldrawworldbgoffset.y = g_Vars.currentplayer->globaldrawworldoffset.y;
	g_Vars.currentplayer->globaldrawworldbgoffset.z = g_Vars.currentplayer->globaldrawworldoffset.z;

	room_set_last_for_offset(room);
}
