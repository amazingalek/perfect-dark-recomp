// modelasm00018680, which poses a model's nodes from its animation, as the decompilation's C (src/lib/modelasm_c.c).
//
// The game's own is hand-written assembly (src/lib/modelasm.s) whose pieces branch into each other: N64Recomp can't
// recompile it as functions, so us.toml stubs every piece and this replaces the one entry the rest of the game calls
// (model_set_matrices). The C is the decompilation's equivalent of it, as its non-matching builds use; every name it
// defines is given a prefix here, so none is taken for one of the game's functions.

#include "patches.h"

#define modelasm00018680 pd_c_modelasm00018680
#define modelasm_iterate_things1 pd_c_modelasm_iterate_things1
#define modelasm_iterate_things2 pd_c_modelasm_iterate_things2
#define modelasm_read_frame_data pd_c_modelasm_read_frame_data
#define modelasm_get_node_rw_data pd_c_modelasm_get_node_rw_data
#define modelasm_math_pain1 pd_c_modelasm_math_pain1
#define modelasm_math_pain2 pd_c_modelasm_math_pain2
#define modelasm_math_pain3 pd_c_modelasm_math_pain3
#define modelasm_math_pain4 pd_c_modelasm_math_pain4
#define modelasm_prepare_rot_mtx180 pd_c_modelasm_prepare_rot_mtx180
#define modelasm_prepare_rot_mtx360 pd_c_modelasm_prepare_rot_mtx360
#define modelasm_mtx_multiply pd_c_modelasm_mtx_multiply
#define modelasm_find_node_mtx pd_c_modelasm_find_node_mtx
#define modelasm_acos_or_asin pd_c_modelasm_acos_or_asin
#define model_get_node_rw_data pd_c_model_get_node_rw_data
#define cosf pd_c_cosf
#define sinf pd_c_sinf
#include "../lib/perfect_dark/src/lib/modelasm_c.c"
#undef modelasm00018680

RECOMP_PATCH bool modelasm00018680(struct modelrenderdata *renderdata, struct model *model)
{
	return pd_c_modelasm00018680(renderdata, model);
}
