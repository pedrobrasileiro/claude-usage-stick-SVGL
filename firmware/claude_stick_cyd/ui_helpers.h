#pragma once
#include <lvgl.h>
#include "state_app.h"

// ============================================================
// Helpers de UI (LVGL) e formatação — usados pelas telas touch
// e pelo dashboard. Sem lógica de rede/NVS.
// ============================================================

// ---- Paleta (escuro, minimalista; acento coral do Claude) ----
#define C_BG       0x0F0F12
#define C_SURFACE  0x1A1A20   // cards sem borda
#define C_SURFACE2 0x24242C   // teclas / botoes secundarios
#define C_TRACK    0x26262E   // trilho de barras
#define C_GRID     0x232329   // linhas de grade dentro de cards
#define C_BORDER   0x30303A   // hairlines raras
#define C_TEXT     0xF2F0EC
#define C_MUTED    0x8C8C98
#define C_FAINT    0x5C5C68
#define C_ACCENT   0xD97757   // coral Claude
#define C_OK       0x4ADE80
#define C_WARN     0xFBBF24
#define C_BAD      0xF87171

lv_obj_t *mklabel(lv_obj_t *p, const char *txt, const lv_font_t *font, uint32_t color);
void no_box(lv_obj_t *o);
// Botão pílula com label centralizado; user_data leva o State alvo (nav_cb).
lv_obj_t *mkbtn(lv_obj_t *p, const char *txt, const lv_font_t *font, uint32_t bg, uint32_t fg);
// Navegação genérica — user_data leva o State alvo.
void nav_cb(lv_event_t *e);
uint32_t pct_color(float p);
// gradiente contínuo verde -> âmbar -> vermelho conforme o uso cresce
lv_color_t grad_color(float p);
// acende os segmentos do medidor; acesos ganham a cor do gradiente,
// apagados ficam no trilho escuro
void set_meter(lv_obj_t **seg, float pct);
// "reseta em 1h 23m" / "2d 4h" / "agora" / "--" (relógio não sincronizado)
void fmt_eta(uint32_t epoch, char *out, int sz);
void fmt_clock(uint32_t epoch, char *out, int sz);
void fmt_hm(uint32_t epoch, char *out, int sz);
// 1234 -> "1.2k", 2345678 -> "2.3M"
void fmt_tok(long long v, char *out, int sz);

uint32_t status_color(const char *s);
const char *overall_label(const char *s);
// label posicionado vazio (preenchido em refresh_ui_values/dash_tick)
lv_obj_t *tlabel(lv_obj_t *p, const lv_font_t *f, uint32_t c, int x, int y);
lv_obj_t *tstatic(lv_obj_t *p, const char *txt, const lv_font_t *f, uint32_t c, int x, int y);
void tile_setup(lv_obj_t *t);
// card moderno: superfície arredondada SEM borda (estrutura por cor, não caixa)
lv_obj_t *card(lv_obj_t *p, int x, int y, int w, int h);
// chip com fundo tintado + texto na cor (mais leve que chip sólido)
lv_obj_t *mkchip(lv_obj_t *p, int x, int y);
void set_chip(lv_obj_t *o, const char *txt, uint32_t col);
// peça retangular arredondada do mascote
lv_obj_t *rrect(lv_obj_t *p, int x, int y, int w, int h, int r, uint32_t col);
// humor do modelo: sonda real (HTTP) + incidentes do status.claude.com
int model_mood(int i);
// adereço pixel que identifica cada modelo (flutuando sobre a cabeça)
void build_accessory(lv_obj_t *c, int model);
// Mascote da pagina de modelos: Clawd oficial + humor + adereço.
void build_model_mascot(lv_obj_t *parent, int cx, int i);
void model_chip(int i, char *out, size_t sz, uint32_t *col);
