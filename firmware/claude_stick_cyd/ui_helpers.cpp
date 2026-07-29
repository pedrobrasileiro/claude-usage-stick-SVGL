#include <time.h>
#include <string.h>
#include "ui_helpers.h"
#include "state_dashboard.h"
#include "logo_assets.h"

lv_obj_t *mklabel(lv_obj_t *p, const char *txt, const lv_font_t *font, uint32_t color) {
  lv_obj_t *l = lv_label_create(p);
  lv_label_set_text(l, txt);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
  return l;
}
void no_box(lv_obj_t *o) {
  lv_obj_set_style_bg_opa(o, 0, 0);
  lv_obj_set_style_border_width(o, 0, 0);
  lv_obj_set_style_pad_all(o, 0, 0);
  lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
}
lv_obj_t *mkbtn(lv_obj_t *p, const char *txt, const lv_font_t *font,
                uint32_t bg, uint32_t fg) {
  lv_obj_t *b = lv_button_create(p);
  lv_obj_set_style_bg_color(b, lv_color_hex(bg), 0);
  lv_obj_set_style_radius(b, 10, 0);
  lv_obj_set_style_shadow_width(b, 0, 0);
  lv_obj_center(mklabel(b, txt, font, fg));
  return b;
}
void nav_cb(lv_event_t *e) {
  State s = (State)(intptr_t)lv_event_get_user_data(e);
  request_state(s);
}
uint32_t pct_color(float p) {
  if (p < 70.0f) return C_OK;
  if (p < 90.0f) return C_WARN;
  return C_BAD;
}
lv_color_t grad_color(float p) {
  if (p < 0) p = 0; if (p > 100) p = 100;
  if (p <= 50.0f)
    return lv_color_mix(lv_color_hex(C_WARN), lv_color_hex(C_OK), (uint8_t)(p * 255.0f / 50.0f));
  return lv_color_mix(lv_color_hex(C_BAD), lv_color_hex(C_WARN), (uint8_t)((p - 50.0f) * 255.0f / 50.0f));
}
void set_meter(lv_obj_t **seg, float pct) {
  int filled = (int)(pct / 100.0f * NSEG + 0.5f);
  if (pct > 0.5f && filled == 0) filled = 1;
  if (filled > NSEG) filled = NSEG;
  lv_color_t col = grad_color(pct);
  for (int i = 0; i < NSEG; i++) {
    if (!seg[i]) continue;
    lv_obj_set_style_bg_color(seg[i], (i < filled) ? col : lv_color_hex(C_TRACK), 0);
    lv_obj_set_style_bg_opa(seg[i], (i < filled) ? LV_OPA_COVER : 160, 0);
  }
}
void fmt_eta(uint32_t epoch, char *out, int sz) {
  time_t now = time(nullptr);
  if (now < 1000000000L || epoch == 0) { snprintf(out, sz, "--"); return; }
  long d = (long)epoch - (long)now;
  if (d <= 0) { snprintf(out, sz, "%s", TRS("agora", "now")); return; }
  int days = d / 86400; d %= 86400;
  int hrs  = d / 3600;  d %= 3600;
  int mins = d / 60;
  if (days > 0)      snprintf(out, sz, "%dd %dh", days, hrs);
  else if (hrs > 0)  snprintf(out, sz, "%dh %02dm", hrs, mins);
  else               snprintf(out, sz, "%dm", mins);
}
void fmt_clock(uint32_t epoch, char *out, int sz) {
  if (epoch == 0 || time(nullptr) < 1000000000L) { strlcpy(out, "--:--", sz); return; }
  time_t t = (time_t)epoch; struct tm tmv;
  localtime_r(&t, &tmv);
  strftime(out, sz, "%a %H:%M", &tmv);
}
void fmt_hm(uint32_t epoch, char *out, int sz) {
  if (epoch == 0 || time(nullptr) < 1000000000L) { strlcpy(out, "--:--", sz); return; }
  time_t t = (time_t)epoch; struct tm tmv;
  localtime_r(&t, &tmv);
  strftime(out, sz, "%H:%M", &tmv);
}
void fmt_tok(long long v, char *out, int sz) {
  if (v >= 100000000LL)     snprintf(out, sz, "%lldM", v / 1000000LL);
  else if (v >= 1000000LL)  snprintf(out, sz, "%.1fM", v / 1e6);
  else if (v >= 10000LL)    snprintf(out, sz, "%lldk", v / 1000LL);
  else if (v >= 1000LL)     snprintf(out, sz, "%.1fk", v / 1e3);
  else                      snprintf(out, sz, "%lld", v);
}

uint32_t status_color(const char *s) {
  if (!s || !s[0]) return C_MUTED;
  if (!strcmp(s, "rejected") || !strcmp(s, "rate_limited") || !strcmp(s, "exceeded")) return C_BAD;
  if (strstr(s, "warning")) return C_WARN;
  return C_OK;
}
const char *overall_label(const char *s) {
  if (!s || !s[0]) return "--";
  if (!strcmp(s, "allowed")) return "OK";
  if (strstr(s, "warning"))  return TRS("ATENCAO", "WARNING");
  if (!strcmp(s, "rejected")) return TRS("BLOQUEADO", "BLOCKED");
  return s;
}

