#pragma once
#include <lvgl.h>
#include "state_dashboard.h"

// ============================================================
// Dashboard principal (tileview de 4 páginas), atualização de
// valores e overlay de "momentos" de limiar. Maior e mais
// acoplado dos módulos — depende de state_app/state_dashboard,
// ui_helpers e history.
// ============================================================

void ui_main();
void refresh_ui_values();   // preenche valores após fetch, sem rebuild de tela
void dash_tick();           // contadores/relógios (chamado a cada 1s pelo loop())
void set_hdr_status();
void update_tok_row();      // linha de tokens/sessão (chamada também por web_server.cpp)

// ---- Overlay de "momentos" (25/50/70/100%) ----
struct MomentUI {
  lv_obj_t *scrim, *box, *img, *pct, *seg[NSEG];
  lv_obj_t *lid[2], *drop[2], *ring, *xline[4];
  int win, thr, fromPct;
  int boxY;
  uint32_t t0;
};
extern MomentUI g_mo;
extern int g_pendWin, g_pendThr;   // momento aguardando exibição (checado pelo loop())

void check_thresholds();   // detecta cruzamento de limiar após cada fetch
void show_moment(int win, int thr);
void moment_tick();        // anima o overlay ativo (chamado a cada frame do loop())
void moment_close();
