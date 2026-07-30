#include "settings_actions.h"
#include "state_app.h"
#include "storage.h"
#include <WiFi.h>
#include "providers/claude_provider.h"
#include "providers/opencode_provider.h"

lv_obj_t *g_briLbl = nullptr, *g_pollLbl = nullptr,
         *g_tzLbl = nullptr, *g_slideLbl = nullptr, *g_heatLbl = nullptr,
         *g_providerLbl = nullptr;

static const int POLL_OPTS[4] = {30, 60, 120, 300};
static const int TZ_OPTS[] = {-3, -4, -5, -6, -7, -8, -2, -1, 0, 1, 2, 3};
#define NTZ ((int)(sizeof(TZ_OPTS) / sizeof(TZ_OPTS[0])))

extern void apply_tz();   // ainda em claude_stick_cyd.ino (ciclo de tempo/dados)

void apply_setting_action(int act) {
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
    case 4:                                            // apagar tudo (2 toques, ou confirmação no form web)
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
    case 10: request_state(ST_ABOUT); break;            // sobre / about
    case 11: {                                          // heatmap: hoje -> 7d -> 30d -> tudo -> hoje
      g_heatMode = (g_heatMode + 1) % 4;
      g_prefs.putInt("heatm", g_heatMode);
      if (g_heatLbl) {
        const char *m[4] = {TRS("hoje", "today"), "7d", "30d", TRS("tudo", "all")};
        char buf[48]; snprintf(buf, sizeof(buf), TRS(LV_SYMBOL_CHARGE "  Ritmo por hora: %s",
                                                     LV_SYMBOL_CHARGE "  Hourly burn: %s"), m[g_heatMode]);
        lv_label_set_text(g_heatLbl, buf);
      }
      break;
    }
    case 12: {                                          // toggle provider (Claude <-> OpenCode)
      g_providerIdx = (g_providerIdx + 1) % 2;
      g_prefs.putInt(NVS_PROVIDER, g_providerIdx);
      if (g_providerIdx == 0) g_provider = &g_claudeProvider;
      else                    g_provider = &g_opencodeProvider;
      if (g_providerLbl) {
        char m[40];
        snprintf(m, sizeof(m), LV_SYMBOL_SHUFFLE "  %s: %s",
                 TRS("Provedor", "Provider"), g_provider->name());
        lv_label_set_text(g_providerLbl, m);
      }
      request_state(ST_SETTINGS);                       // redesenha settings (mostra/esconde campos OC)
      break;
    }
    case 13: case 14: {                                  // campos OpenCode: mostra IP pra config via web
      String ip = WiFi.localIP().toString();
      if (ip.length() > 0) {
        request_state(ST_MAIN);                          // volta pro dashboard
        // nao da pra mostrar toast, entao nao fazemos nada — o IP ja esta visivel nos settings
      }
      break;
    }
    case 15: break;                                      // salvar OpenCode (web only)
  }
}