lv_obj_t *tlabel(lv_obj_t *p, const lv_font_t *f, uint32_t c, int x, int y) {
  lv_obj_t *l = lv_label_create(p);
  lv_obj_set_style_text_font(l, f, 0);
  lv_obj_set_style_text_color(l, lv_color_hex(c), 0);
  lv_label_set_text(l, "");
  lv_obj_set_pos(l, x, y);
  return l;
}
lv_obj_t *tstatic(lv_obj_t *p, const char *txt, const lv_font_t *f, uint32_t c, int x, int y) {
  lv_obj_t *l = mklabel(p, txt, f, c);
  lv_obj_set_pos(l, x, y);
  return l;
}
void tile_setup(lv_obj_t *t) {
  lv_obj_set_style_bg_opa(t, 0, 0);
  lv_obj_set_style_border_width(t, 0, 0);
  lv_obj_set_style_pad_all(t, 0, 0);
  lv_obj_clear_flag(t, LV_OBJ_FLAG_SCROLLABLE);
}
lv_obj_t *card(lv_obj_t *p, int x, int y, int w, int h) {
  lv_obj_t *c = lv_obj_create(p);
  lv_obj_set_pos(c, x, y); lv_obj_set_size(c, w, h);
  lv_obj_set_style_bg_color(c, lv_color_hex(C_SURFACE), 0);
  lv_obj_set_style_border_width(c, 0, 0);
  lv_obj_set_style_radius(c, 18, 0);
  lv_obj_set_style_pad_all(c, 14, 0);
  lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);
  return c;
}
lv_obj_t *mkchip(lv_obj_t *p, int x, int y) {
  lv_obj_t *o = lv_obj_create(p);
  lv_obj_set_pos(o, x, y);
  lv_obj_set_size(o, LV_SIZE_CONTENT, 24);
  lv_obj_set_style_radius(o, 12, 0);
  lv_obj_set_style_pad_hor(o, 10, 0);
  lv_obj_set_style_pad_ver(o, 0, 0);
  lv_obj_set_style_border_width(o, 0, 0);
  lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t *l = lv_label_create(o);
  lv_obj_set_style_text_font(l, &lv_font_montserrat_12, 0);
  lv_label_set_text(l, ""); lv_obj_center(l);
  return o;
}
void set_chip(lv_obj_t *o, const char *txt, uint32_t col) {
  if (!o) return;
  lv_color_t c = lv_color_hex(col);
  lv_obj_set_style_bg_color(o, lv_color_mix(c, lv_color_hex(C_BG), 60), 0);
  lv_obj_t *l = lv_obj_get_child(o, 0);
  if (l) { lv_label_set_text(l, txt[0] ? txt : "--"); lv_obj_set_style_text_color(l, c, 0); }
}
lv_obj_t *rrect(lv_obj_t *p, int x, int y, int w, int h, int r, uint32_t col) {
  lv_obj_t *o = lv_obj_create(p);
  lv_obj_set_pos(o, x, y); lv_obj_set_size(o, w, h);
  lv_obj_set_style_radius(o, r, 0);
  lv_obj_set_style_bg_color(o, lv_color_hex(col), 0);
  lv_obj_set_style_border_width(o, 0, 0);
  lv_obj_set_style_pad_all(o, 0, 0);
  lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  return o;
}
int model_mood(int i) {
  bool inc = (i == 0) ? g_status.haikuUp : (i == 1) ? g_status.sonnetUp
           : (i == 2) ? g_status.opusUp  : g_status.fableUp;
  int c = g_models[i].pr.code;
  if (!inc) return 3;
  if (c == 0) return 0;
  if (c == 200) return 1;
  if (c == 429) return 2;
  if (c == 404) return 4;
  return 3;                              // rede / 5xx / auth
}

void build_accessory(lv_obj_t *c, int model) {
  switch (model) {
    case 0:                              // Haiku: raio
      rrect(c, 46, 0, 8, 7, 1, C_WARN);
      rrect(c, 41, 5, 8, 7, 1, C_WARN);
      rrect(c, 46, 10, 8, 7, 1, C_WARN);
      break;
    case 1:                              // Sonnet: nota musical
      rrect(c, 50, 0, 10, 4, 1, 0x7DD3FC);
      rrect(c, 50, 0, 4, 13, 1, 0x7DD3FC);
      rrect(c, 44, 10, 8, 7, 3, 0x7DD3FC);
      break;
    case 2:                              // Opus: coroa
      rrect(c, 30, 4, 7, 8, 1, C_WARN);
      rrect(c, 41, 1, 7, 11, 1, C_WARN);
      rrect(c, 52, 4, 7, 8, 1, C_WARN);
      rrect(c, 30, 12, 29, 6, 1, C_WARN);
      break;
    case 3:                              // Fable: faisca (estrela 4 pontas)
      rrect(c, 41, 0, 6, 17, 2, 0xC4B5FD);
      rrect(c, 36, 6, 16, 6, 2, 0xC4B5FD);
      break;
  }
}

