#include <Arduino.h>
#include "ui_screens.h"
#include "state_app.h"
#include "state_security.h"
#include "state_dashboard.h"
#include "ui_helpers.h"
#include "settings_actions.h"
#include "web_server.h"
#include "config.h"

// ============================================================
// Tela: PIN (keypad touch) — pedido a cada boot pra decifrar o token;
// mesma tela gateia o acesso a Ajustes (settings_unlocked()/g_settingsUnlockedUntil).
// ============================================================
static const char *pin_map[] = {
  "1", "2", "3", "\n",
  "4", "5", "6", "\n",
  "7", "8", "9", "\n",
  LV_SYMBOL_LEFT, "0", LV_SYMBOL_OK, ""
};

static void pin_update_dots() {
  if (!g_pinDots) return;
  char dots[24] = {0};
  int len = strlen(g_pinEntry);
  for (int i = 0; i < PIN_LEN; i++) {
    strcat(dots, i < len ? "*" : "_");
    if (i < PIN_LEN - 1) strcat(dots, " ");
  }
  lv_label_set_text(g_pinDots, dots);
}

static void pin_submit() {
  char err[80] = {0};
  bool ok;
  if (g_pinForSettings) ok = verify_pin_or_lockout(String(g_pinEntry), err, sizeof(err));
  else                  ok = verify_pin_or_lockout(String(g_pinEntry), err, sizeof(err),
                                                    nullptr, g_token, sizeof(g_token));
  g_pinEntry[0] = 0;
  pin_update_dots();
  if (ok) {
    if (g_pinForSettings) {
      g_settingsUnlockedUntil = millis() + SETTINGS_SESSION_MS;
      request_state(ST_SETTINGS);
    } else {
      request_state(ST_LOADING);
    }
    return;
  }
  if (g_pinMsg) lv_label_set_text(g_pinMsg, err);   // wipe (se ocorrer) já redireciona pro provisionamento
}

static void pin_kb_cb(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;
  if (millis() < g_lockoutUntil) return;     // travado
  lv_obj_t *bm = (lv_obj_t *)lv_event_get_target(e);
  uint32_t id = lv_buttonmatrix_get_selected_button(bm);
  const char *txt = lv_buttonmatrix_get_button_text(bm, id);
  if (!txt) return;
  int len = strlen(g_pinEntry);
  if (strcmp(txt, LV_SYMBOL_LEFT) == 0) {
    if (len > 0) g_pinEntry[len - 1] = 0;
    pin_update_dots();
  } else if (strcmp(txt, LV_SYMBOL_OK) == 0) {
    if (len == PIN_LEN) pin_submit();
  } else if (len < PIN_LEN) {
    g_pinEntry[len] = txt[0];
    g_pinEntry[len + 1] = 0;
    pin_update_dots();
    if (len + 1 == PIN_LEN) pin_submit();     // auto-submit ao completar
  }
}

void ui_pin() {
  lv_obj_t *scr = lv_screen_active();
  lv_obj_t *t = mklabel(scr, TRS("Digite o PIN", "Enter the PIN"), &lv_font_montserrat_18, C_TEXT);
  lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 8);

  g_pinDots = mklabel(scr, "", &lv_font_montserrat_24, C_ACCENT);
  lv_obj_align(g_pinDots, LV_ALIGN_TOP_MID, 0, 36);
  pin_update_dots();

  g_pinMsg = mklabel(scr, g_pinForSettings ? TRS("Acesso aos ajustes", "Access to settings")
                                            : TRS("Necessario p/ desbloquear o token",
                                                  "Needed to unlock the token"),
                     &lv_font_montserrat_12, C_MUTED);
  lv_obj_align(g_pinMsg, LV_ALIGN_TOP_MID, 0, 62);

  lv_obj_t *bm = lv_buttonmatrix_create(scr);
  lv_buttonmatrix_set_map(bm, pin_map);
  lv_obj_set_size(bm, SCREEN_WIDTH - 40, SCREEN_HEIGHT - 92);
  lv_obj_align(bm, LV_ALIGN_BOTTOM_MID, 0, -6);
  lv_obj_set_style_bg_color(bm, lv_color_hex(C_BG), 0);
  lv_obj_set_style_border_width(bm, 0, 0);
  lv_obj_set_style_text_font(bm, &lv_font_montserrat_20, 0);
  lv_obj_set_style_bg_color(bm, lv_color_hex(C_SURFACE2), LV_PART_ITEMS);
  lv_obj_set_style_text_color(bm, lv_color_hex(C_TEXT), LV_PART_ITEMS);
  lv_obj_add_event_cb(bm, pin_kb_cb, LV_EVENT_VALUE_CHANGED, NULL);

  if (millis() < g_lockoutUntil && g_pinMsg) {
    int rem = (g_lockoutUntil - millis()) / 1000;
    char m[48]; snprintf(m, sizeof(m), TRS("Aguarde %ds", "Wait %ds"), rem);
    lv_label_set_text(g_pinMsg, m);
  }
}

