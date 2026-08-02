#include <Arduino.h>
#include "config.h"
#include "wifi_manager.h"
#include "state_app.h"
#include "state_security.h"
#include "storage.h"
#include "providers/claude_provider.h"
#include "providers/opencode_provider.h"

extern WiFiManager g_wifi;   // ainda vive no .ino (hardware, ver plano de m\u00f3dulos)

void load_persisted() {
  g_prefs.begin(NVS_NAMESPACE, false);
  size_t n = g_prefs.getBytesLength("blob");
  if (n == sizeof(EncryptedBlob)) {
    g_prefs.getBytes("blob", &g_blob, sizeof(EncryptedBlob));
    g_hasToken = true;
  }
  g_pinAttempts = g_prefs.getInt("pinatt", 0);
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

  // Provider (0=Claude, 1=OpenCode)
  g_providerIdx = g_prefs.getInt(NVS_PROVIDER, 0);
  if (g_providerIdx == 1) g_provider = &g_opencodeProvider;
  else { g_providerIdx = 0; g_provider = &g_claudeProvider; }

  // OpenCode campos
  String wsid = g_prefs.getString(NVS_OC_WSID, "");
  strncpy(g_ocWorkspaceId, wsid.c_str(), sizeof(g_ocWorkspaceId) - 1);
  String ck = g_prefs.getString(NVS_OC_COOKIE, "");
  strncpy(g_ocCookie, ck.c_str(), sizeof(g_ocCookie) - 1);
}
void save_blob() { g_prefs.putBytes("blob", &g_blob, sizeof(EncryptedBlob)); }
void save_attempts() { g_prefs.putInt("pinatt", g_pinAttempts); }
void apply_brightness() { ledcWrite(TFT_BL, BRI_LEVELS[g_briIdx]); }

void factory_reset() {
  g_prefs.clear();              // apaga blob, pinatt, bri do namespace claude
  g_wifi.forgetAll();
  g_hasToken = false;
  g_token[0] = 0;
  g_pinEntry[0] = 0;
  g_pinAttempts = 0;
  g_ocWorkspaceId[0] = 0;
  g_ocCookie[0] = 0;
  g_providerIdx = 0;
  g_provider = &g_claudeProvider;
  Serial.println("[RESET] tudo apagado");
}
