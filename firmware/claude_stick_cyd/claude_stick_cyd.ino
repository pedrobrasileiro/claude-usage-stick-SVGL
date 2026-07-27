/**
 * Claude Usage Stick — CYD (ESP32-2432S028), sem touch
 * Placa: "Cheap Yellow Display", ESP32 clássico WROOM-32, display ILI9341
 * SPI 320x240 paisagem. Fork de firmware/claude_stick/ (Guition JC4832W535)
 * adaptado pra esse hardware — ver firmware/bringup_cyd/ pro bring-up.
 *
 * Dashboard do rate-limit do Claude Code (janelas 5h e 7d, headers unified-*),
 * sonda real por modelo (latencia + HTTP), projecao de esgotamento da janela
 * 5h e ritmo de uso por hora com filtro de periodo.
 *
 * Sem touch nessa placa (não respondeu em nenhum teste de hardware — ver
 * plano). Navegação via botão físico BOOT (clique curto = próxima tela do
 * dashboard; clique longo = ajustes). Configuração (WiFi + token + PIN) e
 * ajustes finos via portal web (AP no primeiro uso, depois
 * claude-stick.local) — mesma cifra AES-256-GCM do app original. Boot
 * decifra o token sozinho usando o PIN salvo na NVS (sem prompt); o PIN só
 * é pedido de novo pra entrar em /settings (troca de token, apagar tudo
 * etc.). Trade-off: PIN em NVS sem flash encryption é recuperável com
 * acesso físico/dump ao device — mesmo risco que as demais configs já
 * salvas em claro hoje.
 *
 * Tokens por sessao: a API nao expoe contagem para conta de assinatura; um
 * bridge opcional (tools/token_bridge.py) soma os transcripts locais do
 * Claude Code e faz POST /tokens neste device (mDNS claude-stick.local).
 */
#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <lvgl.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <LittleFS.h>
#include <time.h>
#include <math.h>
#include "config.h"
#include "wifi_manager.h"
#include "api.h"
#include "status.h"
#include "crypto.h"
#include "logo_assets.h"   // Clawd + logotipo oficiais (gerado por tools/gen_logo_assets.py)

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

// ---- Idioma (0 = portugues, 1 = english; Ajustes -> NVS "lang") ----
static uint8_t g_lang = 0;
#define TRS(pt, en) (g_lang ? (en) : (pt))

// ---- Hardware ----
Arduino_GFX *gfx = nullptr;
SPIClass sharedSPI(VSPI);   // display não tem touch pra dividir o barramento, mas mantém padrão do bring-up
WiFiManager g_wifi;
Preferences g_prefs;

// ---- Botão físico (BOOT) ----
static bool     g_bootDown = false;
static uint32_t g_bootDownMs = 0;
static bool     g_bootLongFired = false;

// ---- Estado da aplicação ----
enum State {
  ST_BOOT, ST_PROVISION, ST_LOADING, ST_MAIN, ST_SETTINGS, ST_ABOUT, ST_ERROR
};
static State g_state = ST_BOOT;
static State g_pending = ST_BOOT;
static bool  g_dirty = false;
static void request_state(State s) { g_pending = s; g_dirty = true; }

// ---- Dados ----
static UsageData   g_usage = {};
static ModelStatus g_status = {true, true, true, true, false};

// ---- Modelos sondados (1 por ciclo, rotativo) ----
#define NMODELS 4
struct ModelInfo { const char *name; const char *id; ProbeResult pr; uint32_t atMs; };
static ModelInfo g_models[NMODELS] = {
  {"Haiku",  "claude-haiku-4-5-20251001", {0, 0}, 0},
  {"Sonnet", "claude-sonnet-5",           {0, 0}, 0},
  {"Opus",   "claude-opus-4-8",           {0, 0}, 0},
  {"Fable",  "claude-fable-5",            {0, 0}, 0},
};
static int g_probeIdx = 0;
static const int MODEL_CENTERS[NMODELS] = {44, 121, 199, 276};  // centros dos mascotes em 320px

// ---- Tokens por sessao (vindos do bridge via POST /tokens) ----
struct TokenStats { long long tin, tout, cache; int sessions; uint32_t atMs; };
static TokenStats g_tok = {0, 0, 0, 0, 0};
#define TOK_FRESH_MS (15UL * 60UL * 1000UL)

// ---- Token / segurança ----
static EncryptedBlob g_blob;
static bool g_hasToken = false;              // existe blob salvo no NVS
static char g_token[200] = {0};              // token decifrado (só em RAM)
static char g_savedPin[PIN_LEN + 1] = {0};   // PIN salvo em claro na NVS (decrypt automático no boot)
static int  g_pinAttempts = 0;               // tentativas erradas (persistido)
static uint32_t g_lockoutUntil = 0;          // millis até liberar nova tentativa
static uint32_t g_settingsUnlockedUntil = 0; // millis até expirar sessão de /settings desbloqueado
static bool g_timeInit = false;
static bool g_forceWifi = false;             // "Configurar WiFi" pediu reconfiguração
static bool g_forceToken = false;            // "Trocar token" pediu novo token

// ---- Refresh em background ----
static bool g_wantRefresh = false;        // botão de refresh pediu atualização
static bool g_refreshing = false;         // busca em andamento
static bool g_lastFetchOk = true;         // último fetch deu certo?
static uint32_t g_lastOkMs = 0;           // millis do último sucesso (p/ "atualizado há Xs")
static lv_obj_t *g_hdrStatus = nullptr;   // texto de status no cabeçalho do dashboard

// ---- Brilho ----
static const uint8_t BRI_LEVELS[3] = {60, 160, 255};
static int g_briIdx = 1;

static uint32_t g_lastPollMs = 0;         // millis do último poll (p/ barra de refresh)
static int g_pollSec = DEFAULT_POLL_SEC;  // intervalo de atualização (config, NVS)
static int g_tzOffset = -3;               // fuso GMT (horas), config NVS
static int g_slideSec = 0;                // slideshow: 0=off, 5/10/15/30s (config, NVS)
static int g_heatMode = 3;                // 0=hoje 1=7d 2=30d 3=tudo (config, NVS)
static uint32_t g_lastTouchMs = 0;        // ultimo toque (pausa o slideshow)
static uint32_t g_lastSlideMs = 0;

// ---- Histórico (ring buffer; persistido em LittleFS) ----
#define HIST_MAX 160
struct Sample { uint32_t t; uint8_t h5; uint8_t d7; };   // t = epoch (0 = relógio não sincronizado)
static Sample g_hist[HIST_MAX];
static int g_histN = 0;
static int g_histHead = 0;
static float g_hourBurn[24] = {0};   // consumo por hora do dia (todo o tempo)
static float g_lastH5 = -1.0f;       // última utilização 5h (delta do heatmap)

// ---- Heatmap por dia (para o filtro hoje/7d/30d) ----
#define NDAYS 31
struct DayHeat { uint32_t day; float burn[24]; };   // day = dias locais desde epoch
static DayHeat g_days[NDAYS];
static int g_dayN = 0;

// ---- Mascotes Clawd oficiais (pagina de modelos; humor por status) ----
// mood: 0=nunca sondado, 1=ok, 2=limitado(429), 3=erro/incidente, 4=n/d(404)
struct Mascot { lv_obj_t *cont, *img, *lid[2], *drop; int baseY, mood; };
static Mascot g_masc[NMODELS];
static int g_mascN = 0;
static lv_point_precise_t g_mXPts[NMODELS][4][2];   // olhos em X (mood 3)

// ---- Ponteiros de UI do dashboard (zerados a cada build de ST_MAIN) ----
#define NTILES 4
#define NSEG 18                       // segmentos do medidor de janela
struct DashUI {
  lv_obj_t *tv, *tile[NTILES], *dots[NTILES];
  lv_obj_t *refBar;
  // agora (overview + reset mesclados)
  lv_obj_t *agChip, *agPct5, *agCd5, *agAt5;
  lv_obj_t *agPct7, *agCd7, *agAt7, *agTok;
  lv_obj_t *seg5[NSEG], *seg7[NSEG];  // medidores segmentados
  // modelos
  lv_obj_t *mChip[NMODELS], *incident;
  // tendência da janela 5h (linhas custom)
  lv_obj_t *trHist, *trProj, *trDot, *trCap, *trT0, *trT1;
  // ritmo por hora
  lv_obj_t *heat[24], *heatBtn[4];
};
static DashUI g_ui;
static int g_curTile = 0;

// pontos das linhas do gráfico de tendência (precisam persistir)
static lv_point_precise_t g_trPts[HIST_MAX];
static lv_point_precise_t g_trProjPts[2];

