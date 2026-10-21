// RecompFrontend's UI API (recompui/src/api/ui_api_events.cpp) includes this file from the game's patches/, with
// patch_helpers.h and recompui_event_structs.h: rush-2049-recomp's, unchanged. No patch here uses them yet.

#ifndef __UI_FUNCS_INTERNAL_H__
#define __UI_FUNCS_INTERNAL_H__

#include "patch_helpers.h"
#include "recompui_event_structs.h"

DECLARE_FUNC(void, recomp_run_ui_callbacks);

#endif
