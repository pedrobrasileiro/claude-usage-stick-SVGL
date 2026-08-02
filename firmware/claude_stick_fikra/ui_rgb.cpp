#include <lvgl.h>
#include "ui_rgb.h"
#include "ui_helpers.h"
#include "state_app.h"
#include "rgb_led.h"
#include "config.h"

static lv_obj_t *s_dot = nullptr;
static lv_obj_t *s_status = nullptr;

static void update_indicator() {
  if (s_dot) lv_obj_set_style_bg_color(s_dot, lv_color_make(g_ledR, g_ledG, g_ledB), 0);
  if (s_status) {
    const char *m = g_ledMode == LED_OFF   ? TRS("desligado", "off")
                  : g_ledMode == LED_CYCLE ? TRS("alternando cores", "cycling colors")
                                           : TRS("cor fixa", "solid color");
    char buf[40];
    snprintf(buf, sizeof(buf), TRS("LED: %s", "LED: %s"), m);
    lv_label_set_text(s_status, buf);
  }
}

static void set_color_cb(lv_event_t *e) {
  uint32_t rgb = (uint32_t)(intptr_t)lv_event_get_user_data(e);
  led_set_mode(LED_SOLID);
  led_set((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
  update_indicator();
}

static void toggle_cycle_cb(lv_event_t *e) {
  (void)e;
  led_set_mode(g_ledMode == LED_CYCLE ? LED_OFF : LED_CYCLE);
  update_indicator();
}

static void turn_off_cb(lv_event_t *e) {
  (void)e;
  led_set_mode(LED_OFF);
  update_indicator();
}

void ui_rgb() {
  lv_obj_t *scr = lv_screen_active();

  lv_obj_t *title = mklabel(scr, TRS("LED RGB", "RGB LED"), &lv_font_montserrat_18, C_TEXT);
  lv_obj_align(title, LV_ALIGN_TOP_LEFT, 10, 8);

  lv_obj_t *bk = mkbtn(scr, LV_SYMBOL_LEFT, &lv_font_montserrat_14, C_SURFACE2, C_MUTED);
  lv_obj_set_size(bk, 44, 28);
  lv_obj_set_ext_click_area(bk, 8);
  lv_obj_align(bk, LV_ALIGN_TOP_RIGHT, -8, 6);
  lv_obj_add_event_cb(bk, nav_cb, LV_EVENT_CLICKED, (void *)(intptr_t)ST_SETTINGS);

  // indicador da cor atual
  s_dot = rrect(scr, SCREEN_WIDTH / 2 - 24, 40, 48, 48, 24, 0x000000);
  lv_obj_set_style_border_width(s_dot, 2, 0);
  lv_obj_set_style_border_color(s_dot, lv_color_hex(C_BORDER), 0);

  s_status = mklabel(scr, "", &lv_font_montserrat_14, C_MUTED);
  lv_obj_align(s_status, LV_ALIGN_TOP_MID, 0, 96);

  // botões de cor fixa — R/G/B + branco e preto (teste de painel)
  static const struct { const char *label; uint32_t rgb; uint32_t fg; } colors[5] = {
    {"R", 0xFF0000, 0xFFFFFF}, {"G", 0x00FF00, 0xFFFFFF}, {"B", 0x0000FF, 0xFFFFFF},
    {"W", 0xFFFFFF, 0x000000}, {"K", 0x000000, 0xFFFFFF},
  };
  const int NCOLORS = 5;
  const int bw = 56, bh = 56, gap = 6;
  const int totalw = bw * NCOLORS + gap * (NCOLORS - 1);
  const int x0 = (SCREEN_WIDTH - totalw) / 2;
  for (int i = 0; i < NCOLORS; i++) {
    lv_obj_t *b = mkbtn(scr, colors[i].label, &lv_font_montserrat_20, colors[i].rgb, colors[i].fg);
    lv_obj_set_size(b, bw, bh);
    lv_obj_align(b, LV_ALIGN_TOP_LEFT, x0 + i * (bw + gap), 128);
    if (colors[i].rgb == 0x000000) {   // preto precisa de borda pra não sumir no fundo escuro
      lv_obj_set_style_border_width(b, 1, 0);
      lv_obj_set_style_border_color(b, lv_color_hex(C_BORDER), 0);
    }
    lv_obj_add_event_cb(b, set_color_cb, LV_EVENT_CLICKED, (void *)(intptr_t)colors[i].rgb);
  }

  lv_obj_t *cyc = mkbtn(scr, TRS(LV_SYMBOL_LOOP "  Alternar", LV_SYMBOL_LOOP "  Cycle"),
                        &lv_font_montserrat_14, C_SURFACE2, C_TEXT);
  lv_obj_set_size(cyc, 150, 40);
  lv_obj_align(cyc, LV_ALIGN_TOP_LEFT, x0, 128 + bh + 12);
  lv_obj_add_event_cb(cyc, toggle_cycle_cb, LV_EVENT_CLICKED, nullptr);

  lv_obj_t *off = mkbtn(scr, TRS(LV_SYMBOL_CLOSE "  Desligar", LV_SYMBOL_CLOSE "  Off"),
                        &lv_font_montserrat_14, C_SURFACE2, C_MUTED);
  lv_obj_set_size(off, 150, 40);
  lv_obj_align(off, LV_ALIGN_TOP_LEFT, x0 + totalw - 150, 128 + bh + 12);
  lv_obj_add_event_cb(off, turn_off_cb, LV_EVENT_CLICKED, nullptr);

  update_indicator();
}
