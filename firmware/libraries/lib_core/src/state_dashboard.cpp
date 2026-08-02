#include "state_dashboard.h"

DashUI g_ui;
int g_curTile = 0;

lv_point_precise_t g_trPts[HIST_MAX];
lv_point_precise_t g_trProjPts[2];

Mascot g_masc[NMODELS];
int g_mascN = 0;
lv_point_precise_t g_mXPts[NMODELS][4][2];

lv_obj_t *g_hdrStatus = nullptr;
lv_obj_t *g_pinDots = nullptr, *g_pinMsg = nullptr, *g_pinLockBar = nullptr;