void open_settings() {
  if (settings_unlocked()) { request_state(ST_SETTINGS); return; }
  g_pinForSettings = true;
  g_pinEntry[0] = 0;
  request_state(ST_PIN);
}
void gear_cb(lv_event_t *e) { (void)e; open_settings(); }

// ============================================================
// Tela: loading / mensagem
// ============================================================
void ui_message(const char *title, const char *sub, uint32_t color) {
  lv_obj_t *scr = lv_screen_active();
  lv_obj_t *t = mklabel(scr, title, &lv_font_montserrat_24, color);
  lv_obj_align(t, LV_ALIGN_CENTER, 0, -16);
  if (sub && sub[0]) {
    lv_obj_t *s = mklabel(scr, sub, &lv_font_montserrat_16, C_MUTED);
    lv_obj_align(s, LV_ALIGN_CENTER, 0, 20);
  }
}
void ui_loading(const char *sub) {
  lv_obj_t *scr = lv_screen_active();
  lv_obj_t *mark = build_claude_mark(scr);
  lv_obj_align(mark, LV_ALIGN_CENTER, 0, -50);
  lv_obj_t *t = mklabel(scr, TRS("Carregando seu uso...", "Loading your usage..."), &lv_font_montserrat_18, C_TEXT);
  lv_obj_align(t, LV_ALIGN_CENTER, 0, 28);
  if (sub && sub[0]) {
    lv_obj_t *s = mklabel(scr, sub, &lv_font_montserrat_12, C_MUTED);
    lv_obj_align(s, LV_ALIGN_CENTER, 0, 52);
  }
  lv_obj_t *spn = lv_spinner_create(scr);
  lv_spinner_set_anim_params(spn, 1200, 70);
  lv_obj_set_size(spn, 34, 34);
  lv_obj_align(spn, LV_ALIGN_CENTER, 0, 90);
  lv_obj_set_style_arc_color(spn, lv_color_hex(C_SURFACE2), LV_PART_MAIN);
  lv_obj_set_style_arc_color(spn, lv_color_hex(C_ACCENT), LV_PART_INDICATOR);
  lv_obj_set_style_arc_width(spn, 4, LV_PART_MAIN);
  lv_obj_set_style_arc_width(spn, 4, LV_PART_INDICATOR);
}

// ============================================================
// Tela: settings (lista rolável; linhas >=44px de toque)
// ============================================================
static bool g_wipeArmed = false;
static lv_obj_t *g_wipeLbl = nullptr;

static void settings_action_cb(lv_event_t *e) {
  int act = (int)(intptr_t)lv_event_get_user_data(e);
  if (act == 4) {                                     // apagar tudo — exige 2 toques
    if (!g_wipeArmed) {
      g_wipeArmed = true;
      if (g_wipeLbl) lv_label_set_text(g_wipeLbl, TRS(LV_SYMBOL_TRASH "  Toque de novo p/ confirmar",
                                                      LV_SYMBOL_TRASH "  Tap again to confirm"));
      return;
    }
    g_wipeArmed = false;
  }
  apply_setting_action(act);
}

