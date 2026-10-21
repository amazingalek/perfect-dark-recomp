// The sky's clouds and the water or ground below the horizon, drawn as triangles RT64 draws.
//
// sky_render (src/game/sky.c) finds where the screen's corners and edges meet the cloud plane and the water plane and
// draws each as a polygon of 3-5 vertices, through sky_render_tri and sky_render_full: raw RDP triangles, which RT64
// doesn't draw (its G_RDPHALF_1 only keeps the word for the next command), so a level with clouds showed none and its
// ground or water was the fill rectangle only on the game's own 4:3. Here the same polygons are drawn as vertices
// (sky_draw_polygon), as the PC port of the decompilation does and GoldenEye's recompilation does for its own sky
// (whose patches/skybox.c this follows):
//  - Camera-relative, through a rotation-only modelview, so no translation goes through the s15.16 matrix.
//  - Pulled towards the eye to inside the far plane (the planes reach 300000 units out), which leaves them where they
//    are on the screen and their perspective-correct texturing alone, and kept twice the near plane away.
//  - Scaled to the s16 range of a Vtx, the scale undone in the matrix.
//  - Sheared up by the clouds' height: the game aims its corner rays env->clouds_height pixels lower and then draws
//    everything that many pixels higher (sky_convert_vertex), here a view-space shear.
//  - Texture coordinates as the PC port gives them (the clouds' as the game makes them, the water's a tenth), moved
//    by a whole number of repeats towards zero to fit s16.
//  - The game makes the polygons for its own view, and RT64 draws wider than that (Expand): sky_render builds them
//    for a view twice as wide and a quarter taller with the same centre and scale, the rest off the screen.
//  - Scissored to the player's view, as the game clamped its 2D vertices to it: the scissor is the whole screen then
//    (vi_render_viewport_edges), and a cutscene's bars would show sky.
// What's drawn with the polygon's own state is set inside pushes of RT64's, so nothing reaches what is drawn after.
//
// sky_render_inner is src/game/sky.c's sky_render as the decompilation builds it for ntsc-final, with its two calls of
// the raw triangles' code and the 2D vertices they took made into sky_draw_polygon, and the ground's fill rectangle
// made into one too.

#include "patches.h"
#include "rt64_extended_gbi.h"
#include "gbiex.h"
#include "game/sky.h"
#include "game/camera.h"
#include "game/env.h"
#include "game/tex.h"
#include "game/gfxmemory.h"
#include "lib/vi.h"
#include "lib/mtx.h"
#include "bss.h"
#include "data.h"

#define CORNERSTATE_NONE     0x0
#define CORNERSTATE_BR       0x1
#define CORNERSTATE_BL       0x2
#define CORNERSTATE_BOTTOM   0x3
#define CORNERSTATE_TR       0x4
#define CORNERSTATE_RIGHT    0x5
#define CORNERSTATE_TR_BL_BR 0x7
#define CORNERSTATE_TL       0x8
#define CORNERSTATE_LEFT     0xa
#define CORNERSTATE_TL_BL_BR 0xb
#define CORNERSTATE_TOP      0xc
#define CORNERSTATE_TL_TR_BR 0xd
#define CORNERSTATE_TL_TR_BL 0xe
#define CORNERSTATE_FULL     0xf

extern Mtxf g_SkyMtx;
extern f32 g_SkyCloudOffset;

void sky_get_world_pos_from_screen_pos(f32 left, f32 top, struct coord *dst);
bool sky_is_screen_corner_in_sky(struct coord *corner3dpos, struct coord *dstpos, f32 *dstfrac);
bool sky_is_corner_in_water(struct coord *corner3dpos, struct coord *dstpos, f32 *dstfrac);
void sky_calculate_edge_vertex(struct coord *base, struct coord *ref, struct coord *out);
f32 sky_clamp(f32 value, f32 min, f32 max);
void sky_choose_cloud_vtx_colour(struct skyvtx3d *arg0, f32 arg1);
void sky_choose_water_vtx_colour(struct skyvtx3d *arg0, f32 arg1);

#define SKY_VTX_RANGE 32000.0f
// 256 texels in s10.5: a whole number of repeats of any sky or water texture up to 256 wide.
#define SKY_TC_PERIOD (256.0f * 32.0f)
#define SKY_WIDEN_X 2.0f
#define SKY_WIDEN_Y 1.25f

enum skykind { SKY_CLOUDS, SKY_WATER, SKY_GROUND };

static f32 sky_abs(f32 value)
{
	return value < 0.0f ? -value : value;
}

// Draws a polygon of sky_render's vertices (world positions times scale), as its raw triangles did.
static Gfx *sky_draw_polygon(Gfx *gdl, struct skyvtx3d *src, s32 count, f32 scale, enum skykind kind)
{
	struct environment *env = env_get_current();
	struct player *player = g_Vars.currentplayer;
	Mtxf *w2s = cam_get_world_to_screen_mtxf();
	Vtx *verts = gfx_allocate_vertices(count);
	Col *cols = gfx_allocate_colours(count);
	Mtx *mtx = gfx_allocate_matrix();
	Mtxf mv;
	struct zrange zrange;
	f32 inv[3][3];
	f32 det;
	f32 eye[3];
	f32 rel[5][3];
	f32 tc[5][2];
	f32 mean[2] = {0.0f, 0.0f};
	f32 shift[2];
	f32 depth;
	f32 maxdepth = 0.0f;
	f32 mindepth = 1e9f;
	f32 pull;
	f32 maxabs = 1.0f;
	f32 fit;
	f32 shear;
	s32 i;
	s32 j;

	// The eye from the world-to-camera matrix (row vectors: camera = world * R + t, so eye = -t * R^-1).
	det = w2s->m[0][0] * (w2s->m[1][1] * w2s->m[2][2] - w2s->m[1][2] * w2s->m[2][1])
		- w2s->m[0][1] * (w2s->m[1][0] * w2s->m[2][2] - w2s->m[1][2] * w2s->m[2][0])
		+ w2s->m[0][2] * (w2s->m[1][0] * w2s->m[2][1] - w2s->m[1][1] * w2s->m[2][0]);
	inv[0][0] = (w2s->m[1][1] * w2s->m[2][2] - w2s->m[1][2] * w2s->m[2][1]) / det;
	inv[0][1] = (w2s->m[0][2] * w2s->m[2][1] - w2s->m[0][1] * w2s->m[2][2]) / det;
	inv[0][2] = (w2s->m[0][1] * w2s->m[1][2] - w2s->m[0][2] * w2s->m[1][1]) / det;
	inv[1][0] = (w2s->m[1][2] * w2s->m[2][0] - w2s->m[1][0] * w2s->m[2][2]) / det;
	inv[1][1] = (w2s->m[0][0] * w2s->m[2][2] - w2s->m[0][2] * w2s->m[2][0]) / det;
	inv[1][2] = (w2s->m[0][2] * w2s->m[1][0] - w2s->m[0][0] * w2s->m[1][2]) / det;
	inv[2][0] = (w2s->m[1][0] * w2s->m[2][1] - w2s->m[1][1] * w2s->m[2][0]) / det;
	inv[2][1] = (w2s->m[0][1] * w2s->m[2][0] - w2s->m[0][0] * w2s->m[2][1]) / det;
	inv[2][2] = (w2s->m[0][0] * w2s->m[1][1] - w2s->m[0][1] * w2s->m[1][0]) / det;

	for (j = 0; j < 3; j++) {
		eye[j] = -(w2s->m[3][0] * inv[0][j] + w2s->m[3][1] * inv[1][j] + w2s->m[3][2] * inv[2][j]);
	}

	for (i = 0; i < count; i++) {
		rel[i][0] = src[i].x / scale - eye[0];
		rel[i][1] = src[i].y / scale - eye[1];
		rel[i][2] = src[i].z / scale - eye[2];
	}

	// Inside the far plane, the nearest vertex at least twice the near plane away.
	vi_get_z_range(&zrange);

	for (i = 0; i < count; i++) {
		depth = -(rel[i][0] * w2s->m[0][2] + rel[i][1] * w2s->m[1][2] + rel[i][2] * w2s->m[2][2]);
		maxdepth = MAX(maxdepth, depth);
		mindepth = MIN(mindepth, depth);
	}

	if (maxdepth > zrange.far * 0.9f) {
		pull = zrange.far * 0.9f / maxdepth;

		if (mindepth > 0.0f) {
			pull = MIN(1.0f, MAX(pull, 2.0f * zrange.near / mindepth));
		}

		for (i = 0; i < count; i++) {
			for (j = 0; j < 3; j++) {
				rel[i][j] *= pull;
			}
		}
	}

	for (i = 0; i < count; i++) {
		for (j = 0; j < 3; j++) {
			maxabs = MAX(maxabs, sky_abs(rel[i][j]));
		}
	}

	// The vertices are rel * fit, the rotation-only matrix divides by it. The shear is y += clouds_height * c_scaley *
	// -z in the camera's space: c_scaley is its units a pixel at a distance of 1.
	fit = SKY_VTX_RANGE / maxabs;
	shear = -env->clouds_height * player->c_scaley;

	for (i = 0; i < 4; i++) {
		for (j = 0; j < 4; j++) {
			mv.m[i][j] = i < 3 && j < 3 ? w2s->m[i][j] / fit : (i == j ? 1.0f : 0.0f);
		}

		if (i < 3) {
			mv.m[i][1] += mv.m[i][2] * shear;
		}
	}

	guMtxF2L(mv.m, mtx);

	for (i = 0; i < count; i++) {
		verts[i].x = rel[i][0] * fit;
		verts[i].y = rel[i][1] * fit;
		verts[i].z = rel[i][2] * fit;
		verts[i].flags = 0;
		verts[i].colour = i * 4;

		if (kind == SKY_WATER) {
			tc[i][0] = src[i].s * 0.1f + g_SkyCloudOffset;
			tc[i][1] = (src[i].t - g_SkyCloudOffset) * 0.1f + g_SkyCloudOffset;
		} else {
			tc[i][0] = src[i].s;
			tc[i][1] = src[i].t;
		}

		mean[0] += tc[i][0] / count;
		mean[1] += tc[i][1] / count;

		if (kind == SKY_GROUND) {
			// What the game fills the ground with when there is no water (vi_set_fill_colour in sky_render).
			cols[i].r = env->sky_r;
			cols[i].g = env->sky_g;
			cols[i].b = env->sky_b;
			cols[i].a = 0xff;
		} else {
			cols[i].r = src[i].r;
			cols[i].g = src[i].g;
			cols[i].b = src[i].b;
			cols[i].a = src[i].a;
		}
	}

	shift[0] = (s32) (mean[0] / SKY_TC_PERIOD) * SKY_TC_PERIOD;
	shift[1] = (s32) (mean[1] / SKY_TC_PERIOD) * SKY_TC_PERIOD;

	for (i = 0; i < count; i++) {
		verts[i].s = sky_clamp(tc[i][0] - shift[0], -32768.0f, 32767.0f);
		verts[i].t = sky_clamp(tc[i][1] - shift[1], -32768.0f, 32767.0f);
	}

	gEXPushGeometryMode(gdl++);
	gSPClearGeometryMode(gdl++, G_CULL_BOTH | G_LIGHTING | G_FOG | G_ZBUFFER | G_TEXTURE_GEN);
	gSPSetGeometryMode(gdl++, G_SHADE | G_SHADING_SMOOTH);
	gEXPushOtherMode(gdl++);
	gEXPushCombineMode(gdl++);
	gEXPushScissor(gdl++);
	gDPPipeSync(gdl++);
	gDPSetScissor(gdl++, G_SC_NON_INTERLACE, player->viewleft, player->viewtop,
			player->viewleft + player->viewwidth, player->viewtop + player->viewheight);
	gDPSetTexturePersp(gdl++, G_TP_PERSP);

	if (kind == SKY_GROUND) {
		gDPSetCycleType(gdl++, G_CYC_1CYCLE);
		gDPSetRenderMode(gdl++, G_RM_OPA_SURF, G_RM_OPA_SURF2);
		gDPSetCombineMode(gdl++, G_CC_SHADE, G_CC_SHADE);
	}

	gSPMatrix(gdl++, OS_K0_TO_PHYSICAL(mtx), G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_PUSH);
	gSPColor(gdl++, OS_K0_TO_PHYSICAL(cols), count);
	gSPVertex(gdl++, OS_K0_TO_PHYSICAL(verts), count, 0);

	// As sky_render's calls of sky_render_tri made them.
	if (count == 4) {
		gSPTri2(gdl++, 0, 1, 3, 3, 2, 0);
	} else if (count == 5) {
		gSPTri3(gdl++, 0, 1, 2, 0, 2, 3, 0, 3, 4);
	} else if (count == 3) {
		gSPTri1(gdl++, 0, 1, 2);
	}

	gSPPopMatrix(gdl++, G_MTX_MODELVIEW);
	gDPPipeSync(gdl++);
	gEXPopScissor(gdl++);
	gEXPopCombineMode(gdl++);
	gEXPopOtherMode(gdl++);
	gEXPopGeometryMode(gdl++);

	return gdl;
}