// ---- Forward declarations ----
static void render_state();
static void refresh_ui_values();
static void dash_tick();
static void set_hdr_status();
static void apply_tz();
static void ui_provision();
static void ui_loading(const char *sub);
static void ui_main();
static void ui_settings();
static void ui_message(const char *title, const char *sub, uint32_t color);
static void start_data_web();
static void apply_setting_action(int act);
static void trend_redraw();
static void heat_redraw();
static void update_tok_row();
static void show_moment(int win, int thr);
static void moment_tick();
static void moment_close();

// ============================================================
// Pipeline de display (validado no bring-up) — sem Canvas, LVGL desenha
// direto no painel via draw16bitRGBBitmap (render parcial, sem PSRAM).
// ============================================================
static void disp_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
  uint32_t w = area->x2 - area->x1 + 1;
  uint32_t h = area->y2 - area->y1 + 1;
  gfx->draw16bitRGBBitmap(area->x1, area->y1, (uint16_t *)px_map, w, h);
  lv_disp_flush_ready(disp);
}

// ---- Botão físico BOOT (clique curto = próxima tela; longo = ajustes) ----
static void boot_button_poll() {
  bool down = (digitalRead(CFG_BOOT_PIN) == LOW);
  uint32_t now = millis();
  if (down && !g_bootDown) {
    g_bootDown = true; g_bootDownMs = now; g_bootLongFired = false;
  } else if (down && g_bootDown) {
    if (!g_bootLongFired && now - g_bootDownMs > BOOT_LONGPRESS_MS) {
      g_bootLongFired = true;
      if (g_state == ST_MAIN) request_state(ST_SETTINGS);
      else if (g_state == ST_SETTINGS) request_state(ST_ABOUT);
    }
  } else if (!down && g_bootDown) {
    g_bootDown = false;
    if (!g_bootLongFired) {                   // clique curto
      if (g_state == ST_MAIN) {
        int next = (g_curTile + 1) % NTILES;
        if (g_ui.tv) lv_tileview_set_tile_by_index(g_ui.tv, next, 0, LV_ANIM_ON);
      } else if (g_state == ST_SETTINGS || g_state == ST_ABOUT) {
        request_state(ST_MAIN);
      }
    }
  }
}