static void add_setting_row(lv_obj_t *p, const char *txt, int act, uint32_t fg, lv_obj_t **out) {
  lv_obj_t *b = lv_button_create(p);
  lv_obj_set_size(b, SCREEN_WIDTH - 16, 36);
  lv_obj_set_style_bg_color(b, lv_color_hex(C_SURFACE), 0);
  lv_obj_set_style_radius(b, 10, 0);
  lv_obj_set_style_shadow_width(b, 0, 0);
  lv_obj_t *l = mklabel(b, txt, &lv_font_montserrat_14, fg);
  lv_obj_align(l, LV_ALIGN_LEFT_MID, 8, 0);
  lv_obj_add_event_cb(b, settings_action_cb, LV_EVENT_CLICKED, (void *)(intptr_t)act);
  if (out) *out = l;
}

void ui_settings() {
  lv_obj_t *scr = lv_screen_active();
  g_wipeArmed = false;
  start_data_web();
  lv_obj_t *title = mklabel(scr, TRS("Ajustes", "Settings"), &lv_font_montserrat_18, C_TEXT);
  lv_obj_align(title, LV_ALIGN_TOP_LEFT, 10, 8);

  lv_obj_t *bk = mkbtn(scr, LV_SYMBOL_LEFT, &lv_font_montserrat_14, C_SURFACE2, C_MUTED);
  lv_obj_set_size(bk, 44, 28);
  lv_obj_set_ext_click_area(bk, 8);
  lv_obj_align(bk, LV_ALIGN_TOP_RIGHT, -8, 6);
  lv_obj_add_event_cb(bk, nav_cb, LV_EVENT_CLICKED, (void *)(intptr_t)ST_MAIN);

  lv_obj_t *lst = lv_obj_create(scr);
  lv_obj_set_pos(lst, 8, 40);
  lv_obj_set_size(lst, SCREEN_WIDTH - 16, SCREEN_HEIGHT - 40);
  lv_obj_set_style_bg_opa(lst, 0, 0);
  lv_obj_set_style_border_width(lst, 0, 0);
  lv_obj_set_style_pad_all(lst, 0, 0);
  lv_obj_set_style_pad_row(lst, 6, 0);
  lv_obj_set_flex_flow(lst, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_scroll_dir(lst, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(lst, LV_SCROLLBAR_MODE_AUTO);

  const char *n[3] = {TRS("baixo", "low"), TRS("medio", "medium"), TRS("alto", "high")};
  char bri[40]; snprintf(bri, sizeof(bri), TRS(LV_SYMBOL_EYE_OPEN "  Brilho: %s",
                                               LV_SYMBOL_EYE_OPEN "  Brightness: %s"), n[g_briIdx]);
  char pollTxt[40];
  if (g_pollSec < 60) snprintf(pollTxt, sizeof(pollTxt), TRS(LV_SYMBOL_LOOP "  Atualizar: %ds",
                                                             LV_SYMBOL_LOOP "  Refresh: %ds"), g_pollSec);
  else                snprintf(pollTxt, sizeof(pollTxt), TRS(LV_SYMBOL_LOOP "  Atualizar: %dmin",
                                                             LV_SYMBOL_LOOP "  Refresh: %dmin"), g_pollSec / 60);
  char tzTxt[40]; snprintf(tzTxt, sizeof(tzTxt), TRS(LV_SYMBOL_GPS "  Fuso: GMT%+d",
                                                     LV_SYMBOL_GPS "  Timezone: GMT%+d"), g_tzOffset);
  char slideTxt[48];
  if (g_slideSec) snprintf(slideTxt, sizeof(slideTxt), LV_SYMBOL_PLAY "  Slideshow: %ds", g_slideSec);
  else            snprintf(slideTxt, sizeof(slideTxt), "%s", TRS(LV_SYMBOL_PLAY "  Slideshow: desligado",
                                                                 LV_SYMBOL_PLAY "  Slideshow: off"));
  const char *heatN[4] = {TRS("hoje", "today"), "7d", "30d", TRS("tudo", "all")};
  char heatTxt[48]; snprintf(heatTxt, sizeof(heatTxt), TRS(LV_SYMBOL_CHARGE "  Ritmo por hora: %s",
                                                           LV_SYMBOL_CHARGE "  Hourly burn: %s"), heatN[g_heatMode]);

  add_setting_row(lst, TRS(LV_SYMBOL_REFRESH "  Atualizar agora",
                           LV_SYMBOL_REFRESH "  Refresh now"),   0, C_TEXT, nullptr);
  add_setting_row(lst, pollTxt,                                  6, C_TEXT, &g_pollLbl);
  add_setting_row(lst, slideTxt,                                 8, C_TEXT, &g_slideLbl);
  add_setting_row(lst, tzTxt,                                    7, C_TEXT, &g_tzLbl);
  add_setting_row(lst, bri,                                      3, C_TEXT, &g_briLbl);
  add_setting_row(lst, heatTxt,                                 11, C_TEXT, &g_heatLbl);
  add_setting_row(lst, TRS(LV_SYMBOL_LIST "  Idioma: Portugues",
                           LV_SYMBOL_LIST "  Language: English"), 9, C_TEXT, nullptr);
  add_setting_row(lst, TRS(LV_SYMBOL_WIFI "  Configurar WiFi",
                           LV_SYMBOL_WIFI "  Configure WiFi"),   1, C_TEXT, nullptr);
  add_setting_row(lst, TRS(LV_SYMBOL_KEYBOARD "  Trocar token",
                           LV_SYMBOL_KEYBOARD "  Change token"), 2, C_TEXT, nullptr);
  add_setting_row(lst, TRS(LV_SYMBOL_FILE "  Sobre",
                           LV_SYMBOL_FILE "  About"),           10, C_TEXT, nullptr);
  add_setting_row(lst, TRS(LV_SYMBOL_TRASH "  Apagar tudo",
                           LV_SYMBOL_TRASH "  Erase everything"), 4, C_BAD, &g_wipeLbl);
}

// ============================================================
// Tela: sobre / about
// ============================================================
void ui_about() {
  lv_obj_t *scr = lv_screen_active();
  start_data_web();

  lv_obj_t *bk = mkbtn(scr, LV_SYMBOL_LEFT, &lv_font_montserrat_14, C_SURFACE2, C_MUTED);
  lv_obj_set_size(bk, 44, 28);
  lv_obj_set_ext_click_area(bk, 8);
  lv_obj_align(bk, LV_ALIGN_TOP_RIGHT, -8, 6);
  lv_obj_add_event_cb(bk, nav_cb, LV_EVENT_CLICKED, (void *)(intptr_t)ST_SETTINGS);

  lv_obj_t *mark = build_claude_mark(scr);
  lv_obj_align(mark, LV_ALIGN_TOP_MID, 0, 0);

  lv_obj_t *t = mklabel(scr, "Claude Usage Stick", &lv_font_montserrat_20, C_TEXT);
  lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 74);

  char v[64];
  snprintf(v, sizeof(v), "v" FW_VERSION " \xE2\x80\xA2 ESP32-2432S028 (CYD) \xE2\x80\xA2 LVGL 9.2");
  lv_obj_t *ver = mklabel(scr, v, &lv_font_montserrat_12, C_FAINT);
  lv_obj_align(ver, LV_ALIGN_TOP_MID, 0, 100);

  lv_obj_t *d = mklabel(scr, TRS("Medidor de uso do Claude Code em tempo real: "
                                 "janelas de 5h e semanal direto da API da Anthropic.",
                                 "Real-time Claude Code usage meter: "
                                 "5-hour and weekly windows straight from the Anthropic API."),
                        &lv_font_montserrat_14, C_MUTED);
  lv_obj_set_width(d, SCREEN_WIDTH - 40);
  lv_obj_set_style_text_align(d, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_long_mode(d, LV_LABEL_LONG_WRAP);
  lv_obj_align(d, LV_ALIGN_TOP_MID, 0, 122);
}
