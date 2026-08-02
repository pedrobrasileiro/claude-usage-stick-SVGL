#pragma once
#include <stdint.h>
#include <Preferences.h>
#include "api.h"
#include "status.h"
#include "providers/provider.h"

extern Preferences g_prefs;   // NVS — usado por storage.cpp, security.cpp, settings_actions.cpp

// ---- Provedor de IA ativo (Claude Code ou OpenCode Go) ----
class ClaudeProvider;
class OpenCodeProvider;
extern ClaudeProvider    g_claudeProvider;
extern OpenCodeProvider  g_opencodeProvider;
extern AIProvider* g_provider;
extern int g_providerIdx;     // 0 = Claude, 1 = OpenCode (NVS "provider")

// ============================================================
// Estado da aplicação: máquina de estado, config runtime (NVS)
// e dados de negócio (uso/modelos/tokens). Compartilhado por
// quase todos os módulos — telas touch, web server, dashboard.
// ============================================================

// ---- Idioma (0 = portugues, 1 = english; Ajustes -> NVS "lang") ----
extern uint8_t g_lang;
#define TRS(pt, en) (g_lang ? (en) : (pt))

// ---- Máquina de estado ----
enum State {
  ST_BOOT, ST_PIN, ST_PROVISION, ST_LOADING, ST_MAIN, ST_SETTINGS, ST_ABOUT, ST_ERROR,
  ST_EXTRA   // tela extra opcional específica da placa (ex.: LED RGB na Fikra) — ver g_hasBoardExtra
};
extern State g_state;
extern State g_pending;
extern bool  g_dirty;
void request_state(State s);

// ---- Tela extra opcional (hardware específico de uma placa) ----
// Por padrão nenhuma placa tem — o .ino de quem tiver (ex.: claude_stick_fikra,
// LED RGB WS2812) seta essas duas em setup() e trata ST_EXTRA no seu
// render_state(). Sem isso, a entrada correspondente nem aparece em Ajustes.
extern bool g_hasBoardExtra;
extern const char *g_boardExtraLabel;

// ---- Dados ----
extern UsageData   g_usage;
extern ModelStatus g_status;

// ---- Modelos sondados (1 por ciclo, rotativo) ----
#define NMODELS 4
struct ModelInfo { const char *name; const char *id; ProbeResult pr; uint32_t atMs; };
extern ModelInfo g_models[NMODELS];
extern int g_probeIdx;
extern const int MODEL_CENTERS[NMODELS];   // centros dos mascotes em 320px

// ---- Tokens por sessao (vindos do bridge via POST /tokens) ----
struct TokenStats { long long tin, tout, cache; int sessions; uint32_t atMs; };
extern TokenStats g_tok;
#define TOK_FRESH_MS (15UL * 60UL * 1000UL)

// ---- Refresh em background ----
extern bool g_wantRefresh;        // botão de refresh pediu atualização
extern bool g_refreshing;         // busca em andamento
extern bool g_lastFetchOk;        // último fetch deu certo?
extern uint32_t g_lastOkMs;       // millis do último sucesso (p/ "atualizado há Xs")

// ---- Brilho ----
extern const uint8_t BRI_LEVELS[3];
extern int g_briIdx;

// ---- Config runtime (NVS) ----
extern uint32_t g_lastPollMs;     // millis do último poll (p/ barra de refresh)
extern int g_pollSec;             // intervalo de atualização (config, NVS)
extern int g_tzOffset;            // fuso GMT (horas), config NVS
extern int g_slideSec;            // slideshow: 0=off, 5/10/15/30s (config, NVS)
extern int g_heatMode;            // 0=hoje 1=7d 2=30d 3=tudo (config, NVS)
extern uint32_t g_lastTouchMs;    // ultimo toque (pausa o slideshow)
extern uint32_t g_lastSlideMs;

// ---- Flags de (re)provisionamento ----
extern bool g_forceWifi;          // "Configurar WiFi" pediu reconfiguração
extern bool g_forceToken;         // "Trocar token" pediu novo token
extern bool g_timeInit;

// ---- OpenCode Go ----
extern OpenCodeUsage g_ocUsage;          // ultimo fetch do dashboard OpenCode
extern char g_ocWorkspaceId[64];         // ID do workspace (NVS "oc_wsid")
extern char g_ocCookie[768];             // cookie de sessao (NVS "oc_cookie", cifrado)