// ============================================================
// Helpers de UI
// ============================================================
static lv_obj_t *mklabel(lv_obj_t *p, const char *txt, const lv_font_t *font, uint32_t color) {
  lv_obj_t *l = lv_label_create(p);
  lv_label_set_text(l, txt);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
  return l;
}
static void no_box(lv_obj_t *o) {
  lv_obj_set_style_bg_opa(o, 0, 0);
  lv_obj_set_style_border_width(o, 0, 0);
  lv_obj_set_style_pad_all(o, 0, 0);
  lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
}
// Botão pílula com label centralizado; user_data leva o State alvo (nav_cb).
static lv_obj_t *mkbtn(lv_obj_t *p, const char *txt, const lv_font_t *font,
                       uint32_t bg, uint32_t fg) {
  lv_obj_t *b = lv_button_create(p);
  lv_obj_set_style_bg_color(b, lv_color_hex(bg), 0);
  lv_obj_set_style_radius(b, 10, 0);
  lv_obj_set_style_shadow_width(b, 0, 0);
  lv_obj_center(mklabel(b, txt, font, fg));
  return b;
}
static uint32_t pct_color(float p) {
  if (p < 70.0f) return C_OK;
  if (p < 90.0f) return C_WARN;
  return C_BAD;
}
// gradiente contínuo verde -> âmbar -> vermelho conforme o uso cresce
static lv_color_t grad_color(float p) {
  if (p < 0) p = 0; if (p > 100) p = 100;
  if (p <= 50.0f)
    return lv_color_mix(lv_color_hex(C_WARN), lv_color_hex(C_OK), (uint8_t)(p * 255.0f / 50.0f));
  return lv_color_mix(lv_color_hex(C_BAD), lv_color_hex(C_WARN), (uint8_t)((p - 50.0f) * 255.0f / 50.0f));
}
// acende os segmentos do medidor; acesos ganham a cor do gradiente,
// apagados ficam no trilho escuro
static void set_meter(lv_obj_t **seg, float pct) {
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
// "reseta em 1h 23m" / "2d 4h" / "agora" / "--" (relógio não sincronizado)
static void fmt_eta(uint32_t epoch, char *out, int sz) {
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
static void fmt_clock(uint32_t epoch, char *out, int sz) {
  if (epoch == 0 || time(nullptr) < 1000000000L) { strlcpy(out, "--:--", sz); return; }
  time_t t = (time_t)epoch; struct tm tmv;
  localtime_r(&t, &tmv);
  strftime(out, sz, "%a %H:%M", &tmv);
}
static void fmt_hm(uint32_t epoch, char *out, int sz) {
  if (epoch == 0 || time(nullptr) < 1000000000L) { strlcpy(out, "--:--", sz); return; }
  time_t t = (time_t)epoch; struct tm tmv;
  localtime_r(&t, &tmv);
  strftime(out, sz, "%H:%M", &tmv);
}
// 1234 -> "1.2k", 2345678 -> "2.3M"
static void fmt_tok(long long v, char *out, int sz) {
  if (v >= 100000000LL)     snprintf(out, sz, "%lldM", v / 1000000LL);
  else if (v >= 1000000LL)  snprintf(out, sz, "%.1fM", v / 1e6);
  else if (v >= 10000LL)    snprintf(out, sz, "%lldk", v / 1000LL);
  else if (v >= 1000LL)     snprintf(out, sz, "%.1fk", v / 1e3);
  else                      snprintf(out, sz, "%lld", v);
}

// ============================================================
// NVS / persistência
// ============================================================
static void load_persisted() {
  g_prefs.begin(NVS_NAMESPACE, false);
  size_t n = g_prefs.getBytesLength("blob");
  if (n == sizeof(EncryptedBlob)) {
    g_prefs.getBytes("blob", &g_blob, sizeof(EncryptedBlob));
    g_hasToken = true;
  }
  g_pinAttempts = g_prefs.getInt("pinatt", 0);
  String p = g_prefs.getString("pin", "");
  strlcpy(g_savedPin, p.c_str(), sizeof(g_savedPin));
  g_briIdx = g_prefs.getInt("bri", 1);
  if (g_briIdx < 0 || g_briIdx > 2) g_briIdx = 1;
  g_pollSec = g_prefs.getInt("poll", DEFAULT_POLL_SEC);
  if (g_pollSec < MIN_POLL_SEC || g_pollSec > MAX_POLL_SEC) g_pollSec = DEFAULT_POLL_SEC;
  g_tzOffset = g_prefs.getInt("tz", -3);
  if (g_tzOffset < -12 || g_tzOffset > 14) g_tzOffset = -3;
  g_slideSec = g_prefs.getInt("slide", 0);
  if (g_slideSec != 0 && g_slideSec != 5 && g_slideSec != 10 &&
      g_slideSec != 15 && g_slideSec != 30) g_slideSec = 0;
  g_heatMode = g_prefs.getInt("heatm", 3);
  if (g_heatMode < 0 || g_heatMode > 3) g_heatMode = 3;
  g_lang = g_prefs.getInt("lang", 0) ? 1 : 0;
}
static void save_blob() { g_prefs.putBytes("blob", &g_blob, sizeof(EncryptedBlob)); }
static void save_attempts() { g_prefs.putInt("pinatt", g_pinAttempts); }
static void apply_brightness() { ledcWrite(TFT_BL, BRI_LEVELS[g_briIdx]); }

static void factory_reset() {
  g_prefs.clear();              // apaga blob, pin, pinatt, bri do namespace claude
  g_wifi.forgetAll();
  g_hasToken = false;
  g_token[0] = 0;
  g_savedPin[0] = 0;
  g_pinAttempts = 0;
  Serial.println("[RESET] tudo apagado");
}

// ============================================================
// WebServer: dados (bridge de tokens) — sempre ativo no dashboard
// ============================================================
static WebServer *g_web = nullptr;

static void stop_web() { if (g_web) { g_web->stop(); delete g_web; g_web = nullptr; } }

static bool g_mdnsUp = false;
static void ensure_mdns() {
  if (g_mdnsUp || !g_wifi.isConnected()) return;
  if (MDNS.begin("claude-stick")) {
    MDNS.addService("http", "tcp", 80);
    g_mdnsUp = true;
    Serial.println("[MDNS] claude-stick.local");
  }
}

static void anim_opa_cb(void *o, int32_t v) { lv_obj_set_style_opa((lv_obj_t *)o, (lv_opa_t)v, 0); }

// Clawd oficial (pixel-art) com "respiração" — substitui o antigo sol de raios.
static lv_obj_t *build_claude_mark(lv_obj_t *parent) {
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

// ---- páginas HTML ----
#define WEB_CSS \
  ":root{--bg:#0F0F12;--card:#1A1A20;--bd:#30303A;--tx:#F2F0EC;--mut:#8C8C98;--cor:#D97757}" \
  "*{box-sizing:border-box}" \
  "body{margin:0;background:var(--bg);color:var(--tx);font-family:-apple-system,Segoe UI,Roboto,sans-serif;" \
  "display:flex;min-height:100vh;align-items:center;justify-content:center}" \
  ".card{background:var(--card);border:1px solid var(--bd);border-radius:16px;padding:26px;max-width:520px;width:92%}" \
  "h1{font-size:19px;margin:0 0 6px;display:flex;align-items:center;gap:10px}" \
  "p{color:var(--mut);font-size:14px;line-height:1.5;margin:6px 0 14px}" \
  "textarea{width:100%;background:var(--bg);color:var(--tx);border:1px solid var(--bd);border-radius:10px;" \
  "padding:12px;font-family:ui-monospace,monospace;font-size:13px;min-height:96px;resize:vertical}" \
  "button{margin-top:14px;width:100%;background:var(--cor);color:#1A1A20;border:0;border-radius:10px;" \
  "padding:14px;font-size:16px;font-weight:700;cursor:pointer}" \
  ".spark{width:26px;height:26px;flex:0 0 auto}code,a{color:var(--cor)}"

#define WEB_SPARK \
  "<svg class=spark viewBox='0 0 100 100'><g stroke='#D97757' stroke-width='12' stroke-linecap='round'>" \
  "<line x1=50 y1=9 x2=50 y2=91/><line x1=9 y1=50 x2=91 y2=50/>" \
  "<line x1=21 y1=21 x2=79 y2=79/><line x1=79 y1=21 x2=21 y2=79/>" \
  "<line x1=34 y1=11 x2=66 y2=89/><line x1=66 y1=11 x2=34 y2=89/></g></svg>"

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
// Confere PIN contra g_blob, aplicando o mesmo lockout exponencial/wipe do
// desbloqueio de boot. Retorna true só quando o PIN bate (e zera as
// tentativas); preenche errMsg com mensagem pronta pra UI em caso de espera/
// erro/wipe. wipedOut (se não-nulo) sai true quando o limite de tentativas
// estourou e factory_reset() já rodou. tokenOut/tokenOutLen (se não-nulo)
// recebem o token decifrado — sem eles o decrypt só serve pra validar o PIN.
static bool verify_pin_or_lockout(const String &pin, char *errMsg, size_t errLen, bool *wipedOut = nullptr,
                                   char *tokenOut = nullptr, size_t tokenOutLen = 0) {
  if (wipedOut) *wipedOut = false;
  if (millis() < g_lockoutUntil) {
    int rem = (g_lockoutUntil - millis()) / 1000;
    snprintf(errMsg, errLen, TRS("Aguarde %ds", "Wait %ds"), rem);
    return false;
  }
  char tmp[200];
  char *dst = tokenOut ? tokenOut : tmp;
  size_t dstLen = tokenOut ? tokenOutLen : sizeof(tmp);
  if (decryptToken(g_blob, pin.c_str(), dst, dstLen)) {
    g_pinAttempts = 0; save_attempts();
    return true;
  }
  g_pinAttempts++; save_attempts();
  if (g_pinAttempts >= MAX_PIN_ATTEMPTS) {
    Serial.println("[PIN] limite estourado -> wipe");
    factory_reset();
    g_forceWifi = true; g_forceToken = true;
    if (wipedOut) *wipedOut = true;
    snprintf(errMsg, errLen, "%s",
             TRS("PIN errado demais vezes — tudo apagado. Configure de novo.",
                 "Wrong PIN too many times — everything wiped. Reconfigure."));
    request_state(ST_PROVISION);
    return false;
  }
  int wait = LOCKOUT_BASE_SEC * (1 << (g_pinAttempts - 1));
  if (wait > 3600) wait = 3600;
  g_lockoutUntil = millis() + (uint32_t)wait * 1000;
  snprintf(errMsg, errLen, TRS("PIN errado (%d/%d). Aguarde %ds", "Wrong PIN (%d/%d). Wait %ds"),
           g_pinAttempts, MAX_PIN_ATTEMPTS, wait);
  return false;
}

static bool settings_unlocked() { return millis() < g_settingsUnlockedUntil; }

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

static void start_data_web() {
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

// ============================================================
// Portal de configuração unificado (WiFi + token + PIN via navegador)
// Substitui as antigas telas touch ST_WIFI/ST_TOKEN/ST_PIN/ST_SETUP_PIN.
// Sem WiFi salvo -> AP (PROVISION_AP_SSID). Com WiFi -> STA + mDNS,
// PIN pedido a cada boot (mesmo modelo de segurança do app original).
// ============================================================
static lv_obj_t *g_provMsg = nullptr;
static bool s_provNeedWifi = false;
static bool s_provNeedToken = false;
static bool s_provApMode = false;

static String provision_page(bool needWifi, bool needToken, const char *errMsg) {
  String h = F("<!doctype html><html lang=pt><head><meta charset=utf-8>"
               "<meta name=viewport content='width=device-width,initial-scale=1'>"
               "<title>Claude Usage Stick</title><style>" WEB_CSS "</style></head><body><div class=card>"
               "<h1>" WEB_SPARK " Claude Usage Stick</h1>");
  if (errMsg && errMsg[0]) {
    h += F("<p style='color:#F87171'>"); h += errMsg; h += F("</p>");
  }
  h += F("<form method=POST action='/save'>");
  if (needWifi) {
    h += F("<p>Rede WiFi (2.4GHz):</p>"
           "<input name=ssid placeholder='nome da rede (SSID)' autocomplete=off autofocus "
           "style='width:100%;padding:12px;border-radius:10px;border:1px solid #30303A;"
           "background:#0F0F12;color:#F2F0EC;font-size:14px;margin-bottom:10px'>"
           "<input name=pass type=password placeholder='senha do WiFi' autocomplete=off "
           "style='width:100%;padding:12px;border-radius:10px;border:1px solid #30303A;"
           "background:#0F0F12;color:#F2F0EC;font-size:14px'>");
  }
  if (needToken) {
    h += F("<p>Token OAuth do Claude (<code>sk-ant-oat01-...</code>):</p>"
           "<textarea name=token placeholder='sk-ant-oat01-...' autocomplete=off></textarea>"
           "<p>Crie um PIN de 4 dígitos (vai pedir ele a cada vez que a placa ligar):</p>"
           "<input name=pin inputmode=numeric maxlength=4 placeholder='novo PIN' "
           "style='width:100%;padding:12px;border-radius:10px;border:1px solid #30303A;"
           "background:#0F0F12;color:#F2F0EC;font-size:14px;margin-bottom:10px'>"
           "<input name=pin2 inputmode=numeric maxlength=4 placeholder='confirme o PIN' "
           "style='width:100%;padding:12px;border-radius:10px;border:1px solid #30303A;"
           "background:#0F0F12;color:#F2F0EC;font-size:14px'>");
  } else {
    // Sem touch na tela pra corrigir um erro de digitação na hora — confirma
    // em dois campos, como no cadastro. Errar demais aqui apaga tudo (10
    // tentativas), então vale a pena checar antes de gastar uma tentativa.
    h += F("<p>Digite o PIN pra desbloquear:</p>"
           "<input name=pin inputmode=numeric maxlength=4 autofocus placeholder='PIN' "
           "style='width:100%;padding:12px;border-radius:10px;border:1px solid #30303A;"
           "background:#0F0F12;color:#F2F0EC;font-size:14px;margin-bottom:10px'>"
           "<input name=pin2 inputmode=numeric maxlength=4 placeholder='confirme o PIN' "
           "style='width:100%;padding:12px;border-radius:10px;border:1px solid #30303A;"
           "background:#0F0F12;color:#F2F0EC;font-size:14px'>");
  }
  h += F("<button type=submit>Salvar</button></form></div></body></html>");
  return h;
}
static String provision_done_page() {
  return F("<!doctype html><html lang=pt><head><meta charset=utf-8>"
           "<meta name=viewport content='width=device-width,initial-scale=1'>"
           "<title>Claude Usage Stick</title><style>" WEB_CSS "</style></head><body><div class=card>"
           "<h1>" WEB_SPARK " Pronto!</h1>"
           "<p>Configurado. O dashboard já está carregando na tela do gadget. Pode fechar esta página.</p>"
           "</div></body></html>");
}

static void handleProvisionGet() {
  g_web->send(200, "text/html; charset=utf-8", provision_page(s_provNeedWifi, s_provNeedToken, ""));
}

static bool s_provBusy = false;
static void handleProvisionPost() {
  // browser às vezes reenvia o POST (duplo-clique/retry) enquanto o primeiro
  // ainda está em andamento (ex.: esperando NTP) — sem essa trava, o
  // handleClient() dentro do laço de espera reentra aqui e dispara uma 2ª
  // chamada de fetchUsage() com o relógio ainda não sincronizado.
  if (s_provBusy) { g_web->send(503, "text/plain", "ocupado, aguarde..."); return; }
  s_provBusy = true;
  struct BusyGuard { ~BusyGuard() { s_provBusy = false; } } guard;

  if (s_provNeedWifi) {
    String ssid = g_web->arg("ssid"); ssid.trim();
    String pass = g_web->arg("pass");
    if (ssid.length() == 0) {
      g_web->send(200, "text/html; charset=utf-8", provision_page(true, s_provNeedToken, "Digite o nome da rede (SSID)."));
      return;
    }
    if (g_provMsg) { lv_label_set_text(g_provMsg, TRS("conectando WiFi...", "connecting WiFi...")); lv_refr_now(NULL); }
    bool ok = g_wifi.connectTo(ssid.c_str(), pass.c_str(), 15000);
    if (!ok) {
      g_web->send(200, "text/html; charset=utf-8",
                  provision_page(true, s_provNeedToken, "Falha ao conectar. Confira SSID/senha e tente de novo."));
      return;
    }
    s_provNeedWifi = false;
    ensure_mdns();
  }

  // TLS (setCACert) valida a data do certificado — sem hora certa a conexão
  // HTTPS falha silenciosamente (POST retorna -1) mesmo com token válido.
  if (!g_timeInit) {
    apply_tz();
    g_timeInit = true;
    if (g_provMsg) { lv_label_set_text(g_provMsg, TRS("sincronizando hora...", "syncing time...")); lv_refr_now(NULL); }
    uint32_t t0 = millis();
    while (time(nullptr) < 1700000000 && millis() - t0 < 15000) { delay(200); g_web->handleClient(); }
  }

  String pin = g_web->arg("pin"); pin.trim();

  if (s_provNeedToken) {
    String token = g_web->arg("token"); token.trim();
    String pin2 = g_web->arg("pin2"); pin2.trim();
    if (token.length() < 8) {
      g_web->send(200, "text/html; charset=utf-8", provision_page(false, true, "Token vazio ou muito curto."));
      return;
    }
    if (pin.length() != PIN_LEN || pin != pin2) {
      g_web->send(200, "text/html; charset=utf-8", provision_page(false, true, "PIN precisa ter 4 dígitos e bater nos dois campos."));
      return;
    }
    if (g_provMsg) { lv_label_set_text(g_provMsg, TRS("validando token...", "validating token...")); lv_refr_now(NULL); }
    UsageData tmp = {};
    bool ok = fetchUsage(token.c_str(), tmp);
    if (!ok) {
      String m = String("A API recusou o token (") + tmp.error + ").";
      g_web->send(200, "text/html; charset=utf-8", provision_page(false, true, m.c_str()));
      return;
    }
    if (!encryptToken(token.c_str(), pin.c_str(), g_blob)) {
      g_web->send(200, "text/html; charset=utf-8", provision_page(false, true, "Falha ao cifrar o token. Tente de novo."));
      return;
    }
    save_blob();
    g_prefs.putString("pin", pin);
    strlcpy(g_savedPin, pin.c_str(), sizeof(g_savedPin));
    strlcpy(g_token, token.c_str(), sizeof(g_token));
    g_usage = tmp;
    g_hasToken = true; g_forceToken = false;
    g_pinAttempts = 0; save_attempts();
    g_web->send(200, "text/html; charset=utf-8", provision_done_page());
    request_state(ST_LOADING);
    return;
  }

  // Desbloqueio: blob já existe, só confere o PIN
  {
    String pin2 = g_web->arg("pin2"); pin2.trim();
    if (pin != pin2) {
      g_web->send(200, "text/html; charset=utf-8",
                  provision_page(false, false, TRS("Os dois campos de PIN nao batem. Tente de novo.",
                                                    "The two PIN fields don't match. Try again.")));
      return;
    }
  }
  char err[80] = {0};
  bool wiped = false;
  if (verify_pin_or_lockout(pin, err, sizeof(err), &wiped, g_token, sizeof(g_token))) {
    g_prefs.putString("pin", pin);           // re-sincroniza pro boot automático seguinte
    strlcpy(g_savedPin, pin.c_str(), sizeof(g_savedPin));
    g_web->send(200, "text/html; charset=utf-8", provision_done_page());
    request_state(ST_LOADING);
  } else {
    // wipe já chamou request_state(ST_PROVISION) e setou g_forceWifi/
    // g_forceToken dentro do helper; nesse caso o formulário certo é o
    // completo (wifi+token), senão só o de PIN.
    g_web->send(200, "text/html; charset=utf-8", provision_page(wiped, wiped, err));
  }
}

static void start_provision_web() {
  stop_web();
  s_provNeedToken = (!g_hasToken || g_forceToken);
  s_provNeedWifi = (g_wifi.getSavedCount() == 0) || g_forceWifi;
  if (!s_provNeedWifi) {
    if (!g_wifi.autoConnect(WIFI_CONNECT_TIMEOUT_MS)) s_provNeedWifi = true;
  }
  if (s_provNeedWifi) {
    WiFi.mode(WIFI_AP);
    WiFi.softAP(PROVISION_AP_SSID);
    s_provApMode = true;
    Serial.printf("[PROV] AP '%s' IP=%s\n", PROVISION_AP_SSID, WiFi.softAPIP().toString().c_str());
  } else {
    s_provApMode = false;
    ensure_mdns();
    Serial.printf("[PROV] STA IP=%s\n", WiFi.localIP().toString().c_str());
  }
  g_web = new WebServer(80);
  g_web->on("/", HTTP_GET, handleProvisionGet);
  g_web->on("/save", HTTP_POST, handleProvisionPost);
  g_web->onNotFound([]() { g_web->sendHeader("Location", "/"); g_web->send(302, "text/plain", ""); });
  g_web->begin();
}

static void ui_provision() {
  start_provision_web();   // decide AP vs STA e sobe o servidor ANTES de montar os textos abaixo

  lv_obj_t *scr = lv_screen_active();

  lv_obj_t *mark = build_claude_mark(scr);
  lv_obj_align(mark, LV_ALIGN_TOP_MID, 0, 6);

  lv_obj_t *cap = mklabel(scr, TRS("Configure pelo navegador:", "Configure via browser:"),
                          &lv_font_montserrat_14, C_MUTED);
  lv_obj_align(cap, LV_ALIGN_TOP_MID, 0, 84);

  String addr;
  if (s_provApMode) addr = String(PROVISION_AP_SSID) + " -> 192.168.4.1";
  else addr = String("http://") + WiFi.localIP().toString();
  lv_obj_t *ip = mklabel(scr, addr.c_str(), &lv_font_montserrat_18, C_ACCENT);
  lv_obj_align(ip, LV_ALIGN_TOP_MID, 0, 104);

  const char *hint = s_provApMode
    ? TRS("conecte nessa rede WiFi e abra o endereco acima", "join that WiFi network and open the address above")
    : TRS("abra esse endereco num PC/celular na MESMA rede", "open this address on a PC/phone on the SAME network");
  lv_obj_t *h = mklabel(scr, hint, &lv_font_montserrat_12, C_MUTED);
  lv_obj_set_width(h, SCREEN_WIDTH - 20);
  lv_obj_set_style_text_align(h, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_long_mode(h, LV_LABEL_LONG_WRAP);
  lv_obj_align(h, LV_ALIGN_TOP_MID, 0, 134);

  g_provMsg = mklabel(scr, TRS("aguardando...", "waiting..."), &lv_font_montserrat_12, C_FAINT);
  lv_obj_align(g_provMsg, LV_ALIGN_BOTTOM_MID, 0, -10);
}

// ============================================================
// Tela: loading / mensagem
// ============================================================
static void ui_message(const char *title, const char *sub, uint32_t color) {
  lv_obj_t *scr = lv_screen_active();
  lv_obj_t *t = mklabel(scr, title, &lv_font_montserrat_24, color);
  lv_obj_align(t, LV_ALIGN_CENTER, 0, -16);
  if (sub && sub[0]) {
    lv_obj_t *s = mklabel(scr, sub, &lv_font_montserrat_16, C_MUTED);
    lv_obj_align(s, LV_ALIGN_CENTER, 0, 20);
  }
}
static void ui_loading(const char *sub) {
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
// Histórico / heatmap
// ============================================================
static void hist_push(float h5, float d7) {
  time_t now = time(nullptr);
  g_hist[g_histHead].t  = (now > 1000000000L) ? (uint32_t)now : 0;
  g_hist[g_histHead].h5 = (uint8_t)(h5 + 0.5f);
  g_hist[g_histHead].d7 = (uint8_t)(d7 + 0.5f);
  g_histHead = (g_histHead + 1) % HIST_MAX;
  if (g_histN < HIST_MAX) g_histN++;
}
static int hist_idx(int i) { return (g_histHead - g_histN + i + HIST_MAX * 2) % HIST_MAX; }

// dia local (dias desde epoch, corrigido pelo fuso configurado)
static uint32_t day_key() {
  time_t now = time(nullptr);
  if (now < 1000000000L) return 0;
  return (uint32_t)((now + (long)g_tzOffset * 3600) / 86400);
}
// retorna o índice do dia em g_days (cria/rotaciona se preciso) — retorna int
// em vez de DayHeat* para não tropeçar nos protótipos automáticos do .ino
static int day_slot(uint32_t dk) {
  for (int i = 0; i < g_dayN; i++)
    if (g_days[i].day == dk) return i;
  if (g_dayN == NDAYS) {              // descarta o mais antigo (array cronológico)
    memmove(&g_days[0], &g_days[1], sizeof(DayHeat) * (NDAYS - 1));
    g_dayN--;
  }
  int i = g_dayN++;
  g_days[i].day = dk;
  memset(g_days[i].burn, 0, sizeof(g_days[i].burn));
  return i;
}

// Heatmap: atribui o consumo (Δ utilização 5h) à hora do dia local.
static void accumulate_heat(float h5) {
  time_t now = time(nullptr);
  if (g_lastH5 >= 0 && now > 1000000000L) {
    float d = h5 - g_lastH5;
    if (d > 0 && d < 100) {
      struct tm tmv; localtime_r(&now, &tmv);
      g_hourBurn[tmv.tm_hour] += d;
      uint32_t dk = day_key();
      if (dk) g_days[day_slot(dk)].burn[tmv.tm_hour] += d;
    }
  }
  g_lastH5 = h5;
}

// soma o heatmap conforme o período escolhido (0=hoje 1=7d 2=30d 3=tudo)
static void heat_mode_data(int mode, float out[24]) {
  memset(out, 0, sizeof(float) * 24);
  if (mode == 3) { memcpy(out, g_hourBurn, sizeof(float) * 24); return; }
  uint32_t today = day_key();
  if (!today) return;
  uint32_t minDay = (mode == 0) ? today : (mode == 1) ? today - 6 : today - 29;
  for (int i = 0; i < g_dayN; i++) {
    if (g_days[i].day < minDay || g_days[i].day > today) continue;
    for (int h = 0; h < 24; h++) out[h] += g_days[i].burn[h];
  }
}

// Persistência do histórico/heatmap em LittleFS (sobrevive reboot).
// v2 = v1 + heatmap por dia. Carrega v1 antigo para não perder histórico.
#define HIST_MAGIC_V1 0xC1A0DE01
#define HIST_MAGIC_V2 0xC1A0DE02
#define HIST_MAX_V1 120
struct HistFileV1 { uint32_t magic; int n, head; Sample hist[HIST_MAX_V1]; float hourBurn[24]; float lastH5; };
struct HistFileV2 {
  uint32_t magic; int n, head; Sample hist[HIST_MAX]; float hourBurn[24]; float lastH5;
  int dayN; DayHeat days[NDAYS];
};
static void save_history() {
  File f = LittleFS.open("/hist.bin", "w");
  if (!f) return;
  static HistFileV2 hf;                       // grande demais p/ stack
  hf.magic = HIST_MAGIC_V2; hf.n = g_histN; hf.head = g_histHead;
  memcpy(hf.hist, g_hist, sizeof(g_hist));
  memcpy(hf.hourBurn, g_hourBurn, sizeof(g_hourBurn));
  hf.lastH5 = g_lastH5;
  hf.dayN = g_dayN;
  memcpy(hf.days, g_days, sizeof(g_days));
  f.write((uint8_t *)&hf, sizeof(hf));
  f.close();
}
static void load_history() {
  File f = LittleFS.open("/hist.bin", "r");
  if (!f) return;
  uint32_t magic = 0;
  f.read((uint8_t *)&magic, sizeof(magic));
  f.seek(0);
  if (magic == HIST_MAGIC_V2) {
    static HistFileV2 hf;
    if (f.read((uint8_t *)&hf, sizeof(hf)) == (int)sizeof(hf)) {
      g_histN = hf.n; g_histHead = hf.head;
      memcpy(g_hist, hf.hist, sizeof(g_hist));
      memcpy(g_hourBurn, hf.hourBurn, sizeof(g_hourBurn));
      g_lastH5 = hf.lastH5;
      g_dayN = (hf.dayN >= 0 && hf.dayN <= NDAYS) ? hf.dayN : 0;
      memcpy(g_days, hf.days, sizeof(g_days));
    }
  } else if (magic == HIST_MAGIC_V1) {
    static HistFileV1 hf;
    if (f.read((uint8_t *)&hf, sizeof(hf)) == (int)sizeof(hf)) {
      int n = (hf.n > HIST_MAX_V1) ? HIST_MAX_V1 : hf.n;
      for (int i = 0; i < n; i++)
        g_hist[i] = hf.hist[(hf.head - n + i + HIST_MAX_V1 * 2) % HIST_MAX_V1];
      g_histN = n; g_histHead = n % HIST_MAX;
      memcpy(g_hourBurn, hf.hourBurn, sizeof(g_hourBurn));
      g_lastH5 = hf.lastH5;
      Serial.println("[HIST] migrado v1 -> v2");
    }
  }
  f.close();
}

// ============================================================
// Dashboard — helpers visuais
// ============================================================
static uint32_t status_color(const char *s) {
  if (!s || !s[0]) return C_MUTED;
  if (!strcmp(s, "rejected") || !strcmp(s, "rate_limited") || !strcmp(s, "exceeded")) return C_BAD;
  if (strstr(s, "warning")) return C_WARN;
  return C_OK;
}
static const char *overall_label(const char *s) {
  if (!s || !s[0]) return "--";
  if (!strcmp(s, "allowed")) return "OK";
  if (strstr(s, "warning"))  return TRS("ATENCAO", "WARNING");
  if (!strcmp(s, "rejected")) return TRS("BLOQUEADO", "BLOCKED");
  return s;
}

// label posicionado vazio (preenchido em refresh_ui_values/dash_tick)
static lv_obj_t *tlabel(lv_obj_t *p, const lv_font_t *f, uint32_t c, int x, int y) {
  lv_obj_t *l = lv_label_create(p);
  lv_obj_set_style_text_font(l, f, 0);
  lv_obj_set_style_text_color(l, lv_color_hex(c), 0);
  lv_label_set_text(l, "");
  lv_obj_set_pos(l, x, y);
  return l;
}
static lv_obj_t *tstatic(lv_obj_t *p, const char *txt, const lv_font_t *f, uint32_t c, int x, int y) {
  lv_obj_t *l = mklabel(p, txt, f, c);
  lv_obj_set_pos(l, x, y);
  return l;
}
static void tile_setup(lv_obj_t *t) {
  lv_obj_set_style_bg_opa(t, 0, 0);
  lv_obj_set_style_border_width(t, 0, 0);
  lv_obj_set_style_pad_all(t, 0, 0);
  lv_obj_clear_flag(t, LV_OBJ_FLAG_SCROLLABLE);
}
// card moderno: superfície arredondada SEM borda (estrutura por cor, não caixa)
static lv_obj_t *card(lv_obj_t *p, int x, int y, int w, int h) {
  lv_obj_t *c = lv_obj_create(p);
  lv_obj_set_pos(c, x, y); lv_obj_set_size(c, w, h);
  lv_obj_set_style_bg_color(c, lv_color_hex(C_SURFACE), 0);
  lv_obj_set_style_border_width(c, 0, 0);
  lv_obj_set_style_radius(c, 18, 0);
  lv_obj_set_style_pad_all(c, 14, 0);
  lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);
  return c;
}
// chip com fundo tintado + texto na cor (mais leve que chip sólido)
static lv_obj_t *mkchip(lv_obj_t *p, int x, int y) {
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
static void set_chip(lv_obj_t *o, const char *txt, uint32_t col) {
  if (!o) return;
  lv_color_t c = lv_color_hex(col);
  lv_obj_set_style_bg_color(o, lv_color_mix(c, lv_color_hex(C_BG), 60), 0);
  lv_obj_t *l = lv_obj_get_child(o, 0);
  if (l) { lv_label_set_text(l, txt[0] ? txt : "--"); lv_obj_set_style_text_color(l, c, 0); }
}
// peça retangular arredondada do mascote
static lv_obj_t *rrect(lv_obj_t *p, int x, int y, int w, int h, int r, uint32_t col) {
  lv_obj_t *o = lv_obj_create(p);
  lv_obj_set_pos(o, x, y); lv_obj_set_size(o, w, h);
  lv_obj_set_style_radius(o, r, 0);
  lv_obj_set_style_bg_color(o, lv_color_hex(col), 0);
  lv_obj_set_style_border_width(o, 0, 0);
  lv_obj_set_style_pad_all(o, 0, 0);
  lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  return o;
}
// humor do modelo: sonda real (HTTP) + incidentes do status.claude.com
static int model_mood(int i) {
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

// adereço pixel que identifica cada modelo (flutuando sobre a cabeça)
static void build_accessory(lv_obj_t *c, int model) {
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

// Mascote da pagina de modelos: Clawd oficial + humor + adereço.
static void build_model_mascot(lv_obj_t *parent, int cx, int i) {
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

static void model_chip(int i, char *out, size_t sz, uint32_t *col) {
  int c = g_models[i].pr.code;
  if (c == 0)             { strlcpy(out, "--", sz);        *col = C_MUTED; }
  else if (c == 200)      { snprintf(out, sz, "OK %.1fs", g_models[i].pr.ms / 1000.0f); *col = C_OK; }
  else if (c == 429)      { strlcpy(out, TRS("LIMITADO", "LIMITED"), sz); *col = C_WARN; }
  else if (c == 404)      { strlcpy(out, TRS("N/D", "N/A"), sz); *col = C_MUTED; }
  else if (c == 401 || c == 403) { strlcpy(out, "AUTH", sz); *col = C_BAD; }
  else if (c < 0)         { strlcpy(out, TRS("REDE", "NET"), sz); *col = C_BAD; }
  else                    { snprintf(out, sz, TRS("ERRO %d", "ERR %d"), c); *col = C_BAD; }
}

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
static void update_tok_row() {
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
static void dash_tick() {
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
struct MomentUI {
  lv_obj_t *scrim, *box, *img, *pct, *seg[NSEG];
  lv_obj_t *lid[2], *drop[2], *ring, *xline[4];
  int win, thr, fromPct;
  int boxY;
  uint32_t t0;
};
static MomentUI g_mo = {};
static uint32_t g_momentUntil = 0;
static int g_pendWin = -1, g_pendThr = 0;      // momento aguardando exibição
static uint8_t g_thrFired[2] = {0, 0};         // bits já disparados por janela
static float g_thrPrev[2] = {-1, -1};
static bool g_thrBase = false;
static lv_point_precise_t g_moXPts[4][2];      // olhos em X (KO)

// Detecta cruzamento de limiar após cada fetch. Baseline no 1º fetch (não
// dispara pelo que já estava acima); zera quando a janela reseta (queda >15pp).
static void check_thresholds() {
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

static void moment_close() {
  if (!g_mo.scrim) return;
  lv_obj_delete(g_mo.scrim);
  memset(&g_mo, 0, sizeof(g_mo));
}
static void moment_close_cb(lv_event_t *e) { (void)e; moment_close(); }

static void show_moment(int win, int thr) {
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
static void moment_tick() {
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
static void refresh_ui_values() {
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
static void set_hdr_status() {
  if (!g_hdrStatus) return;
  char buf[40]; uint32_t color;
  if (g_refreshing)        { strcpy(buf, TRS("atualizando...", "updating..."));      color = C_ACCENT; }
  else if (!g_lastFetchOk) { strcpy(buf, TRS("falha ao atualizar", "update failed")); color = C_BAD; }
  else {
    uint32_t s = (millis() - g_lastOkMs) / 1000;
    if (s < 60) snprintf(buf, sizeof(buf), TRS("atualizado ha %us", "updated %us ago"), (unsigned)s);
    else        snprintf(buf, sizeof(buf), TRS("atualizado ha %umin", "updated %um ago"), (unsigned)(s / 60));
    color = C_MUTED;
  }
  lv_label_set_text(g_hdrStatus, buf);
  lv_obj_set_style_text_color(g_hdrStatus, lv_color_hex(color), 0);
}
// Botão de refresh: só pede; a busca acontece em background no loop()
static void ui_main() {
  lv_obj_t *scr = lv_screen_active();
  lv_obj_set_style_bg_color(scr, lv_color_hex(C_BG), 0);

  start_data_web();

  // Header: Clawd + logotipo. Sem touch: sem botão de refresh/engrenagem —
  // BOOT curto muda de tile, BOOT longo abre Ajustes (ver boot_button_poll()).
  lv_obj_t *hIcon = lv_image_create(scr);
  lv_image_set_src(hIcon, &img_clawd_sm);
  lv_obj_set_pos(hIcon, 6, 4);
  lv_obj_t *hWord = lv_image_create(scr);
  lv_image_set_src(hWord, &img_wordmark);
  lv_obj_set_pos(hWord, 46, 6);

  g_hdrStatus = mklabel(scr, "", &lv_font_montserrat_12, C_MUTED);
  lv_obj_align(g_hdrStatus, LV_ALIGN_TOP_RIGHT, -4, 8);

  // Barra fina decrescente até o próximo refresh automático (só indicador).
  g_ui.refBar = lv_bar_create(scr);
  lv_obj_set_size(g_ui.refBar, SCREEN_WIDTH, 3);
  lv_obj_set_pos(g_ui.refBar, 0, 28);
  lv_bar_set_range(g_ui.refBar, 0, 1000);
  lv_bar_set_value(g_ui.refBar, 1000, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(g_ui.refBar, lv_color_hex(C_SURFACE), LV_PART_MAIN);
  lv_obj_set_style_bg_color(g_ui.refBar, lv_color_hex(C_ACCENT), LV_PART_INDICATOR);
  lv_obj_set_style_radius(g_ui.refBar, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(g_ui.refBar, 0, LV_PART_INDICATOR);
  lv_obj_clear_flag(g_ui.refBar, LV_OBJ_FLAG_CLICKABLE);

  // Telas (BOOT curto avança; ver boot_button_poll())
  g_ui.tv = lv_tileview_create(scr);
  lv_obj_set_pos(g_ui.tv, 0, 32);
  lv_obj_set_size(g_ui.tv, SCREEN_WIDTH, SCREEN_HEIGHT - 32 - 14);
  lv_obj_set_style_bg_opa(g_ui.tv, 0, 0);
  lv_obj_set_style_border_width(g_ui.tv, 0, 0);
  lv_obj_set_scrollbar_mode(g_ui.tv, LV_SCROLLBAR_MODE_OFF);
  for (int i = 0; i < NTILES; i++) {
    g_ui.tile[i] = lv_tileview_add_tile(g_ui.tv, i, 0, LV_DIR_HOR);
    tile_setup(g_ui.tile[i]);
  }
  build_tile_agora(g_ui.tile[0]);
  build_tile_models(g_ui.tile[1]);
  build_tile_trend(g_ui.tile[2]);
  build_tile_heat(g_ui.tile[3]);
  lv_obj_add_event_cb(g_ui.tv, on_tile_changed, LV_EVENT_VALUE_CHANGED, NULL);

  // Dots (objetos; o ativo vira pílula)
  for (int i = 0; i < NTILES; i++) {
    g_ui.dots[i] = lv_obj_create(scr);
    lv_obj_set_size(g_ui.dots[i], 8, 8);
    lv_obj_set_style_radius(g_ui.dots[i], 4, 0);
    lv_obj_set_style_bg_color(g_ui.dots[i], lv_color_hex(C_BORDER), 0);
    lv_obj_set_style_border_width(g_ui.dots[i], 0, 0);
    lv_obj_clear_flag(g_ui.dots[i], LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(g_ui.dots[i], LV_ALIGN_BOTTOM_MID, (int)((i - (NTILES - 1) / 2.0f) * 18), -4);
  }

  refresh_ui_values();
  on_tile_changed(NULL);
}

// ============================================================
// Tela: settings (lista rolável; linhas >=44px de toque)
// ============================================================
static bool g_wipeArmed = false;
static lv_obj_t *g_briLbl = nullptr, *g_wipeLbl = nullptr, *g_pollLbl = nullptr,
                *g_tzLbl = nullptr, *g_slideLbl = nullptr;
static const int POLL_OPTS[4] = {30, 60, 120, 300};
static const int TZ_OPTS[] = {-3, -4, -5, -6, -7, -8, -2, -1, 0, 1, 2, 3};
#define NTZ ((int)(sizeof(TZ_OPTS) / sizeof(TZ_OPTS[0])))

static void apply_setting_action(int act) {
  switch (act) {
    case 0: request_state(ST_LOADING); break;          // atualizar
    case 1: g_forceWifi = true; request_state(ST_PROVISION); break;   // reconfigurar wifi
    case 2: g_forceToken = true; request_state(ST_PROVISION); break;  // trocar token
    case 3:                                            // brilho
      g_briIdx = (g_briIdx + 1) % 3; g_prefs.putInt("bri", g_briIdx); apply_brightness();
      if (g_briLbl) {
        const char *n[3] = {TRS("baixo", "low"), TRS("medio", "medium"), TRS("alto", "high")};
        char m[40]; snprintf(m, sizeof(m), TRS(LV_SYMBOL_EYE_OPEN "  Brilho: %s",
                                               LV_SYMBOL_EYE_OPEN "  Brightness: %s"), n[g_briIdx]);
        lv_label_set_text(g_briLbl, m);
      }
      break;
    case 4:                                            // apagar tudo (confirmação no form web)
      factory_reset();
      g_forceWifi = true; g_forceToken = true;
      request_state(ST_PROVISION);
      break;
    case 6: {                                          // intervalo de atualização
      int idx = 0;
      for (int i = 0; i < 4; i++) if (POLL_OPTS[i] == g_pollSec) idx = i;
      g_pollSec = POLL_OPTS[(idx + 1) % 4];
      g_prefs.putInt("poll", g_pollSec);
      if (g_pollLbl) {
        char m[40];
        if (g_pollSec < 60) snprintf(m, sizeof(m), TRS(LV_SYMBOL_LOOP "  Atualizar: %ds",
                                                       LV_SYMBOL_LOOP "  Refresh: %ds"), g_pollSec);
        else snprintf(m, sizeof(m), TRS(LV_SYMBOL_LOOP "  Atualizar: %dmin",
                                        LV_SYMBOL_LOOP "  Refresh: %dmin"), g_pollSec / 60);
        lv_label_set_text(g_pollLbl, m);
      }
      break;
    }
    case 7: {                                          // fuso horário (GMT)
      int idx = 0;
      for (int i = 0; i < NTZ; i++) if (TZ_OPTS[i] == g_tzOffset) idx = i;
      g_tzOffset = TZ_OPTS[(idx + 1) % NTZ];
      g_prefs.putInt("tz", g_tzOffset);
      apply_tz();
      if (g_tzLbl) {
        char m[40];
        snprintf(m, sizeof(m), TRS(LV_SYMBOL_GPS "  Fuso: GMT%+d",
                                   LV_SYMBOL_GPS "  Timezone: GMT%+d"), g_tzOffset);
        lv_label_set_text(g_tzLbl, m);
      }
      break;
    }
    case 8: {                                          // slideshow: off -> 5 -> 10 -> 15 -> 30 -> off
      static const int SL[5] = {0, 5, 10, 15, 30};
      int idx = 0;
      for (int i = 0; i < 5; i++) if (SL[i] == g_slideSec) idx = i;
      g_slideSec = SL[(idx + 1) % 5];
      g_prefs.putInt("slide", g_slideSec);
      if (g_slideLbl) {
        char m[48];
        if (g_slideSec) snprintf(m, sizeof(m), LV_SYMBOL_PLAY "  Slideshow: %ds", g_slideSec);
        else            snprintf(m, sizeof(m), "%s", TRS(LV_SYMBOL_PLAY "  Slideshow: desligado",
                                                         LV_SYMBOL_PLAY "  Slideshow: off"));
        lv_label_set_text(g_slideLbl, m);
      }
      break;
    }
    case 9:                                            // idioma / language
      g_lang ^= 1;
      g_prefs.putInt("lang", g_lang);
      request_state(ST_SETTINGS);                      // redesenha tudo no novo idioma
      break;
  }
}
static void add_info_row(lv_obj_t *p, const char *txt, uint32_t fg) {
  lv_obj_t *b = lv_obj_create(p);
  lv_obj_set_size(b, 444, 40);
  lv_obj_set_style_bg_color(b, lv_color_hex(C_SURFACE), 0);
  lv_obj_set_style_radius(b, 12, 0);
  lv_obj_set_style_border_width(b, 0, 0);
  lv_obj_t *l = mklabel(b, txt, &lv_font_montserrat_16, fg);
  lv_obj_align(l, LV_ALIGN_LEFT_MID, 8, 0);
}
// Sem touch: tela é só leitura. Ajustes de verdade são feitos via
// http://claude-stick.local/settings (BOOT longo abre esta tela, BOOT
// curto volta pro dashboard — ver boot_button_poll()).
static void ui_settings() {
  lv_obj_t *scr = lv_screen_active();
  start_data_web();
  lv_obj_t *title = mklabel(scr, TRS("Ajustes (via navegador)", "Settings (via browser)"),
                            &lv_font_montserrat_18, C_TEXT);
  lv_obj_align(title, LV_ALIGN_TOP_LEFT, 14, 10);

  String url = String("claude-stick.local/settings");
  lv_obj_t *hint = mklabel(scr, url.c_str(), &lv_font_montserrat_16, C_ACCENT);
  lv_obj_align(hint, LV_ALIGN_TOP_LEFT, 14, 38);

  lv_obj_t *lst = lv_obj_create(scr);
  lv_obj_set_pos(lst, 8, 70);
  lv_obj_set_size(lst, 464, 242);
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
  add_info_row(lst, pollTxt, C_TEXT);
  add_info_row(lst, slideTxt, C_TEXT);
  add_info_row(lst, tzTxt, C_TEXT);
  add_info_row(lst, bri, C_TEXT);
  add_info_row(lst, TRS(LV_SYMBOL_LIST "  Idioma: Portugues", LV_SYMBOL_LIST "  Language: English"), C_TEXT);

  lv_obj_t *foot = mklabel(scr, TRS("BOOT curto: volta  \xE2\x80\xA2  BOOT longo: sobre",
                                    "BOOT short: back  \xE2\x80\xA2  BOOT long: about"),
                           &lv_font_montserrat_12, C_FAINT);
  lv_obj_align(foot, LV_ALIGN_BOTTOM_MID, 0, -6);
}

// ============================================================
// Tela: sobre / about
// ============================================================
static void ui_about() {
  lv_obj_t *scr = lv_screen_active();
  start_data_web();

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

  lv_obj_t *foot = mklabel(scr, TRS("BOOT curto: volta ao dashboard", "BOOT short: back to dashboard"),
                           &lv_font_montserrat_12, C_FAINT);
  lv_obj_align(foot, LV_ALIGN_BOTTOM_MID, 0, -6);
}

// ============================================================
// Render do estado atual
// ============================================================
static void render_state() {
  g_state = g_pending;
  stop_web();                                 // cada tela sobe o servidor que precisa
  moment_close();                             // overlay vive em lv_layer_top
  lv_obj_clean(lv_layer_top());
  // invalida ponteiros vivos antes de destruir a tela antiga
  memset(&g_ui, 0, sizeof(g_ui));
  g_mascN = 0;
  g_provMsg = nullptr;
  g_hdrStatus = nullptr;
  g_briLbl = g_pollLbl = g_tzLbl = g_slideLbl = nullptr;

  lv_obj_clean(lv_screen_active());
  lv_obj_set_style_bg_color(lv_screen_active(), lv_color_hex(C_BG), 0);
  lv_obj_set_style_bg_opa(lv_screen_active(), LV_OPA_COVER, 0);

  switch (g_state) {
    case ST_PROVISION: ui_provision(); break;
    case ST_LOADING:   ui_loading(g_wifi.isConnected() ? g_wifi.getSSID().c_str()
                                                       : TRS("conectando WiFi", "connecting WiFi")); break;
    case ST_MAIN:      ui_main(); break;
    case ST_SETTINGS:  ui_settings(); break;
    case ST_ABOUT:     ui_about(); break;
    case ST_ERROR:     ui_message(TRS("Falha", "Failed"),
                                  g_usage.error[0] ? g_usage.error : TRS("sem dados", "no data"), C_BAD); break;
    default: break;
  }
}

// ============================================================
// Tempo (NTP) e ciclo de dados
// ============================================================
static void apply_tz() { configTime(g_tzOffset * 3600, 0, NTP_SERVER_1, NTP_SERVER_2); }
static void ensure_time() {
  if (g_timeInit || !g_wifi.isConnected()) return;
  apply_tz();
  g_timeInit = true;
  Serial.println("[NTP] sync iniciado");
}

// Sonda o próximo modelo da rotação.
static void probe_next_model() {
  int mi = g_probeIdx % NMODELS;
  g_probeIdx++;
  probeModel(g_token, g_models[mi].id, g_models[mi].pr);
  g_models[mi].atMs = millis();
}

// Primeiro load (mostra a tela de carregamento). Vai p/ ST_MAIN ou ST_ERROR.
static void do_refresh() {
  ensure_time();
  bool ok = fetchUsage(g_token, g_usage);
  if (ok) {
    fetchModelStatus(g_status); g_lastOkMs = millis(); g_lastFetchOk = true;
    hist_push(g_usage.h5, g_usage.d7); accumulate_heat(g_usage.h5); save_history();
    check_thresholds();
    probe_next_model();
  } else g_lastFetchOk = false;
  g_lastPollMs = millis();
  request_state(ok ? ST_MAIN : ST_ERROR);
}

// Atualização EM BACKGROUND: não troca de tela; mantém o dashboard e os dados
// antigos se falhar. A chamada à API é bloqueante (~1-2s), então mostra
// "atualizando..." no cabeçalho durante a busca.
static void bg_refresh() {
  if (!g_wifi.isConnected()) g_wifi.autoConnect(WIFI_CONNECT_TIMEOUT_MS);
  ensure_time();
  g_refreshing = true; set_hdr_status(); lv_refr_now(NULL);
  UsageData u = {};
  bool ok = fetchUsage(g_token, u);
  bool rebuild = false;
  if (ok) {
    g_usage = u; g_lastOkMs = millis(); g_lastFetchOk = true;
    hist_push(u.h5, u.d7); accumulate_heat(u.h5); save_history();
    check_thresholds();
    int moodBefore[NMODELS];
    for (int i = 0; i < NMODELS; i++) moodBefore[i] = model_mood(i);
    fetchModelStatus(g_status);
    probe_next_model();
    for (int i = 0; i < NMODELS; i++)
      if (moodBefore[i] != model_mood(i)) rebuild = true;   // mascote muda de humor
  } else g_lastFetchOk = false;
  g_refreshing = false;
  g_lastPollMs = millis();
  if (rebuild) request_state(ST_MAIN);    // mascotes mudaram -> rebuild
  else refresh_ui_values();               // resto: in-place (preserva o tile atual)
}

// ============================================================
// setup / loop
// ============================================================
void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n=== Claude Usage Stick — CYD (sem touch) ===");

  // Display — ILI9341 via SPI compartilhado, sem Canvas (validado no bring-up)
  sharedSPI.begin(TFT_SCLK, TFT_MISO, TFT_MOSI);
  Arduino_DataBus *bus = new Arduino_HWSPI(TFT_DC, TFT_CS, TFT_SCLK, TFT_MOSI, TFT_MISO, &sharedSPI);
  gfx = new Arduino_ILI9341(bus, TFT_RST, TFT_ROTATION, false);
  if (!gfx->begin(SPI_FREQ)) { Serial.println("FATAL display"); while (1) delay(1000); }
  gfx->invertDisplay(true);
  gfx->fillScreen(0x0000);

  // Backlight via PWM (brilho ajustável)
  ledcAttach(TFT_BL, 5000, 8);

  // Botão físico BOOT
  pinMode(CFG_BOOT_PIN, INPUT_PULLUP);

  // LVGL — render parcial em RAM comum (sem PSRAM nessa placa)
  lv_init();
  lv_tick_set_cb([]() -> uint32_t { return millis(); });
  const uint32_t BUF_LINES = 20;
  uint32_t bufSize = SCREEN_WIDTH * BUF_LINES * sizeof(lv_color_t);
  lv_color_t *buf1 = (lv_color_t *)malloc(bufSize);
  lv_color_t *buf2 = (lv_color_t *)malloc(bufSize);
  if (!buf1 || !buf2) { Serial.println("FATAL buffer LVGL"); while (1) delay(1000); }
  lv_display_t *disp = lv_display_create(SCREEN_WIDTH, SCREEN_HEIGHT);
  lv_display_set_flush_cb(disp, disp_flush_cb);
  lv_display_set_buffers(disp, buf1, buf2, bufSize, LV_DISPLAY_RENDER_MODE_PARTIAL);

  load_persisted();
  apply_brightness();

  if (!LittleFS.begin(true)) Serial.println("LittleFS: falhou");
  else load_history();

  g_wifi.begin();

  // Boot automático: com WiFi salvo conectando e token+PIN batendo, decifra
  // sozinho e cai direto no dashboard. Qualquer coisa fora disso (sem WiFi
  // salvo, falha de conexão, sem token/PIN ainda) cai no portal de
  // provisionamento como antes — ver ui_provision()/start_provision_web().
  bool wifiOk = g_wifi.autoConnect(WIFI_CONNECT_TIMEOUT_MS);
  if (wifiOk && g_hasToken && g_savedPin[0] &&
      decryptToken(g_blob, g_savedPin, g_token, sizeof(g_token))) {
    g_pinAttempts = 0; save_attempts();
    request_state(ST_LOADING);
  } else {
    request_state(ST_PROVISION);
  }
}

void loop() {
  lv_task_handler();
  boot_button_poll();

  if (g_web) g_web->handleClient();

  if (g_dirty) {
    g_dirty = false;
    render_state();
    if (g_state == ST_LOADING) {
      lv_task_handler();
      lv_refr_now(NULL);
      do_refresh();
    }
  }

  // Poll automático EM BACKGROUND (sem trocar de tela) + refresh manual
  if (g_state == ST_MAIN &&
      (g_wantRefresh || millis() - g_lastPollMs > (uint32_t)g_pollSec * 1000)) {
    g_wantRefresh = false;
    bg_refresh();           // seta g_lastPollMs no fim
  }

  // Atualização viva: contadores (1s), barra de refresh (250ms), mascotes,
  // slideshow (5s, pausa 10s após qualquer toque)
  if (g_state == ST_MAIN) {
    uint32_t now = millis();
    static uint32_t lastTick = 0, lastBar = 0, lastBob = 0, blinkAt = 0;
    static bool blinkClosed = false;
    if (now - lastTick > 1000) { lastTick = now; dash_tick(); update_tok_row(); }
    if (now - lastBar > 250 && g_ui.refBar) {
      lastBar = now;
      int v;
      if (g_refreshing) v = 1000;
      else {
        uint32_t el = now - g_lastPollMs, per = (uint32_t)g_pollSec * 1000;
        v = el >= per ? 0 : (int)(1000 - (uint64_t)el * 1000 / per);
      }
      lv_bar_set_value(g_ui.refBar, v, LV_ANIM_OFF);
    }
    if (now - lastBob > 80) {                       // animação por humor
      lastBob = now;
      float ph = now / 600.0f;
      for (int i = 0; i < g_mascN; i++) {
        if (!g_masc[i].cont) continue;
        if (g_masc[i].mood == 1)                    // ok: bob alegre
          lv_obj_set_y(g_masc[i].cont, g_masc[i].baseY + (int)(2.0f * sinf(ph + i * 0.9f) - 1.0f));
        else if (g_masc[i].mood == 2) {             // limitado: bob curto + suor
          lv_obj_set_y(g_masc[i].cont, g_masc[i].baseY + (int)(1.2f * sinf(ph * 0.6f + i)));
          if (g_masc[i].drop) {
            uint32_t cyc = (now + i * 300) % 900;
            lv_obj_set_y(g_masc[i].drop, 24 + (int)(cyc * 22 / 900));
            lv_obj_set_style_bg_opa(g_masc[i].drop, (lv_opa_t)(255 - cyc * 190 / 900), 0);
          }
        }
      }
    }
    uint32_t bp = blinkClosed ? 150 : 3000;
    if (now - blinkAt > bp) {                        // piscar (só quem está ok)
      blinkAt = now; blinkClosed = !blinkClosed;
      for (int i = 0; i < g_mascN; i++) {
        if (g_masc[i].mood != 1) continue;
        for (int k = 0; k < 2; k++) {
          if (!g_masc[i].lid[k]) continue;
          if (blinkClosed) lv_obj_clear_flag(g_masc[i].lid[k], LV_OBJ_FLAG_HIDDEN);
          else             lv_obj_add_flag(g_masc[i].lid[k], LV_OBJ_FLAG_HIDDEN);
        }
      }
    }
    if (g_slideSec > 0 && g_ui.tv && !g_refreshing && !g_mo.scrim &&
        now - g_lastTouchMs > 10000 && now - g_lastSlideMs > (uint32_t)g_slideSec * 1000) {
      g_lastSlideMs = now;
      int next = (g_curTile + 1) % NTILES;
      lv_tileview_set_tile_by_index(g_ui.tv, next, 0, LV_ANIM_ON);
    }

    // Momentos de limiar: mostra pendente e anima o overlay ativo
    if (g_pendWin >= 0 && !g_mo.scrim && !g_refreshing) {
      show_moment(g_pendWin, g_pendThr);
      g_pendWin = -1;
    }
    if (g_mo.scrim) moment_tick();
  }

  delay(5);
}
