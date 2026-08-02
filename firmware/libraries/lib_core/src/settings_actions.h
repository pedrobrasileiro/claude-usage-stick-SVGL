#pragma once
#include <lvgl.h>

// ============================================================
// Dispatcher de ações de ajuste — compartilhado entre a tela
// touch de settings (settings_action_cb) e o handler web
// (handleSettingsPost). Os ponteiros de label abaixo pertencem
// à tela touch (ui_screens.cpp); apply_setting_action() só os
// atualiza quando não são nulos (web não os usa).
// ============================================================

extern lv_obj_t *g_briLbl, *g_pollLbl, *g_tzLbl, *g_slideLbl, *g_heatLbl, *g_providerLbl;

void apply_setting_action(int act);
