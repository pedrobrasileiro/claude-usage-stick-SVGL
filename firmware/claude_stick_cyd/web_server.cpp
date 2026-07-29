#include <Arduino.h>
#include <ESPmDNS.h>
#include <time.h>
#include "web_server.h"
#include "wifi_manager.h"
#include "state_app.h"
#include "state_security.h"
#include "settings_actions.h"
#include "logo_assets.h"

extern WiFiManager g_wifi;      // ainda vive no .ino (hardware, ver plano de módulos)
extern void update_tok_row();   // dashboard — ainda em claude_stick_cyd.ino (ver ui_dashboard.cpp)

WebServer *g_web = nullptr;

void stop_web() { if (g_web) { g_web->stop(); delete g_web; g_web = nullptr; } }

static bool g_mdnsUp = false;
void ensure_mdns() {
  if (g_mdnsUp || !g_wifi.isConnected()) return;
  if (MDNS.begin("claude-stick")) {
    MDNS.addService("http", "tcp", 80);
    g_mdnsUp = true;
    Serial.println("[MDNS] claude-stick.local");
  }
}

static void anim_opa_cb(void *o, int32_t v) { lv_obj_set_style_opa((lv_obj_t *)o, (lv_opa_t)v, 0); }

lv_obj_t *build_claude_mark(lv_obj_t *parent) {
  lv_obj_t *img = lv_image_create(parent);
  lv_image_set_src(img, &img_clawd_big);
  lv_anim_t a; lv_anim_init(&a);
  lv_anim_set_var(&a, img);
  lv_anim_set_exec_cb(&a, anim_opa_cb);
  lv_anim_set_values(&a, 140, 255);
  lv_anim_set_duration(&a, 900);
  lv_anim_set_playback_duration(&a, 900);
  lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
  lv_anim_start(&a);
  return img;
}

// ---- Endpoints de dados (bridge de tokens; ver tools/token_bridge.py) ----
static long long jll(const String &s, const char *key) {
  String k = String("\"") + key + "\"";
  int i = s.indexOf(k); if (i < 0) return 0;
  i = s.indexOf(':', i + k.length() - 1); if (i < 0) return 0;
  return atoll(s.c_str() + i + 1);
}
static void handleWindow() {
  char b[192];
  snprintf(b, sizeof(b),
           "{\"now\":%lu,\"h5_reset\":%lu,\"d7_reset\":%lu,\"h5_util\":%.4f,\"d7_util\":%.4f}",
           (unsigned long)time(nullptr),
           (unsigned long)g_usage.h5ResetEpoch, (unsigned long)g_usage.d7ResetEpoch,
           g_usage.h5 / 100.0f, g_usage.d7 / 100.0f);
  g_web->send(200, "application/json", b);
}
static void handleTokensPost() {
  String body = g_web->arg("plain");
  g_tok.tin      = jll(body, "in");
  g_tok.tout     = jll(body, "out");
  g_tok.cache    = jll(body, "cache");
  g_tok.sessions = (int)jll(body, "sessions");
  g_tok.atMs     = millis();
  Serial.printf("[TOK] in=%lld out=%lld cache=%lld sess=%d\n",
                g_tok.tin, g_tok.tout, g_tok.cache, g_tok.sessions);
  g_web->send(200, "application/json", "{\"ok\":true}");
  update_tok_row();
}
static void handleInfo() {
  String h = F("<!doctype html><html lang=pt><head><meta charset=utf-8>"
               "<meta name=viewport content='width=device-width,initial-scale=1'>"
               "<title>Claude Usage Stick</title><style>" WEB_CSS "</style></head><body><div class=card>"
               "<h1>" WEB_SPARK " Claude Usage Stick</h1>"
               "<p>Device online. Endpoints: <code>GET /window</code> (janela atual) e "
               "<code>POST /tokens</code> (bridge de tokens por sessao — ver tools/token_bridge.py).</p>"
               "</div></body></html>");
  g_web->send(200, "text/html; charset=utf-8", h);
}
// ---- /settings: única forma de mexer em ajustes sem touch ----
static String settings_page(const char *msg) {
  const char *briN[3] = {"baixo", "medio", "alto"};
  char bri[16]; snprintf(bri, sizeof(bri), "%s", briN[g_briIdx]);
  String h = F("<!doctype html><html lang=pt><head><meta charset=utf-8>"
               "<meta name=viewport content='width=device-width,initial-scale=1'>"
               "<title>Claude Usage Stick — Ajustes</title><style>" WEB_CSS
               "form{width:100%}button{margin-top:8px}</style></head><body><div class=card>"
               "<h1>" WEB_SPARK " Ajustes</h1>");
  if (msg && msg[0]) { h += F("<p style='color:#4ADE80'>"); h += msg; h += F("</p>"); }
  char row[160];
  snprintf(row, sizeof(row), "<p>Atualizar a cada: <b>%ds</b></p>", g_pollSec);
  h += row;
  h += F("<form method=POST action='/settings'><button name=act value=6>Trocar intervalo</button></form>");
  snprintf(row, sizeof(row), "<p>Slideshow: <b>%ds</b> (0 = desligado)</p>", g_slideSec);
  h += row;
  h += F("<form method=POST action='/settings'><button name=act value=8>Trocar slideshow</button></form>");
  snprintf(row, sizeof(row), "<p>Fuso: <b>GMT%+d</b></p>", g_tzOffset);
  h += row;
  h += F("<form method=POST action='/settings'><button name=act value=7>Trocar fuso</button></form>");
  snprintf(row, sizeof(row), "<p>Brilho: <b>%s</b></p>", bri);
  h += row;
  h += F("<form method=POST action='/settings'><button name=act value=3>Trocar brilho</button></form>"
         "<p>Idioma: <b>"); h += (g_lang ? "English" : "Portugues"); h += F("</b></p>"
         "<form method=POST action='/settings'><button name=act value=9>Trocar idioma</button></form>"
         "<p>Modo do ritmo por hora (heatmap): hoje/7d/30d/tudo</p>"
         "<form method=POST action='/settings'>"
         "<select name=heatm style='width:100%;padding:10px;border-radius:10px;border:1px solid #30303A;"
         "background:#0F0F12;color:#F2F0EC'>"
         "<option value=0>Hoje</option><option value=1>7 dias</option>"
         "<option value=2>30 dias</option><option value=3>Tudo</option></select>"
         "<button name=act value=11>Aplicar</button></form>"
         "<p>&nbsp;</p>"
         "<form method=POST action='/settings'><button name=act value=0>Atualizar agora</button></form>"
         "<form method=POST action='/settings'><button name=act value=1>Reconfigurar WiFi</button></form>"
         "<form method=POST action='/settings'><button name=act value=2>Trocar token</button></form>"
         "<form method=POST action='/settings' onsubmit=\"return confirm('Apagar tudo?')\">"
         "<input type=hidden name=confirm value=1>"
         "<button name=act value=4 style='background:#F87171'>Apagar tudo</button></form>"
         "</div></body></html>");
  return h;
}
static String settings_pin_page(const char *msg) {
  String h = F("<!doctype html><html lang=pt><head><meta charset=utf-8>"
               "<meta name=viewport content='width=device-width,initial-scale=1'>"
               "<title>Claude Usage Stick — Ajustes</title><style>" WEB_CSS "</style></head><body><div class=card>"
               "<h1>" WEB_SPARK " Ajustes</h1>");
  if (msg && msg[0]) { h += F("<p style='color:#F87171'>"); h += msg; h += F("</p>"); }
  h += F("<p>Digite o PIN pra acessar os ajustes:</p>"
         "<form method=POST action='/settings/unlock'>"
         "<input name=pin inputmode=numeric maxlength=4 autofocus placeholder='PIN' "
         "style='width:100%;padding:12px;border-radius:10px;border:1px solid #30303A;"
         "background:#0F0F12;color:#F2F0EC;font-size:14px;margin-bottom:10px'>"
         "<button type=submit>Entrar</button></form></div></body></html>");
  return h;
}

