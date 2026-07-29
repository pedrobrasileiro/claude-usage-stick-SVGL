#include <Arduino.h>
#include <WiFi.h>
#include <time.h>
#include "provisioning.h"
#include "web_server.h"
#include "wifi_manager.h"
#include "state_app.h"
#include "state_security.h"
#include "storage.h"
#include "ui_helpers.h"
#include "config.h"

extern WiFiManager g_wifi;   // ainda vive no .ino (hardware, ver plano de módulos)
extern void apply_tz();      // ciclo de tempo/dados — ainda em claude_stick_cyd.ino

lv_obj_t *g_provMsg = nullptr;
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

void ui_provision() {
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