void build_model_mascot(lv_obj_t *parent, int cx, int i) {
  if (g_mascN >= NMODELS) return;
  int mood = model_mood(i);
  int baseY = 14;
  lv_obj_t *c = lv_obj_create(parent);
  lv_obj_set_pos(c, cx - 44, baseY); lv_obj_set_size(c, 88, 80);
  lv_obj_set_style_bg_opa(c, 0, 0); lv_obj_set_style_border_width(c, 0, 0);
  lv_obj_set_style_pad_all(c, 0, 0); lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *img = lv_image_create(c);
  lv_image_set_src(img, &img_clawd_md);
  lv_obj_set_pos(img, 0, 20);

  build_accessory(c, i);

  const int ex[2] = {CLAWD_MD_EYE0_X, CLAWD_MD_EYE1_X};
  const int ey = CLAWD_MD_EYE0_Y + 20, ew = CLAWD_MD_EYE0_W, eh = CLAWD_MD_EYE0_H;

  Mascot &m = g_masc[g_mascN];
  m.cont = c; m.img = img; m.baseY = baseY; m.mood = mood;
  m.lid[0] = m.lid[1] = nullptr; m.drop = nullptr;

  if (mood == 1) {                       // ok: pálpebras escondidas p/ piscar
    for (int k = 0; k < 2; k++) {
      m.lid[k] = rrect(c, ex[k] - 1, ey - 1, ew + 2, eh + 2, 1, C_ACCENT);
      lv_obj_add_flag(m.lid[k], LV_OBJ_FLAG_HIDDEN);
    }
  } else if (mood == 2) {                // limitado: gota de suor
    m.drop = rrect(c, 70, 24, 6, 10, 3, 0x7DD3FC);
  } else if (mood == 3) {                // erro/incidente: cinza + olhos em X
    lv_obj_set_style_image_recolor(img, lv_color_hex(0x6A6A74), 0);
    lv_obj_set_style_image_recolor_opa(img, 190, 0);
    lv_obj_set_y(img, 24);               // caidinho
    for (int k = 0; k < 2; k++) {
      g_mXPts[i][k * 2][0]     = { (lv_value_precise_t)(ex[k] - 2), (lv_value_precise_t)(ey + 2) };
      g_mXPts[i][k * 2][1]     = { (lv_value_precise_t)(ex[k] + ew + 2), (lv_value_precise_t)(ey + eh + 6) };
      g_mXPts[i][k * 2 + 1][0] = { (lv_value_precise_t)(ex[k] + ew + 2), (lv_value_precise_t)(ey + 2) };
      g_mXPts[i][k * 2 + 1][1] = { (lv_value_precise_t)(ex[k] - 2), (lv_value_precise_t)(ey + eh + 6) };
      for (int l = 0; l < 2; l++) {
        lv_obj_t *ln = lv_line_create(c);
        lv_line_set_points(ln, g_mXPts[i][k * 2 + l], 2);
        lv_obj_set_style_line_width(ln, 3, 0);
        lv_obj_set_style_line_color(ln, lv_color_hex(C_BAD), 0);
        lv_obj_set_style_line_rounded(ln, true, 0);
      }
    }
  } else if (mood == 4) {                // n/d p/ o plano: dormindo
    lv_obj_set_style_image_recolor(img, lv_color_hex(0x6A6A74), 0);
    lv_obj_set_style_image_recolor_opa(img, 170, 0);
    for (int k = 0; k < 2; k++)
      m.lid[k] = rrect(c, ex[k] - 1, ey + eh / 2, ew + 2, eh / 2 + 1, 1, 0x8A8A94);
    lv_obj_set_style_opa(c, 180, 0);
  } else {                               // nunca sondado: apagadinho
    lv_obj_set_style_opa(c, 140, 0);
  }
  g_mascN++;
}

void model_chip(int i, char *out, size_t sz, uint32_t *col) {
  int c = g_models[i].pr.code;
  if (c == 0)             { strlcpy(out, "--", sz);        *col = C_MUTED; }
  else if (c == 200)      { snprintf(out, sz, "OK %.1fs", g_models[i].pr.ms / 1000.0f); *col = C_OK; }
  else if (c == 429)      { strlcpy(out, TRS("LIMITADO", "LIMITED"), sz); *col = C_WARN; }
  else if (c == 404)      { strlcpy(out, TRS("N/D", "N/A"), sz); *col = C_MUTED; }
  else if (c == 401 || c == 403) { strlcpy(out, "AUTH", sz); *col = C_BAD; }
  else if (c < 0)         { strlcpy(out, TRS("REDE", "NET"), sz); *col = C_BAD; }
  else                    { snprintf(out, sz, TRS("ERRO %d", "ERR %d"), c); *col = C_BAD; }
}