static void handleSettingsGet() {
  g_web->send(200, "text/html; charset=utf-8",
              settings_unlocked() ? settings_page("") : settings_pin_page(""));
}
static void handleSettingsUnlockPost() {
  String pin = g_web->arg("pin"); pin.trim();
  char err[80] = {0};
  if (verify_pin_or_lockout(pin, err, sizeof(err))) {
    g_settingsUnlockedUntil = millis() + SETTINGS_SESSION_MS;
    g_web->send(200, "text/html; charset=utf-8", settings_page(""));
  } else {
    g_web->send(200, "text/html; charset=utf-8", settings_pin_page(err));
  }
}
static void handleSettingsPost() {
  if (!settings_unlocked()) {
    g_web->send(200, "text/html; charset=utf-8", settings_pin_page(TRS("Digite o PIN primeiro.", "Enter the PIN first.")));
    return;
  }
  g_settingsUnlockedUntil = millis() + SETTINGS_SESSION_MS;   // renova a sessão a cada ação
  int act = g_web->arg("act").toInt();
  if (act == 4 && g_web->arg("confirm") != "1") {
    g_web->send(200, "text/html; charset=utf-8", settings_page("Não confirmado."));
    return;
  }
  if (act == 11) {
    int m = g_web->arg("heatm").toInt();
    if (m >= 0 && m <= 3) { g_heatMode = m; g_prefs.putInt("heatm", m); }
  } else {
    apply_setting_action(act);
  }
  g_web->send(200, "text/html; charset=utf-8", settings_page("Aplicado."));
}

void start_data_web() {
  stop_web();
  ensure_mdns();
  g_web = new WebServer(80);
  g_web->on("/", HTTP_GET, handleInfo);
  g_web->on("/window", HTTP_GET, handleWindow);
  g_web->on("/tokens", HTTP_POST, handleTokensPost);
  g_web->on("/settings", HTTP_GET, handleSettingsGet);
  g_web->on("/settings", HTTP_POST, handleSettingsPost);
  g_web->on("/settings/unlock", HTTP_POST, handleSettingsUnlockPost);
  g_web->onNotFound([]() { g_web->send(404, "application/json", "{\"error\":\"not_found\"}"); });
  g_web->begin();
}