static Gfx *sky_render_inner(Gfx *gdl);

RECOMP_PATCH Gfx *sky_render(Gfx *gdl)
{
	struct player *player = g_Vars.currentplayer;
	f32 saved[6];

	saved[0] = player->c_screenwidth;
	saved[1] = player->c_screenheight;
	saved[2] = player->c_screenleft;
	saved[3] = player->c_screentop;
	saved[4] = player->c_halfwidth;
	saved[5] = player->c_halfheight;

	player->c_screenleft -= player->c_screenwidth * (SKY_WIDEN_X - 1.0f) * 0.5f;
	player->c_screentop -= player->c_screenheight * (SKY_WIDEN_Y - 1.0f) * 0.5f;
	player->c_screenwidth *= SKY_WIDEN_X;
	player->c_screenheight *= SKY_WIDEN_Y;
	player->c_halfwidth *= SKY_WIDEN_X;
	player->c_halfheight *= SKY_WIDEN_Y;

	gdl = sky_render_inner(gdl);

	player->c_screenwidth = saved[0];
	player->c_screenheight = saved[1];
	player->c_screenleft = saved[2];
	player->c_screentop = saved[3];
	player->c_halfwidth = saved[4];
	player->c_halfheight = saved[5];

	return gdl;
}

static Gfx *sky_render_inner(Gfx *gdl)
{
	struct coord tl3dpos;
	struct coord tr3dpos;
	struct coord bl3dpos;
	struct coord br3dpos;
	struct coord sp674;
	struct coord sp668;
	struct coord sp65c;
	struct coord sp650;
	struct coord sp644;
	struct coord sp638;
	struct coord sp62c;
	struct coord sp620;
	struct coord sp614;
	struct coord sp608;
	struct coord sp5fc;
	struct coord sp5f0;
	struct coord sp5e4;
	struct coord sp5d8;
	struct coord sp5cc;
	struct coord sp5c0;
	struct coord sp5b4;
	struct coord sp5a8;
	struct coord sp59c;
	struct coord sp590;
	f32 sp58c;
	f32 sp588;
	f32 sp584;
	f32 sp580;
	f32 sp57c;
	f32 sp578;
	f32 sp574;
	f32 sp570;
	f32 sp56c;
	f32 sp568;
	f32 sp564;
	f32 sp560;
	f32 sp55c;
	f32 sp558;
	f32 sp554;
	f32 sp550;
	f32 sp54c;
	f32 sp548;
	s32 numvertices;
	s32 j;
	s32 cornerstate;
	s32 tlcornerissky;
	s32 trcornerissky;
	s32 blcornerissky;
	s32 brcornerissky;
	struct skyvtx3d skyvertices3d[5];
	struct skyvtx3d watervertices3d[5];
	f32 tmp;
	f32 scale;
	bool sp430;
	struct environment *env;

	sp430 = false;
	env = env_get_current();

	if (!env->clouds_enabled || g_Vars.currentplayer->visionmode == VISIONMODE_XRAY) {
		if (PLAYERCOUNT() == 1) {
			gDPSetCycleType(gdl++, G_CYC_FILL);

			if (g_Vars.currentplayer->visionmode == VISIONMODE_XRAY) {
				gdl = vi_set_fill_colour(gdl, 0, 0, 0);
			} else {
				gdl = vi_set_fill_colour(gdl, env->sky_r, env->sky_g, env->sky_b);
			}

			gDPFillRectangle(gdl++, vi_get_view_left(), vi_get_view_top(),
					vi_get_view_left() + vi_get_view_width() - 1,
					vi_get_view_top() + vi_get_view_height() - 1);

			gDPPipeSync(gdl++);
			return gdl;
		}

		gDPPipeSync(gdl++);
		gDPSetCycleType(gdl++, G_CYC_FILL);

		if (g_Vars.currentplayer->visionmode == VISIONMODE_XRAY) {
			gdl = vi_set_fill_colour(gdl, 0, 0, 0);
		} else {
			gdl = vi_set_fill_colour(gdl, env->sky_r, env->sky_g, env->sky_b);
		}

		gDPSetRenderMode(gdl++, G_RM_NOOP, G_RM_NOOP2);

		gDPFillRectangle(gdl++,
				g_Vars.currentplayer->viewleft, g_Vars.currentplayer->viewtop,
				g_Vars.currentplayer->viewleft + g_Vars.currentplayer->viewwidth - 1,
				g_Vars.currentplayer->viewtop + g_Vars.currentplayer->viewheight - 1);

		gDPPipeSync(gdl++);
		return gdl;
	}

	gdl = vi_set_fill_colour(gdl, env->sky_r, env->sky_g, env->sky_b);

	sky_get_world_pos_from_screen_pos(0.0f, 0.0f, &tl3dpos);
	sky_get_world_pos_from_screen_pos(cam_get_screen_width() - 0.1f, 0.0f, &tr3dpos);
	sky_get_world_pos_from_screen_pos(0.0f, cam_get_screen_height() - 0.1f, &bl3dpos);
	sky_get_world_pos_from_screen_pos(cam_get_screen_width() - 0.1f, cam_get_screen_height() - 0.1f, &br3dpos);

	tlcornerissky = sky_is_screen_corner_in_sky(&tl3dpos, &sp644, &sp58c);
	trcornerissky = sky_is_screen_corner_in_sky(&tr3dpos, &sp638, &sp588);
	blcornerissky = sky_is_screen_corner_in_sky(&bl3dpos, &sp62c, &sp584);
	brcornerissky = sky_is_screen_corner_in_sky(&br3dpos, &sp620, &sp580);

	sky_is_corner_in_water(&tl3dpos, &sp5e4, &sp56c);
	sky_is_corner_in_water(&tr3dpos, &sp5d8, &sp568);
	sky_is_corner_in_water(&bl3dpos, &sp5cc, &sp564);
	sky_is_corner_in_water(&br3dpos, &sp5c0, &sp560);

	// For each screen edge, check if one vertex is off-sky and the other is on-sky.
	// If so, calculate where along the the edge the sky starts/ends.
	if (tlcornerissky != blcornerissky) {
		sp54c = cam_get_screen_top() + cam_get_screen_height() * (tl3dpos.f[1] / (tl3dpos.f[1] - bl3dpos.f[1]));

		sky_get_world_pos_from_screen_pos(0.0f, sp54c, &sp65c);
		sky_calculate_edge_vertex(&tl3dpos, &bl3dpos, &sp65c);
		sky_is_screen_corner_in_sky(&sp65c, &sp5fc, &sp574);
		sky_is_corner_in_water(&sp65c, &sp59c, &sp554);
	} else {
		sp54c = 0.0f;
	}

	if (trcornerissky != brcornerissky) {
		sp548 = cam_get_screen_top() + cam_get_screen_height() * (tr3dpos.f[1] / (tr3dpos.f[1] - br3dpos.f[1]));

		sky_get_world_pos_from_screen_pos(cam_get_screen_width() - 0.1f, sp548, &sp650);
		sky_calculate_edge_vertex(&tr3dpos, &br3dpos, &sp650);
		sky_is_screen_corner_in_sky(&sp650, &sp5f0, &sp570);
		sky_is_corner_in_water(&sp650, &sp590, &sp550);
	} else {
		sp548 = 0.0f;
	}

	if (tlcornerissky != trcornerissky) {
		sky_get_world_pos_from_screen_pos(cam_get_screen_left() + cam_get_screen_width() * (tl3dpos.f[1] / (tl3dpos.f[1] - tr3dpos.f[1])), 0.0f, &sp674);
		sky_calculate_edge_vertex(&tl3dpos, &tr3dpos, &sp674);
		sky_is_screen_corner_in_sky(&sp674, &sp614, &sp57c);
		sky_is_corner_in_water(&sp674, &sp5b4, &sp55c);
	}

	if (blcornerissky != brcornerissky) {
		tmp = cam_get_screen_left() + cam_get_screen_width() * (bl3dpos.f[1] / (bl3dpos.f[1] - br3dpos.f[1]));

		sky_get_world_pos_from_screen_pos(tmp, cam_get_screen_height() - 0.1f, &sp668);
		sky_calculate_edge_vertex(&bl3dpos, &br3dpos, &sp668);
		sky_is_screen_corner_in_sky(&sp668, &sp608, &sp578);
		sky_is_corner_in_water(&sp668, &sp5a8, &sp558);
	}

	cornerstate = (tlcornerissky << 3) | (trcornerissky << 2) | (blcornerissky << 1) | brcornerissky;

	/**
	 * Do maths stuff for the ground/water/below-horizon plane.
	 *
	 * The CORNERSTATE constants denote which corners are in the sky,
	 * which is why these cases appear to be inverted.
	 */
	switch (cornerstate) {
	case CORNERSTATE_FULL:
		// All four screen corners are in the sky.
		numvertices = 0;
		scale = 1.0f / 30.0f;
		break;
	case CORNERSTATE_NONE:
		// All four screen corners are on the ground.
		numvertices = 4;
		scale = 1.0f / 30.0f;
		watervertices3d[0].x = sp5e4.f[0] * scale;
		watervertices3d[0].y = sp5e4.f[1] * scale;
		watervertices3d[0].z = sp5e4.f[2] * scale;
		watervertices3d[1].x = sp5d8.f[0] * scale;
		watervertices3d[1].y = sp5d8.f[1] * scale;
		watervertices3d[1].z = sp5d8.f[2] * scale;
		watervertices3d[2].x = sp5cc.f[0] * scale;
		watervertices3d[2].y = sp5cc.f[1] * scale;
		watervertices3d[2].z = sp5cc.f[2] * scale;
		watervertices3d[3].x = sp5c0.f[0] * scale;
		watervertices3d[3].y = sp5c0.f[1] * scale;
		watervertices3d[3].z = sp5c0.f[2] * scale;
		watervertices3d[0].s = sp5e4.f[0];
		watervertices3d[0].t = sp5e4.f[2] + g_SkyCloudOffset;
		watervertices3d[1].s = sp5d8.f[0];
		watervertices3d[1].t = sp5d8.f[2] + g_SkyCloudOffset;
		watervertices3d[2].s = sp5cc.f[0];
		watervertices3d[2].t = sp5cc.f[2] + g_SkyCloudOffset;
		watervertices3d[3].s = sp5c0.f[0];
		watervertices3d[3].t = sp5c0.f[2] + g_SkyCloudOffset;

		sky_choose_water_vtx_colour(&watervertices3d[0], sp56c);
		sky_choose_water_vtx_colour(&watervertices3d[1], sp568);
		sky_choose_water_vtx_colour(&watervertices3d[2], sp564);
		sky_choose_water_vtx_colour(&watervertices3d[3], sp560);
		break;
	case CORNERSTATE_BOTTOM:
		// The bottom corners are in the sky.
		// This is possible by turning the drugspy upside down in Air Base.
		numvertices = 4;
		scale = 1.0f / 30.0f;
		watervertices3d[0].x = sp5e4.f[0] * scale;
		watervertices3d[0].y = sp5e4.f[1] * scale;
		watervertices3d[0].z = sp5e4.f[2] * scale;
		watervertices3d[1].x = sp5d8.f[0] * scale;
		watervertices3d[1].y = sp5d8.f[1] * scale;
		watervertices3d[1].z = sp5d8.f[2] * scale;
		watervertices3d[2].x = sp59c.f[0] * scale;
		watervertices3d[2].y = sp59c.f[1] * scale;
		watervertices3d[2].z = sp59c.f[2] * scale;
		watervertices3d[3].x = sp590.f[0] * scale;
		watervertices3d[3].y = sp590.f[1] * scale;
		watervertices3d[3].z = sp590.f[2] * scale;
		watervertices3d[0].s = sp5e4.f[0];
		watervertices3d[0].t = sp5e4.f[2] + g_SkyCloudOffset;
		watervertices3d[1].s = sp5d8.f[0];
		watervertices3d[1].t = sp5d8.f[2] + g_SkyCloudOffset;
		watervertices3d[2].s = sp59c.f[0];
		watervertices3d[2].t = sp59c.f[2] + g_SkyCloudOffset;
		watervertices3d[3].s = sp590.f[0];
		watervertices3d[3].t = sp590.f[2] + g_SkyCloudOffset;

		sky_choose_water_vtx_colour(&watervertices3d[0], sp56c);
		sky_choose_water_vtx_colour(&watervertices3d[1], sp568);
		sky_choose_water_vtx_colour(&watervertices3d[2], sp554);
		sky_choose_water_vtx_colour(&watervertices3d[3], sp550);
		break;
	case CORNERSTATE_TOP:
		// The top corners are in the sky. A common occurrence.
		numvertices = 4;
		sp430 = true;
		scale = 1.0f / 30.0f;
		watervertices3d[0].x = sp5c0.f[0] * scale;
		watervertices3d[0].y = sp5c0.f[1] * scale;
		watervertices3d[0].z = sp5c0.f[2] * scale;
		watervertices3d[1].x = sp5cc.f[0] * scale;
		watervertices3d[1].y = sp5cc.f[1] * scale;
		watervertices3d[1].z = sp5cc.f[2] * scale;
		watervertices3d[2].x = sp590.f[0] * scale;
		watervertices3d[2].y = sp590.f[1] * scale;
		watervertices3d[2].z = sp590.f[2] * scale;
		watervertices3d[3].x = sp59c.f[0] * scale;
		watervertices3d[3].y = sp59c.f[1] * scale;
		watervertices3d[3].z = sp59c.f[2] * scale;
		watervertices3d[0].s = sp5c0.f[0];
		watervertices3d[0].t = sp5c0.f[2] + g_SkyCloudOffset;
		watervertices3d[1].s = sp5cc.f[0];
		watervertices3d[1].t = sp5cc.f[2] + g_SkyCloudOffset;
		watervertices3d[2].s = sp590.f[0];
		watervertices3d[2].t = sp590.f[2] + g_SkyCloudOffset;
		watervertices3d[3].s = sp59c.f[0];
		watervertices3d[3].t = sp59c.f[2] + g_SkyCloudOffset;

		sky_choose_water_vtx_colour(&watervertices3d[0], sp560);
		sky_choose_water_vtx_colour(&watervertices3d[1], sp564);
		sky_choose_water_vtx_colour(&watervertices3d[2], sp550);
		sky_choose_water_vtx_colour(&watervertices3d[3], sp554);
		break;
	case CORNERSTATE_LEFT:
		// The left side corners are in the sky.
		// This would happen if the camera rolls significantly.
		numvertices = 4;
		scale = 1.0f / 30.0f;
		watervertices3d[0].x = sp5d8.f[0] * scale;
		watervertices3d[0].y = sp5d8.f[1] * scale;
		watervertices3d[0].z = sp5d8.f[2] * scale;
		watervertices3d[1].x = sp5c0.f[0] * scale;
		watervertices3d[1].y = sp5c0.f[1] * scale;
		watervertices3d[1].z = sp5c0.f[2] * scale;
		watervertices3d[2].x = sp5b4.f[0] * scale;
		watervertices3d[2].y = sp5b4.f[1] * scale;
		watervertices3d[2].z = sp5b4.f[2] * scale;
		watervertices3d[3].x = sp5a8.f[0] * scale;
		watervertices3d[3].y = sp5a8.f[1] * scale;
		watervertices3d[3].z = sp5a8.f[2] * scale;
		watervertices3d[0].s = sp5d8.f[0];
		watervertices3d[0].t = sp5d8.f[2] + g_SkyCloudOffset;
		watervertices3d[1].s = sp5c0.f[0];
		watervertices3d[1].t = sp5c0.f[2] + g_SkyCloudOffset;
		watervertices3d[2].s = sp5b4.f[0];
		watervertices3d[2].t = sp5b4.f[2] + g_SkyCloudOffset;
		watervertices3d[3].s = sp5a8.f[0];
		watervertices3d[3].t = sp5a8.f[2] + g_SkyCloudOffset;

		sky_choose_water_vtx_colour(&watervertices3d[0], sp568);
		sky_choose_water_vtx_colour(&watervertices3d[1], sp560);
		sky_choose_water_vtx_colour(&watervertices3d[2], sp55c);
		sky_choose_water_vtx_colour(&watervertices3d[3], sp558);
		break;
	case CORNERSTATE_RIGHT:
		// The right side corners are in the sky.
		// This would happen if the camera rolls significantly.
		numvertices = 4;
		scale = 1.0f / 30.0f;
		watervertices3d[0].x = sp5cc.f[0] * scale;
		watervertices3d[0].y = sp5cc.f[1] * scale;
		watervertices3d[0].z = sp5cc.f[2] * scale;
		watervertices3d[1].x = sp5e4.f[0] * scale;
		watervertices3d[1].y = sp5e4.f[1] * scale;
		watervertices3d[1].z = sp5e4.f[2] * scale;
		watervertices3d[2].x = sp5a8.f[0] * scale;
		watervertices3d[2].y = sp5a8.f[1] * scale;
		watervertices3d[2].z = sp5a8.f[2] * scale;
		watervertices3d[3].x = sp5b4.f[0] * scale;
		watervertices3d[3].y = sp5b4.f[1] * scale;
		watervertices3d[3].z = sp5b4.f[2] * scale;
		watervertices3d[0].s = sp5cc.f[0];
		watervertices3d[0].t = sp5cc.f[2] + g_SkyCloudOffset;
		watervertices3d[1].s = sp5e4.f[0];
		watervertices3d[1].t = sp5e4.f[2] + g_SkyCloudOffset;
		watervertices3d[2].s = sp5a8.f[0];
		watervertices3d[2].t = sp5a8.f[2] + g_SkyCloudOffset;
		watervertices3d[3].s = sp5b4.f[0];
		watervertices3d[3].t = sp5b4.f[2] + g_SkyCloudOffset;

		sky_choose_water_vtx_colour(&watervertices3d[0], sp564);
		sky_choose_water_vtx_colour(&watervertices3d[1], sp56c);
		sky_choose_water_vtx_colour(&watervertices3d[2], sp558);
		sky_choose_water_vtx_colour(&watervertices3d[3], sp55c);
		break;
	case CORNERSTATE_TL_TR_BL:
		numvertices = 3;
		scale = 1.0f / 30.0f;
		watervertices3d[0].x = sp5c0.f[0] * scale;
		watervertices3d[0].y = sp5c0.f[1] * scale;
		watervertices3d[0].z = sp5c0.f[2] * scale;
		watervertices3d[1].x = sp5a8.f[0] * scale;
		watervertices3d[1].y = sp5a8.f[1] * scale;
		watervertices3d[1].z = sp5a8.f[2] * scale;
		watervertices3d[2].x = sp590.f[0] * scale;
		watervertices3d[2].y = sp590.f[1] * scale;
		watervertices3d[2].z = sp590.f[2] * scale;
		watervertices3d[0].s = sp5c0.f[0];
		watervertices3d[0].t = sp5c0.f[2] + g_SkyCloudOffset;
		watervertices3d[1].s = sp5a8.f[0];
		watervertices3d[1].t = sp5a8.f[2] + g_SkyCloudOffset;
		watervertices3d[2].s = sp590.f[0];
		watervertices3d[2].t = sp590.f[2] + g_SkyCloudOffset;

		sky_choose_water_vtx_colour(&watervertices3d[0], sp560);
		sky_choose_water_vtx_colour(&watervertices3d[1], sp558);
		sky_choose_water_vtx_colour(&watervertices3d[2], sp550);
		break;
	case CORNERSTATE_TL_TR_BR:
		numvertices = 3;
		scale = 1.0f / 30.0f;
		watervertices3d[0].x = sp5cc.f[0] * scale;
		watervertices3d[0].y = sp5cc.f[1] * scale;
		watervertices3d[0].z = sp5cc.f[2] * scale;
		watervertices3d[1].x = sp59c.f[0] * scale;
		watervertices3d[1].y = sp59c.f[1] * scale;
		watervertices3d[1].z = sp59c.f[2] * scale;
		watervertices3d[2].x = sp5a8.f[0] * scale;
		watervertices3d[2].y = sp5a8.f[1] * scale;
		watervertices3d[2].z = sp5a8.f[2] * scale;
		watervertices3d[0].s = sp5cc.f[0];
		watervertices3d[0].t = sp5cc.f[2] + g_SkyCloudOffset;
		watervertices3d[1].s = sp59c.f[0];
		watervertices3d[1].t = sp59c.f[2] + g_SkyCloudOffset;
		watervertices3d[2].s = sp5a8.f[0];
		watervertices3d[2].t = sp5a8.f[2] + g_SkyCloudOffset;

		sky_choose_water_vtx_colour(&watervertices3d[0], sp564);
		sky_choose_water_vtx_colour(&watervertices3d[1], sp554);
		sky_choose_water_vtx_colour(&watervertices3d[2], sp558);
		break;
	case CORNERSTATE_TL_BL_BR:
		numvertices = 3;
		scale = 1.0f / 30.0f;
		watervertices3d[0].x = sp5d8.f[0] * scale;
		watervertices3d[0].y = sp5d8.f[1] * scale;
		watervertices3d[0].z = sp5d8.f[2] * scale;
		watervertices3d[1].x = sp590.f[0] * scale;
		watervertices3d[1].y = sp590.f[1] * scale;
		watervertices3d[1].z = sp590.f[2] * scale;
		watervertices3d[2].x = sp5b4.f[0] * scale;
		watervertices3d[2].y = sp5b4.f[1] * scale;
		watervertices3d[2].z = sp5b4.f[2] * scale;
		watervertices3d[0].s = sp5d8.f[0];
		watervertices3d[0].t = sp5d8.f[2] + g_SkyCloudOffset;
		watervertices3d[1].s = sp590.f[0];
		watervertices3d[1].t = sp590.f[2] + g_SkyCloudOffset;
		watervertices3d[2].s = sp5b4.f[0];
		watervertices3d[2].t = sp5b4.f[2] + g_SkyCloudOffset;

		sky_choose_water_vtx_colour(&watervertices3d[0], sp568);
		sky_choose_water_vtx_colour(&watervertices3d[1], sp550);
		sky_choose_water_vtx_colour(&watervertices3d[2], sp55c);
		break;
	case CORNERSTATE_TR_BL_BR:
		numvertices = 3;
		scale = 1.0f / 30.0f;
		watervertices3d[0].x = sp5e4.f[0] * scale;
		watervertices3d[0].y = sp5e4.f[1] * scale;
		watervertices3d[0].z = sp5e4.f[2] * scale;
		watervertices3d[1].x = sp5b4.f[0] * scale;
		watervertices3d[1].y = sp5b4.f[1] * scale;
		watervertices3d[1].z = sp5b4.f[2] * scale;
		watervertices3d[2].x = sp59c.f[0] * scale;
		watervertices3d[2].y = sp59c.f[1] * scale;
		watervertices3d[2].z = sp59c.f[2] * scale;
		watervertices3d[0].s = sp5e4.f[0];
		watervertices3d[0].t = sp5e4.f[2] + g_SkyCloudOffset;
		watervertices3d[1].s = sp5b4.f[0];
		watervertices3d[1].t = sp5b4.f[2] + g_SkyCloudOffset;
		watervertices3d[2].s = sp59c.f[0];
		watervertices3d[2].t = sp59c.f[2] + g_SkyCloudOffset;

		sky_choose_water_vtx_colour(&watervertices3d[0], sp56c);
		sky_choose_water_vtx_colour(&watervertices3d[1], sp55c);
		sky_choose_water_vtx_colour(&watervertices3d[2], sp554);
		break;
	case CORNERSTATE_BR:
		numvertices = 5;
		scale = 1.0f / 30.0f;
		watervertices3d[0].x = sp5cc.f[0] * scale;
		watervertices3d[0].y = sp5cc.f[1] * scale;
		watervertices3d[0].z = sp5cc.f[2] * scale;
		watervertices3d[1].x = sp5e4.f[0] * scale;
		watervertices3d[1].y = sp5e4.f[1] * scale;
		watervertices3d[1].z = sp5e4.f[2] * scale;
		watervertices3d[2].x = sp5d8.f[0] * scale;
		watervertices3d[2].y = sp5d8.f[1] * scale;
		watervertices3d[2].z = sp5d8.f[2] * scale;
		watervertices3d[3].x = sp590.f[0] * scale;
		watervertices3d[3].y = sp590.f[1] * scale;
		watervertices3d[3].z = sp590.f[2] * scale;
		watervertices3d[4].x = sp5a8.f[0] * scale;
		watervertices3d[4].y = sp5a8.f[1] * scale;
		watervertices3d[4].z = sp5a8.f[2] * scale;
		watervertices3d[0].s = sp5cc.f[0];
		watervertices3d[0].t = sp5cc.f[2] + g_SkyCloudOffset;
		watervertices3d[1].s = sp5e4.f[0];
		watervertices3d[1].t = sp5e4.f[2] + g_SkyCloudOffset;
		watervertices3d[2].s = sp5d8.f[0];
		watervertices3d[2].t = sp5d8.f[2] + g_SkyCloudOffset;
		watervertices3d[3].s = sp590.f[0];
		watervertices3d[3].t = sp590.f[2] + g_SkyCloudOffset;
		watervertices3d[4].s = sp5a8.f[0];
		watervertices3d[4].t = sp5a8.f[2] + g_SkyCloudOffset;

		sky_choose_water_vtx_colour(&watervertices3d[0], sp564);
		sky_choose_water_vtx_colour(&watervertices3d[1], sp56c);
		sky_choose_water_vtx_colour(&watervertices3d[2], sp568);
		sky_choose_water_vtx_colour(&watervertices3d[3], sp550);
		sky_choose_water_vtx_colour(&watervertices3d[4], sp558);
		break;
	case CORNERSTATE_BL:
		numvertices = 5;
		scale = 1.0f / 30.0f;
		watervertices3d[0].x = sp5e4.f[0] * scale;
		watervertices3d[0].y = sp5e4.f[1] * scale;
		watervertices3d[0].z = sp5e4.f[2] * scale;
		watervertices3d[1].x = sp5d8.f[0] * scale;
		watervertices3d[1].y = sp5d8.f[1] * scale;
		watervertices3d[1].z = sp5d8.f[2] * scale;
		watervertices3d[2].x = sp5c0.f[0] * scale;
		watervertices3d[2].y = sp5c0.f[1] * scale;
		watervertices3d[2].z = sp5c0.f[2] * scale;
		watervertices3d[3].x = sp5a8.f[0] * scale;
		watervertices3d[3].y = sp5a8.f[1] * scale;
		watervertices3d[3].z = sp5a8.f[2] * scale;
		watervertices3d[4].x = sp59c.f[0] * scale;
		watervertices3d[4].y = sp59c.f[1] * scale;
		watervertices3d[4].z = sp59c.f[2] * scale;
		watervertices3d[0].s = sp5e4.f[0];
		watervertices3d[0].t = sp5e4.f[2] + g_SkyCloudOffset;
		watervertices3d[1].s = sp5d8.f[0];
		watervertices3d[1].t = sp5d8.f[2] + g_SkyCloudOffset;
		watervertices3d[2].s = sp5c0.f[0];
		watervertices3d[2].t = sp5c0.f[2] + g_SkyCloudOffset;
		watervertices3d[3].s = sp5a8.f[0];
		watervertices3d[3].t = sp5a8.f[2] + g_SkyCloudOffset;
		watervertices3d[4].s = sp59c.f[0];
		watervertices3d[4].t = sp59c.f[2] + g_SkyCloudOffset;

		sky_choose_water_vtx_colour(&watervertices3d[0], sp56c);
		sky_choose_water_vtx_colour(&watervertices3d[1], sp568);
		sky_choose_water_vtx_colour(&watervertices3d[2], sp560);
		sky_choose_water_vtx_colour(&watervertices3d[3], sp558);
		sky_choose_water_vtx_colour(&watervertices3d[4], sp554);
		break;
	case CORNERSTATE_TR:
		numvertices = 5;
		scale = 1.0f / 30.0f;
		watervertices3d[0].x = sp5c0.f[0] * scale;
		watervertices3d[0].y = sp5c0.f[1] * scale;
		watervertices3d[0].z = sp5c0.f[2] * scale;
		watervertices3d[1].x = sp5cc.f[0] * scale;
		watervertices3d[1].y = sp5cc.f[1] * scale;
		watervertices3d[1].z = sp5cc.f[2] * scale;
		watervertices3d[2].x = sp5e4.f[0] * scale;
		watervertices3d[2].y = sp5e4.f[1] * scale;
		watervertices3d[2].z = sp5e4.f[2] * scale;
		watervertices3d[3].x = sp5b4.f[0] * scale;
		watervertices3d[3].y = sp5b4.f[1] * scale;
		watervertices3d[3].z = sp5b4.f[2] * scale;
		watervertices3d[4].x = sp590.f[0] * scale;
		watervertices3d[4].y = sp590.f[1] * scale;
		watervertices3d[4].z = sp590.f[2] * scale;
		watervertices3d[0].s = sp5c0.f[0];
		watervertices3d[0].t = sp5c0.f[2] + g_SkyCloudOffset;
		watervertices3d[1].s = sp5cc.f[0];
		watervertices3d[1].t = sp5cc.f[2] + g_SkyCloudOffset;
		watervertices3d[2].s = sp5e4.f[0];
		watervertices3d[2].t = sp5e4.f[2] + g_SkyCloudOffset;
		watervertices3d[3].s = sp5b4.f[0];
		watervertices3d[3].t = sp5b4.f[2] + g_SkyCloudOffset;
		watervertices3d[4].s = sp590.f[0];
		watervertices3d[4].t = sp590.f[2] + g_SkyCloudOffset;

		sky_choose_water_vtx_colour(&watervertices3d[0], sp560);
		sky_choose_water_vtx_colour(&watervertices3d[1], sp564);
		sky_choose_water_vtx_colour(&watervertices3d[2], sp56c);
		sky_choose_water_vtx_colour(&watervertices3d[3], sp55c);
		sky_choose_water_vtx_colour(&watervertices3d[4], sp550);
		break;
	case CORNERSTATE_TL:
		numvertices = 5;
		scale = 1.0f / 30.0f;
		watervertices3d[0].x = sp5d8.f[0] * scale;
		watervertices3d[0].y = sp5d8.f[1] * scale;
		watervertices3d[0].z = sp5d8.f[2] * scale;
		watervertices3d[1].x = sp5c0.f[0] * scale;
		watervertices3d[1].y = sp5c0.f[1] * scale;
		watervertices3d[1].z = sp5c0.f[2] * scale;
		watervertices3d[2].x = sp5cc.f[0] * scale;
		watervertices3d[2].y = sp5cc.f[1] * scale;
		watervertices3d[2].z = sp5cc.f[2] * scale;
		watervertices3d[3].x = sp59c.f[0] * scale;
		watervertices3d[3].y = sp59c.f[1] * scale;
		watervertices3d[3].z = sp59c.f[2] * scale;
		watervertices3d[4].x = sp5b4.f[0] * scale;
		watervertices3d[4].y = sp5b4.f[1] * scale;
		watervertices3d[4].z = sp5b4.f[2] * scale;
		watervertices3d[0].s = sp5d8.f[0];
		watervertices3d[0].t = sp5d8.f[2] + g_SkyCloudOffset;
		watervertices3d[1].s = sp5c0.f[0];
		watervertices3d[1].t = sp5c0.f[2] + g_SkyCloudOffset;
		watervertices3d[2].s = sp5cc.f[0];
		watervertices3d[2].t = sp5cc.f[2] + g_SkyCloudOffset;
		watervertices3d[3].s = sp59c.f[0];
		watervertices3d[3].t = sp59c.f[2] + g_SkyCloudOffset;
		watervertices3d[4].s = sp5b4.f[0];
		watervertices3d[4].t = sp5b4.f[2] + g_SkyCloudOffset;

		sky_choose_water_vtx_colour(&watervertices3d[0], sp568);
		sky_choose_water_vtx_colour(&watervertices3d[1], sp560);
		sky_choose_water_vtx_colour(&watervertices3d[2], sp564);
		sky_choose_water_vtx_colour(&watervertices3d[3], sp554);
		sky_choose_water_vtx_colour(&watervertices3d[4], sp55c);
		break;
	default:
		return gdl;
	}

	if (numvertices > 0) {
		// Some corners are not in the sky, so consider water
		// @recomp: drawn by sky_draw_polygon, and the ground without water too.
		if (!env->water_enabled) {
			gdl = sky_draw_polygon(gdl, watervertices3d, numvertices, scale, SKY_GROUND);
		} else {
			gDPPipeSync(gdl++);

			tex_select(&gdl, &g_TexSkyWaterConfigs[env->water_type], 1, 0, 2, 1, NULL);

			gDPSetRenderMode(gdl++, G_RM_OPA_SURF, G_RM_OPA_SURF2);

			gdl = sky_draw_polygon(gdl, watervertices3d, numvertices, scale, SKY_WATER);
		}
	}

	/**
	 * Maths for the upper half of the skydome.
	 */
	switch (cornerstate) {
	case CORNERSTATE_NONE:
		// All four screen corners are on the ground.
		return gdl;
	case CORNERSTATE_FULL:
		// All four screen corners are in the sky.
		numvertices = 4;
		skyvertices3d[0].x = sp644.f[0] * scale;
		skyvertices3d[0].y = sp644.f[1] * scale;
		skyvertices3d[0].z = sp644.f[2] * scale;
		skyvertices3d[1].x = sp638.f[0] * scale;
		skyvertices3d[1].y = sp638.f[1] * scale;
		skyvertices3d[1].z = sp638.f[2] * scale;
		skyvertices3d[2].x = sp62c.f[0] * scale;
		skyvertices3d[2].y = sp62c.f[1] * scale;
		skyvertices3d[2].z = sp62c.f[2] * scale;
		skyvertices3d[3].x = sp620.f[0] * scale;
		skyvertices3d[3].y = sp620.f[1] * scale;
		skyvertices3d[3].z = sp620.f[2] * scale;
		skyvertices3d[0].s = sp644.f[0] * 0.1f;
		skyvertices3d[0].t = sp644.f[2] * 0.1f + g_SkyCloudOffset;
		skyvertices3d[1].s = sp638.f[0] * 0.1f;
		skyvertices3d[1].t = sp638.f[2] * 0.1f + g_SkyCloudOffset;
		skyvertices3d[2].s = sp62c.f[0] * 0.1f;
		skyvertices3d[2].t = sp62c.f[2] * 0.1f + g_SkyCloudOffset;
		skyvertices3d[3].s = sp620.f[0] * 0.1f;
		skyvertices3d[3].t = sp620.f[2] * 0.1f + g_SkyCloudOffset;

		sky_choose_cloud_vtx_colour(&skyvertices3d[0], sp58c);
		sky_choose_cloud_vtx_colour(&skyvertices3d[1], sp588);
		sky_choose_cloud_vtx_colour(&skyvertices3d[2], sp584);
		sky_choose_cloud_vtx_colour(&skyvertices3d[3], sp580);
		break;
	case CORNERSTATE_TOP:
		numvertices = 4;
		skyvertices3d[0].x = sp644.f[0] * scale;
		skyvertices3d[0].y = sp644.f[1] * scale;
		skyvertices3d[0].z = sp644.f[2] * scale;
		skyvertices3d[1].x = sp638.f[0] * scale;
		skyvertices3d[1].y = sp638.f[1] * scale;
		skyvertices3d[1].z = sp638.f[2] * scale;
		skyvertices3d[2].x = sp5fc.f[0] * scale;
		skyvertices3d[2].y = sp5fc.f[1] * scale;
		skyvertices3d[2].z = sp5fc.f[2] * scale;
		skyvertices3d[3].x = sp5f0.f[0] * scale;
		skyvertices3d[3].y = sp5f0.f[1] * scale;
		skyvertices3d[3].z = sp5f0.f[2] * scale;
		skyvertices3d[0].s = sp644.f[0] * 0.1f;
		skyvertices3d[0].t = sp644.f[2] * 0.1f + g_SkyCloudOffset;
		skyvertices3d[1].s = sp638.f[0] * 0.1f;
		skyvertices3d[1].t = sp638.f[2] * 0.1f + g_SkyCloudOffset;
		skyvertices3d[2].s = sp5fc.f[0] * 0.1f;
		skyvertices3d[2].t = sp5fc.f[2] * 0.1f + g_SkyCloudOffset;
		skyvertices3d[3].s = sp5f0.f[0] * 0.1f;
		skyvertices3d[3].t = sp5f0.f[2] * 0.1f + g_SkyCloudOffset;

		sky_choose_cloud_vtx_colour(&skyvertices3d[0], sp58c);
		sky_choose_cloud_vtx_colour(&skyvertices3d[1], sp588);
		sky_choose_cloud_vtx_colour(&skyvertices3d[2], sp574);
		sky_choose_cloud_vtx_colour(&skyvertices3d[3], sp570);
		break;
	case CORNERSTATE_BOTTOM:
		numvertices = 4;
		skyvertices3d[0].x = sp620.f[0] * scale;
		skyvertices3d[0].y = sp620.f[1] * scale;
		skyvertices3d[0].z = sp620.f[2] * scale;
		skyvertices3d[1].x = sp62c.f[0] * scale;
		skyvertices3d[1].y = sp62c.f[1] * scale;
		skyvertices3d[1].z = sp62c.f[2] * scale;
		skyvertices3d[2].x = sp5f0.f[0] * scale;
		skyvertices3d[2].y = sp5f0.f[1] * scale;
		skyvertices3d[2].z = sp5f0.f[2] * scale;
		skyvertices3d[3].x = sp5fc.f[0] * scale;
		skyvertices3d[3].y = sp5fc.f[1] * scale;
		skyvertices3d[3].z = sp5fc.f[2] * scale;
		skyvertices3d[0].s = sp620.f[0] * 0.1f;
		skyvertices3d[0].t = sp620.f[2] * 0.1f + g_SkyCloudOffset;
		skyvertices3d[1].s = sp62c.f[0] * 0.1f;
		skyvertices3d[1].t = sp62c.f[2] * 0.1f + g_SkyCloudOffset;
		skyvertices3d[2].s = sp5f0.f[0] * 0.1f;
		skyvertices3d[2].t = sp5f0.f[2] * 0.1f + g_SkyCloudOffset;
		skyvertices3d[3].s = sp5fc.f[0] * 0.1f;
		skyvertices3d[3].t = sp5fc.f[2] * 0.1f + g_SkyCloudOffset;

		sky_choose_cloud_vtx_colour(&skyvertices3d[0], sp580);
		sky_choose_cloud_vtx_colour(&skyvertices3d[1], sp584);
		sky_choose_cloud_vtx_colour(&skyvertices3d[2], sp570);
		sky_choose_cloud_vtx_colour(&skyvertices3d[3], sp574);
		break;
	case CORNERSTATE_RIGHT:
		numvertices = 4;
		skyvertices3d[0].x = sp638.f[0] * scale;
		skyvertices3d[0].y = sp638.f[1] * scale;
		skyvertices3d[0].z = sp638.f[2] * scale;
		skyvertices3d[1].x = sp620.f[0] * scale;
		skyvertices3d[1].y = sp620.f[1] * scale;
		skyvertices3d[1].z = sp620.f[2] * scale;
		skyvertices3d[2].x = sp614.f[0] * scale;
		skyvertices3d[2].y = sp614.f[1] * scale;
		skyvertices3d[2].z = sp614.f[2] * scale;
		skyvertices3d[3].x = sp608.f[0] * scale;
		skyvertices3d[3].y = sp608.f[1] * scale;
		skyvertices3d[3].z = sp608.f[2] * scale;
		skyvertices3d[0].s = sp638.f[0] * 0.1f;
		skyvertices3d[0].t = sp638.f[2] * 0.1f + g_SkyCloudOffset;
		skyvertices3d[1].s = sp620.f[0] * 0.1f;
		skyvertices3d[1].t = sp620.f[2] * 0.1f + g_SkyCloudOffset;
		skyvertices3d[2].s = sp614.f[0] * 0.1f;
		skyvertices3d[2].t = sp614.f[2] * 0.1f + g_SkyCloudOffset;
		skyvertices3d[3].s = sp608.f[0] * 0.1f;
		skyvertices3d[3].t = sp608.f[2] * 0.1f + g_SkyCloudOffset;

		sky_choose_cloud_vtx_colour(&skyvertices3d[0], sp588);
		sky_choose_cloud_vtx_colour(&skyvertices3d[1], sp580);
		sky_choose_cloud_vtx_colour(&skyvertices3d[2], sp57c);
		sky_choose_cloud_vtx_colour(&skyvertices3d[3], sp578);
		break;
	case CORNERSTATE_LEFT:
		numvertices = 4;
		skyvertices3d[0].x = sp62c.f[0] * scale;
		skyvertices3d[0].y = sp62c.f[1] * scale;
		skyvertices3d[0].z = sp62c.f[2] * scale;
		skyvertices3d[1].x = sp644.f[0] * scale;
		skyvertices3d[1].y = sp644.f[1] * scale;
		skyvertices3d[1].z = sp644.f[2] * scale;
		skyvertices3d[2].x = sp608.f[0] * scale;
		skyvertices3d[2].y = sp608.f[1] * scale;
		skyvertices3d[2].z = sp608.f[2] * scale;
		skyvertices3d[3].x = sp614.f[0] * scale;
		skyvertices3d[3].y = sp614.f[1] * scale;
		skyvertices3d[3].z = sp614.f[2] * scale;
		skyvertices3d[0].s = sp62c.f[0] * 0.1f;
		skyvertices3d[0].t = sp62c.f[2] * 0.1f + g_SkyCloudOffset;
		skyvertices3d[1].s = sp644.f[0] * 0.1f;
		skyvertices3d[1].t = sp644.f[2] * 0.1f + g_SkyCloudOffset;
		skyvertices3d[2].s = sp608.f[0] * 0.1f;
		skyvertices3d[2].t = sp608.f[2] * 0.1f + g_SkyCloudOffset;
		skyvertices3d[3].s = sp614.f[0] * 0.1f;
		skyvertices3d[3].t = sp614.f[2] * 0.1f + g_SkyCloudOffset;

		sky_choose_cloud_vtx_colour(&skyvertices3d[0], sp584);
		sky_choose_cloud_vtx_colour(&skyvertices3d[1], sp58c);
		sky_choose_cloud_vtx_colour(&skyvertices3d[2], sp578);
		sky_choose_cloud_vtx_colour(&skyvertices3d[3], sp57c);
		break;
	case CORNERSTATE_BR:
		numvertices = 3;
		skyvertices3d[0].x = sp620.f[0] * scale;
		skyvertices3d[0].y = sp620.f[1] * scale;
		skyvertices3d[0].z = sp620.f[2] * scale;
		skyvertices3d[1].x = sp608.f[0] * scale;
		skyvertices3d[1].y = sp608.f[1] * scale;
		skyvertices3d[1].z = sp608.f[2] * scale;
		skyvertices3d[2].x = sp5f0.f[0] * scale;
		skyvertices3d[2].y = sp5f0.f[1] * scale;
		skyvertices3d[2].z = sp5f0.f[2] * scale;
		skyvertices3d[0].s = sp620.f[0] * 0.1f;
		skyvertices3d[0].t = sp620.f[2] * 0.1f + g_SkyCloudOffset;
		skyvertices3d[1].s = sp608.f[0] * 0.1f;
		skyvertices3d[1].t = sp608.f[2] * 0.1f + g_SkyCloudOffset;
		skyvertices3d[2].s = sp5f0.f[0] * 0.1f;
		skyvertices3d[2].t = sp5f0.f[2] * 0.1f + g_SkyCloudOffset;

		sky_choose_cloud_vtx_colour(&skyvertices3d[0], sp580);
		sky_choose_cloud_vtx_colour(&skyvertices3d[1], sp578);
		sky_choose_cloud_vtx_colour(&skyvertices3d[2], sp570);
		break;
	case CORNERSTATE_BL:
		numvertices = 3;
		skyvertices3d[0].x = sp62c.f[0] * scale;
		skyvertices3d[0].y = sp62c.f[1] * scale;
		skyvertices3d[0].z = sp62c.f[2] * scale;
		skyvertices3d[1].x = sp5fc.f[0] * scale;
		skyvertices3d[1].y = sp5fc.f[1] * scale;
		skyvertices3d[1].z = sp5fc.f[2] * scale;
		skyvertices3d[2].x = sp608.f[0] * scale;
		skyvertices3d[2].y = sp608.f[1] * scale;
		skyvertices3d[2].z = sp608.f[2] * scale;
		skyvertices3d[0].s = sp62c.f[0] * 0.1f;
		skyvertices3d[0].t = sp62c.f[2] * 0.1f + g_SkyCloudOffset;
		skyvertices3d[1].s = sp5fc.f[0] * 0.1f;
		skyvertices3d[1].t = sp5fc.f[2] * 0.1f + g_SkyCloudOffset;
		skyvertices3d[2].s = sp608.f[0] * 0.1f;
		skyvertices3d[2].t = sp608.f[2] * 0.1f + g_SkyCloudOffset;

		sky_choose_cloud_vtx_colour(&skyvertices3d[0], sp584);
		sky_choose_cloud_vtx_colour(&skyvertices3d[1], sp574);
		sky_choose_cloud_vtx_colour(&skyvertices3d[2], sp578);
		break;
	case CORNERSTATE_TR:
		numvertices = 3;
		skyvertices3d[0].x = sp638.f[0] * scale;
		skyvertices3d[0].y = sp638.f[1] * scale;
		skyvertices3d[0].z = sp638.f[2] * scale;
		skyvertices3d[1].x = sp5f0.f[0] * scale;
		skyvertices3d[1].y = sp5f0.f[1] * scale;
		skyvertices3d[1].z = sp5f0.f[2] * scale;
		skyvertices3d[2].x = sp614.f[0] * scale;
		skyvertices3d[2].y = sp614.f[1] * scale;
		skyvertices3d[2].z = sp614.f[2] * scale;
		skyvertices3d[0].s = sp638.f[0] * 0.1f;
		skyvertices3d[0].t = sp638.f[2] * 0.1f + g_SkyCloudOffset;
		skyvertices3d[1].s = sp5f0.f[0] * 0.1f;
		skyvertices3d[1].t = sp5f0.f[2] * 0.1f + g_SkyCloudOffset;
		skyvertices3d[2].s = sp614.f[0] * 0.1f;
		skyvertices3d[2].t = sp614.f[2] * 0.1f + g_SkyCloudOffset;

		sky_choose_cloud_vtx_colour(&skyvertices3d[0], sp588);
		sky_choose_cloud_vtx_colour(&skyvertices3d[1], sp570);
		sky_choose_cloud_vtx_colour(&skyvertices3d[2], sp57c);
		break;
	case CORNERSTATE_TL:
		numvertices = 3;
		skyvertices3d[0].x = sp644.f[0] * scale;
		skyvertices3d[0].y = sp644.f[1] * scale;
		skyvertices3d[0].z = sp644.f[2] * scale;
		skyvertices3d[1].x = sp614.f[0] * scale;
		skyvertices3d[1].y = sp614.f[1] * scale;
		skyvertices3d[1].z = sp614.f[2] * scale;
		skyvertices3d[2].x = sp5fc.f[0] * scale;
		skyvertices3d[2].y = sp5fc.f[1] * scale;
		skyvertices3d[2].z = sp5fc.f[2] * scale;
		skyvertices3d[0].s = sp644.f[0] * 0.1f;
		skyvertices3d[0].t = sp644.f[2] * 0.1f + g_SkyCloudOffset;
		skyvertices3d[1].s = sp614.f[0] * 0.1f;
		skyvertices3d[1].t = sp614.f[2] * 0.1f + g_SkyCloudOffset;
		skyvertices3d[2].s = sp5fc.f[0] * 0.1f;
		skyvertices3d[2].t = sp5fc.f[2] * 0.1f + g_SkyCloudOffset;

		sky_choose_cloud_vtx_colour(&skyvertices3d[0], sp58c);
		sky_choose_cloud_vtx_colour(&skyvertices3d[1], sp57c);
		sky_choose_cloud_vtx_colour(&skyvertices3d[2], sp574);
		break;
	case CORNERSTATE_TL_TR_BL:
		numvertices = 5;
		skyvertices3d[0].x = sp62c.f[0] * scale;
		skyvertices3d[0].y = sp62c.f[1] * scale;
		skyvertices3d[0].z = sp62c.f[2] * scale;
		skyvertices3d[1].x = sp644.f[0] * scale;
		skyvertices3d[1].y = sp644.f[1] * scale;
		skyvertices3d[1].z = sp644.f[2] * scale;
		skyvertices3d[2].x = sp638.f[0] * scale;
		skyvertices3d[2].y = sp638.f[1] * scale;
		skyvertices3d[2].z = sp638.f[2] * scale;
		skyvertices3d[3].x = sp5f0.f[0] * scale;
		skyvertices3d[3].y = sp5f0.f[1] * scale;
		skyvertices3d[3].z = sp5f0.f[2] * scale;
		skyvertices3d[4].x = sp608.f[0] * scale;
		skyvertices3d[4].y = sp608.f[1] * scale;
		skyvertices3d[4].z = sp608.f[2] * scale;
		skyvertices3d[0].s = sp62c.f[0] * 0.1f;
		skyvertices3d[0].t = sp62c.f[2] * 0.1f + g_SkyCloudOffset;
		skyvertices3d[1].s = sp644.f[0] * 0.1f;
		skyvertices3d[1].t = sp644.f[2] * 0.1f + g_SkyCloudOffset;
		skyvertices3d[2].s = sp638.f[0] * 0.1f;
		skyvertices3d[2].t = sp638.f[2] * 0.1f + g_SkyCloudOffset;
		skyvertices3d[3].s = sp5f0.f[0] * 0.1f;
		skyvertices3d[3].t = sp5f0.f[2] * 0.1f + g_SkyCloudOffset;
		skyvertices3d[4].s = sp608.f[0] * 0.1f;
		skyvertices3d[4].t = sp608.f[2] * 0.1f + g_SkyCloudOffset;

		sky_choose_cloud_vtx_colour(&skyvertices3d[0], sp584);
		sky_choose_cloud_vtx_colour(&skyvertices3d[1], sp58c);
		sky_choose_cloud_vtx_colour(&skyvertices3d[2], sp588);
		sky_choose_cloud_vtx_colour(&skyvertices3d[3], sp570);
		sky_choose_cloud_vtx_colour(&skyvertices3d[4], sp578);
		break;
	case CORNERSTATE_TL_TR_BR:
		numvertices = 5;
		skyvertices3d[0].x = sp644.f[0] * scale;
		skyvertices3d[0].y = sp644.f[1] * scale;
		skyvertices3d[0].z = sp644.f[2] * scale;
		skyvertices3d[1].x = sp638.f[0] * scale;
		skyvertices3d[1].y = sp638.f[1] * scale;
		skyvertices3d[1].z = sp638.f[2] * scale;
		skyvertices3d[2].x = sp620.f[0] * scale;
		skyvertices3d[2].y = sp620.f[1] * scale;
		skyvertices3d[2].z = sp620.f[2] * scale;
		skyvertices3d[3].x = sp608.f[0] * scale;
		skyvertices3d[3].y = sp608.f[1] * scale;
		skyvertices3d[3].z = sp608.f[2] * scale;
		skyvertices3d[4].x = sp5fc.f[0] * scale;
		skyvertices3d[4].y = sp5fc.f[1] * scale;
		skyvertices3d[4].z = sp5fc.f[2] * scale;
		skyvertices3d[0].s = sp644.f[0] * 0.1f;
		skyvertices3d[0].t = sp644.f[2] * 0.1f + g_SkyCloudOffset;
		skyvertices3d[1].s = sp638.f[0] * 0.1f;
		skyvertices3d[1].t = sp638.f[2] * 0.1f + g_SkyCloudOffset;
		skyvertices3d[2].s = sp620.f[0] * 0.1f;
		skyvertices3d[2].t = sp620.f[2] * 0.1f + g_SkyCloudOffset;
		skyvertices3d[3].s = sp608.f[0] * 0.1f;
		skyvertices3d[3].t = sp608.f[2] * 0.1f + g_SkyCloudOffset;
		skyvertices3d[4].s = sp5fc.f[0] * 0.1f;
		skyvertices3d[4].t = sp5fc.f[2] * 0.1f + g_SkyCloudOffset;

		sky_choose_cloud_vtx_colour(&skyvertices3d[0], sp58c);
		sky_choose_cloud_vtx_colour(&skyvertices3d[1], sp588);
		sky_choose_cloud_vtx_colour(&skyvertices3d[2], sp580);
		sky_choose_cloud_vtx_colour(&skyvertices3d[3], sp578);
		sky_choose_cloud_vtx_colour(&skyvertices3d[4], sp574);
		break;
	case CORNERSTATE_TL_BL_BR:
		numvertices = 5;
		skyvertices3d[0].x = sp620.f[0] * scale;
		skyvertices3d[0].y = sp620.f[1] * scale;
		skyvertices3d[0].z = sp620.f[2] * scale;
		skyvertices3d[1].x = sp62c.f[0] * scale;
		skyvertices3d[1].y = sp62c.f[1] * scale;
		skyvertices3d[1].z = sp62c.f[2] * scale;
		skyvertices3d[2].x = sp644.f[0] * scale;
		skyvertices3d[2].y = sp644.f[1] * scale;
		skyvertices3d[2].z = sp644.f[2] * scale;
		skyvertices3d[3].x = sp614.f[0] * scale;
		skyvertices3d[3].y = sp614.f[1] * scale;
		skyvertices3d[3].z = sp614.f[2] * scale;
		skyvertices3d[4].x = sp5f0.f[0] * scale;
		skyvertices3d[4].y = sp5f0.f[1] * scale;
		skyvertices3d[4].z = sp5f0.f[2] * scale;
		skyvertices3d[0].s = sp620.f[0] * 0.1f;
		skyvertices3d[0].t = sp620.f[2] * 0.1f + g_SkyCloudOffset;
		skyvertices3d[1].s = sp62c.f[0] * 0.1f;
		skyvertices3d[1].t = sp62c.f[2] * 0.1f + g_SkyCloudOffset;
		skyvertices3d[2].s = sp644.f[0] * 0.1f;
		skyvertices3d[2].t = sp644.f[2] * 0.1f + g_SkyCloudOffset;
		skyvertices3d[3].s = sp614.f[0] * 0.1f;
		skyvertices3d[3].t = sp614.f[2] * 0.1f + g_SkyCloudOffset;
		skyvertices3d[4].s = sp5f0.f[0] * 0.1f;
		skyvertices3d[4].t = sp5f0.f[2] * 0.1f + g_SkyCloudOffset;

		sky_choose_cloud_vtx_colour(&skyvertices3d[0], sp580);
		sky_choose_cloud_vtx_colour(&skyvertices3d[1], sp584);
		sky_choose_cloud_vtx_colour(&skyvertices3d[2], sp58c);
		sky_choose_cloud_vtx_colour(&skyvertices3d[3], sp57c);
		sky_choose_cloud_vtx_colour(&skyvertices3d[4], sp570);
		break;
	case CORNERSTATE_TR_BL_BR:
		numvertices = 5;
		skyvertices3d[0].x = sp638.f[0] * scale;
		skyvertices3d[0].y = sp638.f[1] * scale;
		skyvertices3d[0].z = sp638.f[2] * scale;
		skyvertices3d[1].x = sp620.f[0] * scale;
		skyvertices3d[1].y = sp620.f[1] * scale;
		skyvertices3d[1].z = sp620.f[2] * scale;
		skyvertices3d[2].x = sp62c.f[0] * scale;
		skyvertices3d[2].y = sp62c.f[1] * scale;
		skyvertices3d[2].z = sp62c.f[2] * scale;
		skyvertices3d[3].x = sp5fc.f[0] * scale;
		skyvertices3d[3].y = sp5fc.f[1] * scale;
		skyvertices3d[3].z = sp5fc.f[2] * scale;
		skyvertices3d[4].x = sp614.f[0] * scale;
		skyvertices3d[4].y = sp614.f[1] * scale;
		skyvertices3d[4].z = sp614.f[2] * scale;
		skyvertices3d[0].s = sp638.f[0] * 0.1f;
		skyvertices3d[0].t = sp638.f[2] * 0.1f + g_SkyCloudOffset;
		skyvertices3d[1].s = sp620.f[0] * 0.1f;
		skyvertices3d[1].t = sp620.f[2] * 0.1f + g_SkyCloudOffset;
		skyvertices3d[2].s = sp62c.f[0] * 0.1f;
		skyvertices3d[2].t = sp62c.f[2] * 0.1f + g_SkyCloudOffset;
		skyvertices3d[3].s = sp5fc.f[0] * 0.1f;
		skyvertices3d[3].t = sp5fc.f[2] * 0.1f + g_SkyCloudOffset;
		skyvertices3d[4].s = sp614.f[0] * 0.1f;
		skyvertices3d[4].t = sp614.f[2] * 0.1f + g_SkyCloudOffset;

		sky_choose_cloud_vtx_colour(&skyvertices3d[0], sp588);
		sky_choose_cloud_vtx_colour(&skyvertices3d[1], sp580);
		sky_choose_cloud_vtx_colour(&skyvertices3d[2], sp584);
		sky_choose_cloud_vtx_colour(&skyvertices3d[3], sp574);
		sky_choose_cloud_vtx_colour(&skyvertices3d[4], sp57c);
		break;
	default:
		return gdl;
	}

	gDPPipeSync(gdl++);

	tex_select(&gdl, &g_TexSkyWaterConfigs[env->clouds_type], 1, 0, 2, 1, NULL);

	gDPSetEnvColor(gdl++, env->sky_r, env->sky_g, env->sky_b, 0xff);
	gDPSetCombineLERP(gdl++,
			SHADE, ENVIRONMENT, TEXEL0, ENVIRONMENT, 0, 0, 0, SHADE,
			SHADE, ENVIRONMENT, TEXEL0, ENVIRONMENT, 0, 0, 0, SHADE);

	// @recomp: drawn by sky_draw_polygon.
	gdl = sky_draw_polygon(gdl, skyvertices3d, numvertices, scale, SKY_CLOUDS);

	return gdl;
}
