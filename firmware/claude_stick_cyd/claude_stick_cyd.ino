/**
 * Claude Usage Stick — CYD (ESP32-2432S028), com touch
 * Placa: "Cheap Yellow Display", ESP32 clássico WROOM-32, display ILI9341
 * SPI 320x240 paisagem. Fork de firmware/claude_stick/ (Guition JC4832W535)
 * adaptado pra esse hardware — ver firmware/bringup_cyd/ pro bring-up.
 *
 * Dashboard do rate-limit do Claude Code (janelas 5h e 7d, headers unified-*),
 * sonda real por modelo (latencia + HTTP), projecao de esgotamento da janela
 * 5h e ritmo de uso por hora com filtro de periodo.
 *
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
#include "touch.h"
#include "history.h"
#include "storage.h"
#include "state_app.h"
#include "state_security.h"
#include "state_dashboard.h"
#include "ui_helpers.h"
#include "settings_actions.h"
#include "web_server.h"
#include "provisioning.h"
#include "ui_screens.h"
#include "ui_dashboard.h"
#include "logo_assets.h"   // Clawd + logotipo oficiais (gerado por tools/gen_logo_assets.py)
#include "providers/claude_provider.h"
#include "providers/opencode_provider.h"

// ---- Hardware ----
Arduino_GFX *gfx = nullptr;
SPIClass sharedSPI(VSPI);   // display
static XPT2046_TouchDrv g_touch(TOUCH_CLK, TOUCH_MISO, TOUCH_MOSI, TOUCH_CS, TOUCH_IRQ,
                                 TOUCH_X_MIN, TOUCH_X_MAX, TOUCH_Y_MIN, TOUCH_Y_MAX);  // touch, HSPI dedicado
WiFiManager g_wifi;

// ---- Botão físico (BOOT) ----
static bool     g_bootDown = false;
static uint32_t g_bootDownMs = 0;
static bool     g_bootLongFired = false;

// ---- Forward declarations ----
static void render_state();
void apply_tz();

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

static void touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data) {
  (void)indev;
  uint16_t x, y;
  if (g_touch.touched()) {
    g_touch.readData(&x, &y);
    data->point.x = x;
    data->point.y = y;
    data->state = LV_INDEV_STATE_PRESSED;
    g_lastTouchMs = millis();
  } else {
    data->state = LV_INDEV_STATE_RELEASED;
  }
}

// ---- Botão físico BOOT (clique curto = próxima tela; longo = ajustes; fallback) ----
static void boot_button_poll() {
  bool down = (digitalRead(CFG_BOOT_PIN) == LOW);
  uint32_t now = millis();
  if (down && !g_bootDown) {
    g_bootDown = true; g_bootDownMs = now; g_bootLongFired = false;
  } else if (down && g_bootDown) {
    if (!g_bootLongFired && now - g_bootDownMs > BOOT_LONGPRESS_MS) {
      g_bootLongFired = true;
      if (g_state == ST_MAIN) open_settings();
      else if (g_state == ST_SETTINGS) request_state(ST_ABOUT);
    }
  } else if (!down && g_bootDown) {
    g_bootDown = false;
    if (!g_bootLongFired) {                   // clique curto
      if (g_state == ST_MAIN) {
        int nTiles = g_provider->modelCount() > 0 ? NTILES : 3;
        int next = (g_curTile + 1) % nTiles;
        if (g_ui.tv) lv_tileview_set_tile_by_index(g_ui.tv, next, 0, LV_ANIM_ON);
      } else if (g_state == ST_SETTINGS || g_state == ST_ABOUT) {
        request_state(ST_MAIN);
      } else if (g_state == ST_PIN && g_pinForSettings) {
        request_state(ST_MAIN);               // cancela o gate de Ajustes
      }
    }
  }
}

// ============================================================
// Render do estado atual
// ============================================================
static void render_state() {
  g_state = g_pending;
  start_data_web();                           // webserver sempre ativo
  moment_close();                             // overlay vive em lv_layer_top
  lv_obj_clean(lv_layer_top());
  // invalida ponteiros vivos antes de destruir a tela antiga
  memset(&g_ui, 0, sizeof(g_ui));
  g_mascN = 0;
  g_provMsg = nullptr;
  g_hdrStatus = nullptr;
  g_pinDots = g_pinMsg = g_pinLockBar = nullptr;
  g_briLbl = g_pollLbl = g_tzLbl = g_slideLbl = g_heatLbl = g_providerLbl = nullptr;

  lv_obj_clean(lv_screen_active());
  lv_obj_set_style_bg_color(lv_screen_active(), lv_color_hex(C_BG), 0);
  lv_obj_set_style_bg_opa(lv_screen_active(), LV_OPA_COVER, 0);

  switch (g_state) {
    case ST_PIN:       ui_pin(); break;
    case ST_PROVISION: ui_provision(); break;
    case ST_LOADING:   ui_loading(g_wifi.isConnected() ? g_wifi.getSSID().c_str()
                                                       : TRS("conectando WiFi", "connecting WiFi")); break;
    case ST_MAIN:      ui_main(); break;
    case ST_SETTINGS:  ui_settings(); break;
    case ST_ABOUT:     ui_about(); break;
    case ST_ERROR: {
      const char *errMsg = g_usage.error[0] ? g_usage.error
                        : g_provider->lastErrorMessage()[0] ? g_provider->lastErrorMessage()
                        : TRS("sem dados", "no data");
      ui_message(TRS("Falha", "Failed"), errMsg, C_BAD);
      // Mostra IP pra acessar /settings
      String ip = WiFi.localIP().toString();
      if (ip.length() > 0) {
        char iptxt[48];
        snprintf(iptxt, sizeof(iptxt), "http://%s/settings", ip.c_str());
        lv_obj_t *l = mklabel(lv_screen_active(), iptxt, &lv_font_montserrat_12, C_FAINT);
        lv_obj_align(l, LV_ALIGN_CENTER, 0, 44);
      }
      break;
    }
    default: break;
  }
}

// ============================================================
// Tempo (NTP) e ciclo de dados
// ============================================================
void apply_tz() { configTime(g_tzOffset * 3600, 0, NTP_SERVER_1, NTP_SERVER_2); }
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
  bool ok = false;
  if (g_provider->hasApiPolling()) {
    ok = g_provider->fetchUsage(g_token, g_usage);
    if (ok) {
      g_provider->fetchModelStatus(g_status);
      g_lastOkMs = millis(); g_lastFetchOk = true;
      hist_push(g_usage.h5, g_usage.d7); accumulate_heat(g_usage.h5); save_history();
      check_thresholds();
      if (g_provider->hasModelProbing()) probe_next_model();
    } else g_lastFetchOk = false;
  } else if (g_provider->hasDashboardScraping()) {
    ok = g_provider->fetchDashboardUsage(g_ocWorkspaceId, g_ocCookie, g_ocUsage);
    if (ok) {
      g_lastOkMs = millis(); g_lastFetchOk = true;
      // Mapeia OpenCodeUsage -> UsageData pra compatibilidade com dashboard
      g_usage.ok = true;
      g_usage.h5 = g_ocUsage.rollingPct;
      g_usage.d7 = g_ocUsage.weeklyPct;
      g_usage.h5ResetEpoch = time(nullptr) + g_ocUsage.rollingSec;
      g_usage.d7ResetEpoch = time(nullptr) + g_ocUsage.weeklySec;
      g_usage.unifiedResetEpoch = g_usage.h5ResetEpoch;
      hist_push(g_usage.h5, g_usage.d7);
      accumulate_heat(g_usage.h5); save_history();
    } else {
      g_lastFetchOk = false;
      strncpy(g_usage.error, g_ocUsage.error[0] ? g_ocUsage.error : g_provider->lastErrorHint(),
              sizeof(g_usage.error) - 1);
    }
  }
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

  bool ok = false;
  bool rebuild = false;

  if (g_provider->hasApiPolling()) {
    UsageData u = {};
    ok = g_provider->fetchUsage(g_token, u);
    if (ok) {
      g_usage = u; g_lastOkMs = millis(); g_lastFetchOk = true;
      hist_push(u.h5, u.d7); accumulate_heat(u.h5); save_history();
      check_thresholds();
      int moodBefore[NMODELS];
      for (int i = 0; i < NMODELS; i++) moodBefore[i] = model_mood(i);
      g_provider->fetchModelStatus(g_status);
      probe_next_model();
      for (int i = 0; i < NMODELS; i++)
        if (moodBefore[i] != model_mood(i)) rebuild = true;
    } else g_lastFetchOk = false;
  } else if (g_provider->hasDashboardScraping()) {
    ok = g_provider->fetchDashboardUsage(g_ocWorkspaceId, g_ocCookie, g_ocUsage);
    if (ok) {
      g_lastOkMs = millis(); g_lastFetchOk = true;
      g_usage.ok = true;
      g_usage.h5 = g_ocUsage.rollingPct;
      g_usage.d7 = g_ocUsage.weeklyPct;
      g_usage.h5ResetEpoch = time(nullptr) + g_ocUsage.rollingSec;
      g_usage.d7ResetEpoch = time(nullptr) + g_ocUsage.weeklySec;
      g_usage.unifiedResetEpoch = g_usage.h5ResetEpoch;
      hist_push(g_usage.h5, g_usage.d7);
      accumulate_heat(g_usage.h5); save_history();
    } else {
      g_lastFetchOk = false;
      strncpy(g_usage.error, g_ocUsage.error[0] ? g_ocUsage.error : g_provider->lastErrorHint(),
              sizeof(g_usage.error) - 1);
    }
  }

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
  Serial.println("\n=== Claude Usage Stick — CYD (touch) ===");

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

  // Touch — XPT2046, SPI dedicado (HSPI), validado no bring-up
  if (!g_touch.begin()) Serial.println("touch: begin() falhou");
  lv_indev_t *touch_indev = lv_indev_create();
  lv_indev_set_type(touch_indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(touch_indev, touch_read_cb);

  load_persisted();
  apply_brightness();

  if (!LittleFS.begin(true)) Serial.println("LittleFS: falhou");
  else load_history();

  g_wifi.begin();

  // Boot: com WiFi salvo conectando e token já configurado, pede o PIN por
  // touch pra decifrar (ST_PIN — nunca mais fica salvo em claro na NVS).
  // Sem WiFi salvo ou sem token ainda cai no portal de provisionamento —
  // ver ui_provision()/start_provision_web().
  bool wifiOk = g_wifi.autoConnect(WIFI_CONNECT_TIMEOUT_MS);
  if (wifiOk && g_hasToken) {
    g_pinForSettings = false;
    request_state(ST_PIN);
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
      (g_wantRefresh || millis() - g_lastPollMs > (uint32_t)g_provider->effectivePollSec(g_pollSec) * 1000)) {
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
        uint32_t el = now - g_lastPollMs, per = (uint32_t)g_provider->effectivePollSec(g_pollSec) * 1000;
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
      int nTiles2 = g_provider->modelCount() > 0 ? NTILES : 3;
      int next = (g_curTile + 1) % nTiles2;
      lv_tileview_set_tile_by_index(g_ui.tv, next, 0, LV_ANIM_ON);
    }

    // Momentos de limiar: mostra pendente e anima o overlay ativo
    if (g_pendWin >= 0 && !g_mo.scrim && !g_refreshing) {
      show_moment(g_pendWin, g_pendThr);
      g_pendWin = -1;
    }
    if (g_mo.scrim) moment_tick();
  }

  // Atualiza barra de lockout na tela de PIN
  if (g_state == ST_PIN && g_pinLockBar && millis() < g_lockoutUntil) {
    uint32_t totalMs = g_lockoutUntil - g_lockoutStartMs;
    uint32_t elapsed = millis() - g_lockoutStartMs;
    int val = totalMs > 0 ? (int)(1000 - (uint64_t)elapsed * 1000 / totalMs) : 0;
    if (val < 0) val = 0;
    lv_bar_set_value(g_pinLockBar, val, LV_ANIM_OFF);
    static uint32_t lastPinBar = 0;
    if (millis() - lastPinBar > 250) {
      lastPinBar = millis();
      if (g_pinMsg) {
        int rem = (g_lockoutUntil - millis()) / 1000;
        if (rem <= 0) {
          lv_label_set_text(g_pinMsg, "");
          lv_obj_add_flag(g_pinLockBar, LV_OBJ_FLAG_HIDDEN);
        } else {
          char m[48]; snprintf(m, sizeof(m), TRS("Aguarde %ds", "Wait %ds"), rem);
          lv_label_set_text(g_pinMsg, m);
        }
      }
    }
  }

  delay(5);
}
