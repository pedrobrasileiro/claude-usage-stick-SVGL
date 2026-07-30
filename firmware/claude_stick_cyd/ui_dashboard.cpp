#include <Arduino.h>
#include <math.h>
#include <time.h>
#include "ui_dashboard.h"
#include "state_app.h"
#include "ui_helpers.h"
#include "ui_screens.h"
#include "web_server.h"
#include "history.h"
#include "logo_assets.h"
#include "config.h"

// ============================================================
// Builders dos 4 tiles
// ============================================================
// Tile 0 — AGORA: janelas 5h/semana com % grande, medidor segmentado
// (verde -> vermelho conforme o uso) e countdown grande.
static void build_win_card(lv_obj_t *t, int x, const char *title,
                           lv_obj_t **pct, lv_obj_t **seg, lv_obj_t **at, lv_obj_t **cd) {
  lv_obj_t *c = card(t, x, 2, 152, 158);
  tstatic(c, title, &lv_font_montserrat_12, C_MUTED, 0, 0);
  *pct = tlabel(c, &lv_font_montserrat_28, C_OK, 0, 14);
  for (int i = 0; i < NSEG; i++)                    // medidor: 18 segmentos
    seg[i] = rrect(c, i * 6, 54, 4, 12, 1, C_TRACK);
  *at = tlabel(c, &lv_font_montserrat_12, C_FAINT, 0, 72);
  *cd = tlabel(c, &lv_font_montserrat_22, C_TEXT, 0, 88);
}
static void build_tile_agora(lv_obj_t *t) {
  build_win_card(t, 4,   TRS("5 HORAS", "5 HOURS"), &g_ui.agPct5, g_ui.seg5, &g_ui.agAt5, &g_ui.agCd5);
  build_win_card(t, 164, TRS("SEMANA", "WEEK"),     &g_ui.agPct7, g_ui.seg7, &g_ui.agAt7, &g_ui.agCd7);
  g_ui.agChip = mkchip(t, 4, 166);
  g_ui.agTok = tlabel(t, &lv_font_montserrat_12, C_MUTED, 4, 178);
  lv_obj_set_width(g_ui.agTok, 312);
  lv_obj_set_style_text_align(g_ui.agTok, LV_TEXT_ALIGN_LEFT, 0);
}
// Tile 1 — MODELOS: Clawd oficial por modelo (humor animado) + sonda + incidentes.
static void build_tile_models(lv_obj_t *t) {
  for (int i = 0; i < NMODELS; i++) {
    build_model_mascot(t, MODEL_CENTERS[i], i);
    lv_obj_t *n = mklabel(t, g_models[i].name, &lv_font_montserrat_14,
                          model_mood(i) == 1 ? C_TEXT : C_MUTED);
    lv_obj_set_width(n, 74);
    lv_obj_set_style_text_align(n, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(n, MODEL_CENTERS[i] - 37, 98);
    g_ui.mChip[i] = mkchip(t, 0, 122);
  }
  tstatic(t, TRS("sonda real na API \xE2\x80\xA2 1 modelo por ciclo",
                 "live API probe \xE2\x80\xA2 1 model per cycle"),
          &lv_font_montserrat_12, C_FAINT, 6, 148);
  g_ui.incident = tlabel(t, &lv_font_montserrat_14, C_MUTED, 6, 168);
  lv_obj_set_width(g_ui.incident, 308);
  lv_label_set_long_mode(g_ui.incident, LV_LABEL_LONG_WRAP);
}
// Tile 2 — JANELA 5H: histórico + projeção pontilhada até esgotar.
#define TR_X0 6
#define TR_Y0 8
#define TR_W  296
#define TR_H  80
static int tr_x(uint32_t tt, uint32_t ws, uint32_t we) {
  if (we <= ws) return TR_X0;
  long long v = (long long)(tt - ws) * TR_W / (long long)(we - ws);
  if (v < 0) v = 0; if (v > TR_W) v = TR_W;
  return TR_X0 + (int)v;
}
static int tr_y(float p) {
  if (p < 0) p = 0; if (p > 100) p = 100;
  return TR_Y0 + TR_H - (int)(p * TR_H / 100.0f);
}
static void build_tile_trend(lv_obj_t *t) {
  tstatic(t, TRS("Janela de 5h", "5-hour window"), &lv_font_montserrat_16, C_TEXT, 6, 2);

  lv_obj_t *c = card(t, 6, 24, 308, 130);
  lv_obj_set_style_pad_all(c, 0, 0);

  // grade: 25/50/75%
  for (int i = 1; i <= 3; i++) {
    lv_obj_t *g = rrect(c, TR_X0, tr_y(i * 25.0f), TR_W, 1, 0, C_GRID);
    (void)g;
  }
  tstatic(c, "100", &lv_font_montserrat_12, C_FAINT, TR_X0 + TR_W - 24, TR_Y0 - 6);
  tstatic(c, "0",   &lv_font_montserrat_12, C_FAINT, TR_X0 + TR_W - 10, TR_Y0 + TR_H - 14);

  // histórico (linha sólida coral)
  g_ui.trHist = lv_line_create(c);
  lv_obj_set_pos(g_ui.trHist, 0, 0);
  lv_obj_set_style_line_width(g_ui.trHist, 3, 0);
  lv_obj_set_style_line_color(g_ui.trHist, lv_color_hex(C_ACCENT), 0);
  lv_obj_set_style_line_rounded(g_ui.trHist, true, 0);

  // projeção (pontilhada)
  g_ui.trProj = lv_line_create(c);
  lv_obj_set_pos(g_ui.trProj, 0, 0);
  lv_obj_set_style_line_width(g_ui.trProj, 2, 0);
  lv_obj_set_style_line_color(g_ui.trProj, lv_color_hex(C_ACCENT), 0);
  lv_obj_set_style_line_opa(g_ui.trProj, 170, 0);
  lv_obj_set_style_line_dash_width(g_ui.trProj, 6, 0);
  lv_obj_set_style_line_dash_gap(g_ui.trProj, 6, 0);

  // marcador do ponto atual
  g_ui.trDot = rrect(c, 0, 0, 8, 8, 4, C_TEXT);
  lv_obj_add_flag(g_ui.trDot, LV_OBJ_FLAG_HIDDEN);

  // horários do eixo X (início da janela / reset)
  g_ui.trT0 = tlabel(c, &lv_font_montserrat_12, C_FAINT, TR_X0, TR_Y0 + TR_H + 8);
  g_ui.trT1 = tlabel(c, &lv_font_montserrat_12, C_FAINT, TR_X0 + TR_W - 40, TR_Y0 + TR_H + 8);

  g_ui.trCap = tlabel(t, &lv_font_montserrat_14, C_MUTED, 6, 160);
  lv_obj_set_width(g_ui.trCap, 308);
  lv_label_set_long_mode(g_ui.trCap, LV_LABEL_LONG_WRAP);
}
// Tile 3 — RITMO: heatmap por hora com filtro de período.
static void heat_redraw();
static void heat_btn_style() {
  const char *names[4] = {TRS("Hoje", "Today"), "7d", "30d", TRS("Tudo", "All")};
  for (int i = 0; i < 4; i++) {
    if (!g_ui.heatBtn[i]) continue;
    bool on = (i == g_heatMode);
    lv_obj_set_style_bg_color(g_ui.heatBtn[i], lv_color_hex(on ? C_ACCENT : C_SURFACE2), 0);
    lv_obj_t *l = lv_obj_get_child(g_ui.heatBtn[i], 0);
    if (l) {
      lv_label_set_text(l, names[i]);
      lv_obj_set_style_text_color(l, lv_color_hex(on ? C_BG : C_MUTED), 0);
    }
  }
}
static void heat_btn_cb(lv_event_t *e) {
  int m = (int)(intptr_t)lv_event_get_user_data(e);
  if (m == g_heatMode) return;
  g_heatMode = m;
  g_prefs.putInt("heatm", m);
  heat_btn_style();
  heat_redraw();
}
static void build_tile_heat(lv_obj_t *t) {
  tstatic(t, TRS("Ritmo por hora", "Hourly rhythm"), &lv_font_montserrat_16, C_TEXT, 6, 2);
  for (int i = 0; i < 4; i++) {
    lv_obj_t *b = lv_button_create(t);
    lv_obj_set_size(b, 70, 26);
    lv_obj_set_pos(b, 6 + i * 74, 26);
    lv_obj_set_style_radius(b, 13, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_ext_click_area(b, 4);
    lv_obj_t *l = mklabel(b, "", &lv_font_montserrat_14, C_MUTED);
    lv_obj_center(l);
    lv_obj_add_event_cb(b, heat_btn_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    g_ui.heatBtn[i] = b;
  }
  heat_btn_style();
  for (int h = 0; h < 24; h++) {
    lv_obj_t *bar = lv_obj_create(t);
    lv_obj_set_size(bar, 8, 4);
    lv_obj_set_pos(bar, 6 + h * 12, 118);
    lv_obj_set_style_radius(bar, 2, 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(C_ACCENT), 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    g_ui.heat[h] = bar;
  }
  int ticks[5] = {0, 6, 12, 18, 23};
  for (int i = 0; i < 5; i++) {
    int h = ticks[i]; char s[4]; snprintf(s, sizeof(s), "%dh", h);
    lv_obj_t *l = mklabel(t, s, &lv_font_montserrat_12, C_MUTED);
    lv_obj_set_pos(l, 4 + h * 12, 122);
  }
  tstatic(t, TRS("quota da janela 5h queimada em cada hora local",
                 "5h-window quota burned per local hour"), &lv_font_montserrat_12, C_FAINT, 6, 142);
}

static void on_tile_changed(lv_event_t *e) {
  (void)e;
  if (!g_ui.tv) return;
  lv_obj_t *act = lv_tileview_get_tile_active(g_ui.tv);
  for (int i = 0; i < NTILES; i++) {
    if (!g_ui.dots[i]) continue;
    bool on = (g_ui.tile[i] == act);
    if (on) g_curTile = i;
    lv_obj_set_style_bg_color(g_ui.dots[i], lv_color_hex(on ? C_ACCENT : C_BORDER), 0);
    lv_obj_set_width(g_ui.dots[i], on ? 18 : 8);
  }
}

// ============================================================
// Atualização de valores
// ============================================================
void update_tok_row() {
  if (!g_ui.agTok) return;
  if (g_tok.atMs == 0 || millis() - g_tok.atMs > TOK_FRESH_MS) {
    lv_label_set_text(g_ui.agTok, "");
    return;
  }
  char a[16], b[16], s[96];
  fmt_tok(g_tok.tin, a, sizeof(a));
  fmt_tok(g_tok.tout, b, sizeof(b));
  snprintf(s, sizeof(s), TRS("tokens na janela: %s entrada \xE2\x80\xA2 %s saida",
                             "window tokens: %s in \xE2\x80\xA2 %s out"), a, b);
  lv_label_set_text(g_ui.agTok, s);
}

// Contadores/relógios (1s) — separado dos valores de fetch.
void dash_tick() {
  if (g_state != ST_MAIN || !g_ui.agCd5) return;
  char e[32], c[24], b[64];
  fmt_eta(g_usage.h5ResetEpoch, e, sizeof(e));
  lv_label_set_text(g_ui.agCd5, e);
  fmt_clock(g_usage.h5ResetEpoch, c, sizeof(c));
  snprintf(b, sizeof(b), TRS("RESETA EM \xE2\x80\xA2 %s", "RESETS \xE2\x80\xA2 %s"), c);
  lv_label_set_text(g_ui.agAt5, b);

  fmt_eta(g_usage.d7ResetEpoch, e, sizeof(e));
  lv_label_set_text(g_ui.agCd7, e);
  fmt_clock(g_usage.d7ResetEpoch, c, sizeof(c));
  snprintf(b, sizeof(b), TRS("RESETA EM \xE2\x80\xA2 %s", "RESETS \xE2\x80\xA2 %s"), c);
  lv_label_set_text(g_ui.agAt7, b);

  set_hdr_status();
}

// Tendência da janela 5h: histórico + projeção pontilhada até esgotar.
static void trend_redraw() {
  if (!g_ui.trHist) return;
  time_t now = time(nullptr);
  uint32_t we = g_usage.h5ResetEpoch;
  bool clockOk = (now > 1000000000L) && we != 0;

  if (!clockOk) {
    lv_line_set_points(g_ui.trHist, g_trPts, 0);
    lv_line_set_points(g_ui.trProj, g_trProjPts, 0);
    lv_obj_add_flag(g_ui.trDot, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(g_ui.trCap, TRS("Aguardando dados da janela...", "Waiting for window data..."));
    lv_obj_set_style_text_color(g_ui.trCap, lv_color_hex(C_MUTED), 0);
    return;
  }
  uint32_t ws = we - 5 * 3600;

  // horários do eixo
  char t0[12], t1[12], b[112];
  fmt_hm(ws, t0, sizeof(t0)); fmt_hm(we, t1, sizeof(t1));
  lv_label_set_text(g_ui.trT0, t0);
  lv_label_set_text(g_ui.trT1, t1);

  // pontos do histórico dentro da janela
  int n = 0;
  for (int i = 0; i < g_histN && n < HIST_MAX; i++) {
    Sample s = g_hist[hist_idx(i)];
    if (s.t == 0 || s.t < ws || s.t > (uint32_t)now) continue;
    g_trPts[n].x = tr_x(s.t, ws, we);
    g_trPts[n].y = tr_y(s.h5);
    n++;
  }
  // ponto atual (leitura mais recente)
  uint32_t nowClamped = ((uint32_t)now > we) ? we : (uint32_t)now;
  if (n < HIST_MAX) {
    g_trPts[n].x = tr_x(nowClamped, ws, we);
    g_trPts[n].y = tr_y(g_usage.h5);
    n++;
  }
  lv_line_set_points(g_ui.trHist, g_trPts, n);

  int cx = tr_x(nowClamped, ws, we), cy = tr_y(g_usage.h5);
  lv_obj_set_pos(g_ui.trDot, cx - 4, cy - 4);
  lv_obj_clear_flag(g_ui.trDot, LV_OBJ_FLAG_HIDDEN);

  if (n < 3) {
    lv_line_set_points(g_ui.trProj, g_trProjPts, 0);
    lv_label_set_text(g_ui.trCap, TRS("Coletando dados... (~alguns minutos)",
                                      "Collecting data... (~a few minutes)"));
    lv_obj_set_style_text_color(g_ui.trCap, lv_color_hex(C_MUTED), 0);
    return;
  }

  // taxa: janela dos últimos 45 min
  float rate = 0;                    // %/min
  {
    Sample first = {0, 0, 0};
    for (int i = 0; i < g_histN; i++) {
      Sample s = g_hist[hist_idx(i)];
      if (s.t == 0 || s.t < ws) continue;
      if (s.t >= (uint32_t)now - 2700) { first = s; break; }
    }
    if (first.t != 0 && (uint32_t)now > first.t + 300) {
      float dt = ((uint32_t)now - first.t) / 60.0f;
      rate = (g_usage.h5 - first.h5) / dt;
    }
  }

  char e[32];
  if (g_usage.h5 >= 99.5f) {
    lv_line_set_points(g_ui.trProj, g_trProjPts, 0);
    fmt_eta(we, e, sizeof(e));
    snprintf(b, sizeof(b), TRS("Janela esgotada \xE2\x80\xA2 reseta em %s",
                               "Window exhausted \xE2\x80\xA2 resets in %s"), e);
    lv_label_set_text(g_ui.trCap, b);
    lv_obj_set_style_text_color(g_ui.trCap, lv_color_hex(C_BAD), 0);
  } else if (rate > 0.02f) {
    float minsLeft = (100.0f - g_usage.h5) / rate;
    uint32_t etaT = (uint32_t)now + (uint32_t)(minsLeft * 60);
    g_trProjPts[0].x = cx; g_trProjPts[0].y = cy;
    if (etaT <= we) {
      g_trProjPts[1].x = tr_x(etaT, ws, we);
      g_trProjPts[1].y = tr_y(100);
      char hm[12]; fmt_hm(etaT, hm, sizeof(hm));
      snprintf(b, sizeof(b), TRS("No ritmo atual, esgota as %s (em %dh%02dm)",
                                 "At this pace, runs out at %s (in %dh%02dm)"),
               hm, (int)minsLeft / 60, (int)minsLeft % 60);
      lv_label_set_text(g_ui.trCap, b);
      lv_obj_set_style_text_color(g_ui.trCap, lv_color_hex(minsLeft < 60 ? C_BAD : C_WARN), 0);
    } else {
      float endPct = g_usage.h5 + rate * ((we - (uint32_t)now) / 60.0f);
      g_trProjPts[1].x = tr_x(we, ws, we);
      g_trProjPts[1].y = tr_y(endPct);
      snprintf(b, sizeof(b), TRS("No ritmo atual, NAO esgota antes do reset (~%d%%)",
                                 "At this pace, does NOT run out before reset (~%d%%)"),
               (int)(endPct + 0.5f));
      lv_label_set_text(g_ui.trCap, b);
      lv_obj_set_style_text_color(g_ui.trCap, lv_color_hex(C_OK), 0);
    }
    lv_line_set_points(g_ui.trProj, g_trProjPts, 2);
  } else {
    lv_line_set_points(g_ui.trProj, g_trProjPts, 0);
    lv_label_set_text(g_ui.trCap, TRS("Uso estavel \xE2\x80\xA2 sem risco no momento",
                                      "Stable usage \xE2\x80\xA2 no risk right now"));
    lv_obj_set_style_text_color(g_ui.trCap, lv_color_hex(C_OK), 0);
  }
}

static void heat_redraw() {
  if (!g_ui.heat[0]) return;
  float data[24];
  heat_mode_data(g_heatMode, data);
  float mx = 1.0f;
  for (int h = 0; h < 24; h++) if (data[h] > mx) mx = data[h];
  int curHour = -1; time_t now = time(nullptr);
  if (now > 1000000000L) { struct tm tv; localtime_r(&now, &tv); curHour = tv.tm_hour; }
  for (int h = 0; h < 24; h++) {
    if (!g_ui.heat[h]) continue;
    float r = data[h] / mx; if (r < 0) r = 0; if (r > 1) r = 1;
    int hgt = 4 + (int)(r * 62);
    lv_obj_set_size(g_ui.heat[h], 8, hgt);
    lv_obj_set_y(g_ui.heat[h], 118 - hgt);
    lv_obj_set_style_bg_color(g_ui.heat[h], lv_color_hex(h == curHour ? C_TEXT : C_ACCENT), 0);
    lv_obj_set_style_bg_opa(g_ui.heat[h], (lv_opa_t)(70 + (int)(r * 185)), 0);
  }
}

// ============================================================
// Momentos — animações de limiar (25/50/70/100% nas janelas 5h e semanal)
// Overlay em lv_layer_top: Clawd XL com "humor" por nível + contador de %
// + medidor acendendo. Dispara ao cruzar o limiar; toque dispensa.
// Animação 100% procedural (moment_tick no loop), sem lv_anim pendurado.
// ============================================================
static const uint8_t THR[4] = {25, 50, 70, 100};
MomentUI g_mo = {};
static uint32_t g_momentUntil = 0;
int g_pendWin = -1, g_pendThr = 0;      // momento aguardando exibição
static uint8_t g_thrFired[2] = {0, 0};         // bits já disparados por janela
static float g_thrPrev[2] = {-1, -1};
static bool g_thrBase = false;
static lv_point_precise_t g_moXPts[4][2];      // olhos em X (KO)

// Detecta cruzamento de limiar após cada fetch. Baseline no 1º fetch (não
// dispara pelo que já estava acima); zera quando a janela reseta (queda >15pp).
void check_thresholds() {
  float c[2] = {g_usage.h5, g_usage.d7};
  for (int w = 0; w < 2; w++) {
    if (!g_thrBase || (g_thrPrev[w] - c[w]) > 15.0f) {
      g_thrFired[w] = 0;
      for (int i = 0; i < 4; i++) if (c[w] >= THR[i]) g_thrFired[w] |= 1 << i;
    } else {
      int hit = -1;
      for (int i = 0; i < 4; i++)
        if (c[w] >= THR[i] && !(g_thrFired[w] & (1 << i))) { g_thrFired[w] |= 1 << i; hit = i; }
      if (hit >= 0) { g_pendWin = w; g_pendThr = THR[hit]; }
    }
    g_thrPrev[w] = c[w];
  }
  g_thrBase = true;
}

void moment_close() {
  if (!g_mo.scrim) return;
  lv_obj_delete(g_mo.scrim);
  memset(&g_mo, 0, sizeof(g_mo));
}
static void moment_close_cb(lv_event_t *e) { (void)e; moment_close(); }

void show_moment(int win, int thr) {
  moment_close();
  g_mo.win = win; g_mo.thr = thr;
  g_mo.fromPct = (thr == 25) ? 0 : (thr == 50) ? 25 : (thr == 70) ? 50 : 70;
  g_mo.t0 = millis();
  g_momentUntil = g_mo.t0 + 4600;

  lv_obj_t *s = lv_obj_create(lv_layer_top());
  g_mo.scrim = s;
  lv_obj_set_pos(s, 0, 0); lv_obj_set_size(s, SCREEN_WIDTH, SCREEN_HEIGHT);
  lv_obj_set_style_bg_color(s, lv_color_hex(0x0D0D10), 0);
  lv_obj_set_style_bg_opa(s, 248, 0);
  lv_obj_set_style_border_width(s, 0, 0);
  lv_obj_set_style_radius(s, 0, 0);
  lv_obj_set_style_pad_all(s, 0, 0);
  lv_obj_clear_flag(s, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(s, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(s, moment_close_cb, LV_EVENT_CLICKED, NULL);

  // anel de alerta (só no 100%; pisca no tick)
  if (thr == 100) {
    g_mo.ring = lv_obj_create(s);
    lv_obj_set_pos(g_mo.ring, 4, 4); lv_obj_set_size(g_mo.ring, SCREEN_WIDTH - 8, SCREEN_HEIGHT - 8);
    lv_obj_set_style_bg_opa(g_mo.ring, 0, 0);
    lv_obj_set_style_radius(g_mo.ring, 14, 0);
    lv_obj_set_style_border_width(g_mo.ring, 4, 0);
    lv_obj_set_style_border_color(g_mo.ring, lv_color_hex(C_BAD), 0);
    lv_obj_clear_flag(g_mo.ring, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(g_mo.ring, LV_OBJ_FLAG_CLICKABLE);
  }

  // caixa do Clawd (a caixa inteira anima: bounce / shake / queda de entrada)
  // Em 320px de largura o mascote+texto não cabem lado a lado (original era
  // 480px) — mascote centralizado no topo, texto empilhado embaixo.
  g_mo.boxY = 46;
  lv_obj_t *bx = lv_obj_create(s);
  g_mo.box = bx;
  lv_obj_set_pos(bx, (SCREEN_WIDTH - 176) / 2, g_mo.boxY - 40);
  lv_obj_set_size(bx, 176, 116);
  lv_obj_set_style_bg_opa(bx, 0, 0);
  lv_obj_set_style_border_width(bx, 0, 0);
  lv_obj_set_style_pad_all(bx, 0, 0);
  lv_obj_clear_flag(bx, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(bx, LV_OBJ_FLAG_CLICKABLE);

  g_mo.img = lv_image_create(bx);
  lv_image_set_src(g_mo.img, &img_clawd_xl);
  lv_obj_set_pos(g_mo.img, 0, 0);

  // humores (sobre os buracos dos olhos do sprite)
  const int ex[2] = {CLAWD_XL_EYE0_X, CLAWD_XL_EYE1_X};
  const int ey = CLAWD_XL_EYE0_Y, ew = CLAWD_XL_EYE0_W, eh = CLAWD_XL_EYE0_H;
  if (thr == 50) {
    // focado: pálpebras cobrindo metade dos olhos + 1 gota de suor
    for (int i = 0; i < 2; i++)
      g_mo.lid[i] = rrect(bx, ex[i] - 1, ey - 1, ew + 2, eh / 2 + 2, 0, C_ACCENT);
    g_mo.drop[0] = rrect(bx, 150, 6, 8, 12, 4, 0x7DD3FC);
  } else if (thr == 70) {
    // preocupado: olhos arregalados + 2 gotas (a caixa treme no tick)
    for (int i = 0; i < 2; i++)
      g_mo.lid[i] = rrect(bx, ex[i] - 3, ey - 4, ew + 6, eh + 8, 2, 0x0D0D10);
    g_mo.drop[0] = rrect(bx, 150, 6, 8, 12, 4, 0x7DD3FC);
    g_mo.drop[1] = rrect(bx, 18, 12, 8, 12, 4, 0x7DD3FC);
  } else if (thr == 100) {
    // KO: corpo acinzentado + olhos em X
    lv_obj_set_style_image_recolor(g_mo.img, lv_color_hex(0x6A6A74), 0);
    lv_obj_set_style_image_recolor_opa(g_mo.img, 190, 0);
    for (int i = 0; i < 2; i++) {
      g_moXPts[i * 2][0]     = { (lv_value_precise_t)(ex[i] - 2), (lv_value_precise_t)(ey - 1) };
      g_moXPts[i * 2][1]     = { (lv_value_precise_t)(ex[i] + ew + 2), (lv_value_precise_t)(ey + eh + 1) };
      g_moXPts[i * 2 + 1][0] = { (lv_value_precise_t)(ex[i] + ew + 2), (lv_value_precise_t)(ey - 1) };
      g_moXPts[i * 2 + 1][1] = { (lv_value_precise_t)(ex[i] - 2), (lv_value_precise_t)(ey + eh + 1) };
      for (int k = 0; k < 2; k++) {
        lv_obj_t *ln = lv_line_create(bx);
        lv_line_set_points(ln, g_moXPts[i * 2 + k], 2);
        lv_obj_set_style_line_width(ln, 4, 0);
        lv_obj_set_style_line_color(ln, lv_color_hex(C_BAD), 0);
        lv_obj_set_style_line_rounded(ln, true, 0);
        g_mo.xline[i * 2 + k] = ln;
      }
    }
  }

  // texto empilhado abaixo do mascote (320px não cabe a coluna lado a lado
  // do original 480px) — tudo centralizado horizontalmente.
  lv_obj_t *win_l = mklabel(s, win == 0 ? TRS("JANELA DE 5 HORAS", "5-HOUR WINDOW")
                                        : TRS("JANELA SEMANAL", "WEEKLY WINDOW"),
                            &lv_font_montserrat_16, C_MUTED);
  lv_obj_set_pos(win_l, 0, 124);
  lv_obj_set_width(win_l, SCREEN_WIDTH);
  lv_obj_set_style_text_align(win_l, LV_TEXT_ALIGN_CENTER, 0);

  g_mo.pct = tlabel(s, &lv_font_montserrat_40, C_OK, 0, 148);
  lv_obj_set_width(g_mo.pct, SCREEN_WIDTH);
  lv_obj_set_style_text_align(g_mo.pct, LV_TEXT_ALIGN_CENTER, 0);

  const char *MSG[4] = {
    TRS("Comecando \xE2\x80\xA2 ritmo tranquilo",       "Just starting \xE2\x80\xA2 easy pace"),
    TRS("Metade da janela usada",                       "Half the window used"),
    TRS("Atencao \xE2\x80\xA2 uso alto",                "Heads up \xE2\x80\xA2 heavy usage"),
    TRS("Limite atingido \xE2\x80\xA2 aguarde o reset", "Limit reached \xE2\x80\xA2 wait for the reset"),
  };
  int mi = (thr == 25) ? 0 : (thr == 50) ? 1 : (thr == 70) ? 2 : 3;
  lv_obj_t *msg = mklabel(s, MSG[mi], &lv_font_montserrat_14, C_TEXT);
  lv_obj_set_pos(msg, 6, 196);
  lv_obj_set_width(msg, SCREEN_WIDTH - 12);
  lv_obj_set_style_text_align(msg, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_long_mode(msg, LV_LABEL_LONG_WRAP);

  char e[32], b[48];
  fmt_eta(win == 0 ? g_usage.h5ResetEpoch : g_usage.d7ResetEpoch, e, sizeof(e));
  snprintf(b, sizeof(b), TRS("reseta em %s", "resets in %s"), e);
  lv_obj_t *eta = mklabel(s, b, &lv_font_montserrat_12, C_FAINT);
  lv_obj_set_pos(eta, 0, 222);
  lv_obj_set_width(eta, SCREEN_WIDTH);
  lv_obj_set_style_text_align(eta, LV_TEXT_ALIGN_CENTER, 0);
}

// Anima o momento (chamado a cada frame do loop enquanto o overlay existe).
void moment_tick() {
  if (!g_mo.scrim) return;
  uint32_t t = millis() - g_mo.t0;

  // entrada: Clawd cai de cima com acomodação; depois o humor manda
  const int boxX = (SCREEN_WIDTH - 176) / 2;
  int y = g_mo.boxY, x = boxX;
  if (t < 450) {
    float p = t / 450.0f;
    y = g_mo.boxY - (int)((1.0f - p) * (1.0f - p) * 60.0f);
  } else if (g_mo.thr == 25 || g_mo.thr == 50) {
    y = g_mo.boxY + (int)(4.0f * sinf((t - 450) / 260.0f));           // bounce feliz
  } else if (g_mo.thr == 70) {
    x = boxX + (((t / 70) % 2) ? 2 : -2);                              // treme
  } else if (g_mo.thr == 100) {
    y = g_mo.boxY + 6;                                                 // caído
  }
  lv_obj_set_pos(g_mo.box, x, y);

  // contador de % (200ms..1100ms) + medidor acendendo em sequência
  float p = (t < 200) ? 0 : (t > 1100 ? 1.0f : (t - 200) / 900.0f);
  float v = g_mo.fromPct + (g_mo.thr - g_mo.fromPct) * p;
  char b[12]; snprintf(b, sizeof(b), "%d%%", (int)(v + 0.5f));
  lv_label_set_text(g_mo.pct, b);
  lv_obj_set_style_text_color(g_mo.pct, grad_color(v), 0);

  // gotas de suor caindo em ciclo
  for (int i = 0; i < 2; i++) {
    if (!g_mo.drop[i]) continue;
    uint32_t c = (t + i * 450) % 900;
    lv_obj_set_y(g_mo.drop[i], (i ? 12 : 6) + (int)(c * 34 / 900));
    lv_obj_set_style_bg_opa(g_mo.drop[i], (lv_opa_t)(255 - c * 190 / 900), 0);
  }
  // anel vermelho piscando (100%)
  if (g_mo.ring)
    lv_obj_set_style_border_opa(g_mo.ring, ((t / 350) % 2) ? 190 : 30, 0);

  if (millis() > g_momentUntil) moment_close();
}

// Preenche todos os valores vindos do fetch (sem rebuild de tela).
void refresh_ui_values() {
  if (g_state != ST_MAIN || !g_ui.agPct5) return;
  char b[96];

  // Agora: percentuais + medidores segmentados (cor desliza verde -> vermelho)
  snprintf(b, sizeof(b), "%d%%", (int)(g_usage.h5 + 0.5f)); lv_label_set_text(g_ui.agPct5, b);
  lv_obj_set_style_text_color(g_ui.agPct5, grad_color(g_usage.h5), 0);
  set_meter(g_ui.seg5, g_usage.h5);
  snprintf(b, sizeof(b), "%d%%", (int)(g_usage.d7 + 0.5f)); lv_label_set_text(g_ui.agPct7, b);
  lv_obj_set_style_text_color(g_ui.agPct7, grad_color(g_usage.d7), 0);
  set_meter(g_ui.seg7, g_usage.d7);

  set_chip(g_ui.agChip, overall_label(g_usage.statusOverall), status_color(g_usage.statusOverall));
  update_tok_row();

  // Modelos: chips de sonda + incidente
  for (int i = 0; i < NMODELS; i++) {
    if (!g_ui.mChip[i]) continue;
    char txt[16]; uint32_t col;
    model_chip(i, txt, sizeof(txt), &col);
    set_chip(g_ui.mChip[i], txt, col);
    lv_obj_update_layout(g_ui.mChip[i]);
    lv_obj_set_x(g_ui.mChip[i], MODEL_CENTERS[i] - lv_obj_get_width(g_ui.mChip[i]) / 2);
  }
  if (g_ui.incident) {
    bool any = !(g_status.haikuUp && g_status.sonnetUp && g_status.opusUp && g_status.fableUp);
    lv_label_set_text(g_ui.incident,
        !g_status.ok ? TRS("status.claude.com: sem dados", "status.claude.com: no data")
        : (any ? TRS("Incidente ativo \xE2\x80\xA2 veja status.claude.com",
                     "Active incident \xE2\x80\xA2 see status.claude.com")
               : TRS("status.claude.com: OK \xE2\x80\xA2 sem incidentes",
                     "status.claude.com: OK \xE2\x80\xA2 no incidents")));
    lv_obj_set_style_text_color(g_ui.incident, lv_color_hex(any ? C_WARN : C_FAINT), 0);
  }

  trend_redraw();
  heat_redraw();
  dash_tick();
}

// Atualiza o texto de status do cabeçalho (sem trocar de tela)
void set_hdr_status() {
  if (!g_hdrStatus) return;
  char buf[40]; uint32_t color, indColor = C_OK;
  if (g_refreshing)        { strcpy(buf, TRS("atualizando...", "updating..."));      color = g_provider->accentColor(); indColor = C_WARN; }
  else if (!g_lastFetchOk) { strcpy(buf, TRS("falha ao atualizar", "update failed")); color = C_BAD; indColor = C_BAD; }
  else {
    uint32_t s = (millis() - g_lastOkMs) / 1000;
    if (s < 60) snprintf(buf, sizeof(buf), TRS("atualizado ha %us", "updated %us ago"), (unsigned)s);
    else        snprintf(buf, sizeof(buf), TRS("atualizado ha %umin", "updated %um ago"), (unsigned)(s / 60));
    color = C_MUTED;
    int effPoll = g_provider->effectivePollSec(g_pollSec);
    if (s > (uint32_t)effPoll * 2) indColor = C_WARN;  // stale
    else indColor = C_OK;
  }
  lv_label_set_text(g_hdrStatus, buf);
  lv_obj_set_style_text_color(g_hdrStatus, lv_color_hex(color), 0);
  if (g_ui.errInd) lv_obj_set_style_bg_color(g_ui.errInd, lv_color_hex(indColor), 0);
}
// Botão de refresh: só pede; a busca acontece em background no loop()
void ui_main() {
  lv_obj_t *scr = lv_screen_active();
  lv_obj_set_style_bg_color(scr, lv_color_hex(C_BG), 0);

  start_data_web();

  // Header: logo + wordmark + engrenagem + indicador de erro
  lv_obj_t *hIcon = lv_image_create(scr);
  lv_image_set_src(hIcon, g_provider->logoSmall());
  lv_obj_set_pos(hIcon, 6, 4);
  lv_obj_t *hWord = lv_image_create(scr);
  lv_image_set_src(hWord, g_provider->logoWordmark());
  lv_obj_set_pos(hWord, 46, 6);

  lv_obj_t *gear = mkbtn(scr, LV_SYMBOL_SETTINGS, &lv_font_montserrat_14, C_SURFACE2, C_TEXT);
  lv_obj_set_size(gear, 26, 22);
  lv_obj_set_ext_click_area(gear, 10);
  lv_obj_align(gear, LV_ALIGN_TOP_RIGHT, -2, 3);
  lv_obj_add_event_cb(gear, gear_cb, LV_EVENT_CLICKED, NULL);

  // Indicador de status (circulo: verde=ok, amb=stale, verm=erro)
  g_ui.errInd = lv_obj_create(scr);
  lv_obj_set_size(g_ui.errInd, 8, 8);
  lv_obj_set_style_radius(g_ui.errInd, 4, 0);
  lv_obj_set_style_bg_color(g_ui.errInd, lv_color_hex(C_OK), 0);
  lv_obj_set_style_border_width(g_ui.errInd, 0, 0);
  lv_obj_clear_flag(g_ui.errInd, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_align(g_ui.errInd, LV_ALIGN_TOP_RIGHT, -30, 10);
  lv_obj_add_flag(g_ui.errInd, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(g_ui.errInd, [](lv_event_t *) { ui_error_detail(); }, LV_EVENT_CLICKED, NULL);

  g_hdrStatus = mklabel(scr, "", &lv_font_montserrat_12, C_MUTED);
  lv_obj_align(g_hdrStatus, LV_ALIGN_TOP_RIGHT, -42, 8);

  // Barra fina decrescente até o próximo refresh automático (só indicador).
  g_ui.refBar = lv_bar_create(scr);
  lv_obj_set_size(g_ui.refBar, SCREEN_WIDTH, 3);
  lv_obj_set_pos(g_ui.refBar, 0, 28);
  lv_bar_set_range(g_ui.refBar, 0, 1000);
  lv_bar_set_value(g_ui.refBar, 1000, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(g_ui.refBar, lv_color_hex(C_SURFACE), LV_PART_MAIN);
  lv_obj_set_style_bg_color(g_ui.refBar, lv_color_hex(g_provider->accentColor()), LV_PART_INDICATOR);
  lv_obj_set_style_radius(g_ui.refBar, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(g_ui.refBar, 0, LV_PART_INDICATOR);
  lv_obj_clear_flag(g_ui.refBar, LV_OBJ_FLAG_CLICKABLE);

  // Telas (BOOT curto avança; ver boot_button_poll())
  int nTiles = g_provider->modelCount() > 0 ? NTILES : 3;
  g_ui.tv = lv_tileview_create(scr);
  lv_obj_set_pos(g_ui.tv, 0, 32);
  lv_obj_set_size(g_ui.tv, SCREEN_WIDTH, SCREEN_HEIGHT - 32 - 14);
  lv_obj_set_style_bg_opa(g_ui.tv, 0, 0);
  lv_obj_set_style_border_width(g_ui.tv, 0, 0);
  lv_obj_set_scrollbar_mode(g_ui.tv, LV_SCROLLBAR_MODE_OFF);
  for (int i = 0; i < nTiles; i++) {
    g_ui.tile[i] = lv_tileview_add_tile(g_ui.tv, i, 0, LV_DIR_HOR);
    tile_setup(g_ui.tile[i]);
  }
  build_tile_agora(g_ui.tile[0]);
  if (nTiles == 4) {
    build_tile_models(g_ui.tile[1]);
    build_tile_trend(g_ui.tile[2]);
    build_tile_heat(g_ui.tile[3]);
  } else {
    build_tile_trend(g_ui.tile[1]);
    build_tile_heat(g_ui.tile[2]);
  }
  lv_obj_add_event_cb(g_ui.tv, on_tile_changed, LV_EVENT_VALUE_CHANGED, NULL);

  // Dots (objetos; o ativo vira pílula)
  for (int i = 0; i < nTiles; i++) {
    g_ui.dots[i] = lv_obj_create(scr);
    lv_obj_set_size(g_ui.dots[i], 8, 8);
    lv_obj_set_style_radius(g_ui.dots[i], 4, 0);
    lv_obj_set_style_bg_color(g_ui.dots[i], lv_color_hex(C_BORDER), 0);
    lv_obj_set_style_border_width(g_ui.dots[i], 0, 0);
    lv_obj_clear_flag(g_ui.dots[i], LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(g_ui.dots[i], LV_ALIGN_BOTTOM_MID, (int)((i - (nTiles - 1) / 2.0f) * 18), -4);
  }

  refresh_ui_values();
  on_tile_changed(NULL);
}
