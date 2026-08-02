#include "state_app.h"
#include "config.h"
#include "providers/claude_provider.h"
#include "providers/opencode_provider.h"

Preferences g_prefs;

// ---- Provedor ----
ClaudeProvider    g_claudeProvider;
OpenCodeProvider  g_opencodeProvider;
AIProvider* g_provider = &g_claudeProvider;
int g_providerIdx = 0;

uint8_t g_lang = 0;

State g_state = ST_BOOT;
State g_pending = ST_BOOT;
bool  g_dirty = false;
void request_state(State s) { g_pending = s; g_dirty = true; }

bool g_hasBoardExtra = false;
const char *g_boardExtraLabel = nullptr;

UsageData   g_usage = {};
ModelStatus g_status = {true, true, true, true, false};

ModelInfo g_models[NMODELS] = {
  {"Haiku",  "claude-haiku-4-5-20251001", {0, 0}, 0},
  {"Sonnet", "claude-sonnet-5",           {0, 0}, 0},
  {"Opus",   "claude-opus-4-8",           {0, 0}, 0},
  {"Fable",  "claude-fable-5",            {0, 0}, 0},
};
int g_probeIdx = 0;
const int MODEL_CENTERS[NMODELS] = {44, 121, 199, 276};

TokenStats g_tok = {0, 0, 0, 0, 0};

bool g_wantRefresh = false;
bool g_refreshing = false;
bool g_lastFetchOk = true;
uint32_t g_lastOkMs = 0;

const uint8_t BRI_LEVELS[3] = {60, 160, 255};
int g_briIdx = 1;

uint32_t g_lastPollMs = 0;
int g_pollSec = DEFAULT_POLL_SEC;
int g_tzOffset = -3;
int g_slideSec = 0;
int g_heatMode = 3;
uint32_t g_lastTouchMs = 0;
uint32_t g_lastSlideMs = 0;

bool g_forceWifi = false;
bool g_forceToken = false;
bool g_timeInit = false;

OpenCodeUsage g_ocUsage = {};
char g_ocWorkspaceId[64] = {0};
char g_ocCookie[768] = {0};
