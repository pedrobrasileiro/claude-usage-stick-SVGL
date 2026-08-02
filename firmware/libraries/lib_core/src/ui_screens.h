#pragma once
#include <lvgl.h>

// ============================================================
// Telas touch simples: PIN, settings (lista), sobre, loading e
// mensagem genérica. O dashboard (ui_main) fica em ui_dashboard.*
// por ser grande demais pra caber aqui.
// ============================================================

void ui_pin();
void open_settings();       // usado pelo botão físico BOOT (fallback) e pela engrenagem do dashboard
void gear_cb(lv_event_t *e);

void ui_message(const char *title, const char *sub, uint32_t color);
void ui_loading(const char *sub);

void ui_settings();
void ui_about();
void ui_error_detail();     // overlay com detalhes do ultimo erro do provider
