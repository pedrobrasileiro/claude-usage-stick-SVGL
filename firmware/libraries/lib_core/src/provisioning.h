#pragma once
#include <lvgl.h>

// ============================================================
// Portal de configuração unificado (WiFi + token + PIN via
// navegador). Reaproveita g_web/stop_web/ensure_mdns de
// web_server.cpp — só um WebServer* vivo por vez, trocado
// conforme a tela (ver render_state()).
// ============================================================

extern lv_obj_t *g_provMsg;   // zerado em render_state() na troca de tela

void ui_provision();
