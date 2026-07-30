# Plano: Suporte Multi-Provedor (Claude Code + OpenCode Go)

## Objetivo

Adicionar tracking do OpenCode Go ao firmware, com alternância entre Claude Code e OpenCode via settings (touch + web). O OpenCode é um módulo plugável — desativado, custa zero ciclos e zero RAM.

---

## Fonte de dados

O OpenCode Go **não tem API de consumo**. O `opencode-quota` ([slkiser/opencode-quota](https://github.com/slkiser/opencode-quota)) resolve isso com **scraping do dashboard web**: faz `GET https://opencode.ai/workspace/{id}/go` com cookie de sessão e faz parse do HTML (SolidJS SSR hydration).

```
ESP32 → GET https://opencode.ai/workspace/{workspaceId}/go
       Header: Cookie: auth={sessionCookie}
       → HTML contém:
           rollingUsage:$R[N]={usagePercent:45.2, resetInSec:7200}
           weeklyUsage:$R[N]={usagePercent:30.1, resetInSec:86400}
           monthlyUsage:$R[N]={usagePercent:15.7, resetInSec:604800}
       → Streaming parser extrai 3 janelas (5h, semanal, mensal)
```

**Resultado:**
```json
{
  "rolling":  { "usagePercent": 45.2, "resetInSec": 7200,  "percentRemaining": 54.8 },
  "weekly":   { "usagePercent": 30.1, "resetInSec": 86400, "percentRemaining": 69.9 },
  "monthly":  { "usagePercent": 15.7, "resetInSec": 604800, "percentRemaining": 84.3 }
}
```

### O que o usuário configura no device

| Campo | Onde achar | Exemplo | NVS key |
|-------|-----------|---------|---------|
| **Workspace ID** | URL: `opencode.ai/workspace/{id}` | `cly8x...` | `oc_wsid` |
| **Auth Cookie** | DevTools → Application → Cookies → `auth` | `eyJ...` | `oc_cookie` (criptografado) |

---

## Princípio de isolamento

OpenCode é plugável. Se `g_provider = ClaudeProvider` (default), o código do OpenCode compila mas **nunca é instanciado** — zero impacto no runtime.

```
g_provider = &claudeProvider  (default, NVS "provider" = 0)
  → opencode_provider.cpp existe mas não executa
  → zero ciclos de CPU gastos com scraping
  → zero RAM ocupada com OpenCodeUsage
  → todas as funções do Claude funcionam exatamente como hoje

g_provider = &opencodeProvider  (NVS "provider" = 1)
  → claude_provider não é usado (mas continua compilando)
  → polling vai pro scraper, não pra api.anthropic.com
  → dashboard usa ícone OpenCode, 3 tiles, verde
```

Sem `#ifdef`, sem acoplamento bidirecional. A interface abstrata é o ponto único de contato.

---

## Gasto de tokens e risco de bloqueio

### Tokens: ZERO

O scraper faz `GET` na página HTML do dashboard — não passa pelos endpoints de LLM (`/zen/go/v1/chat/completions`, `/zen/go/v1/messages`) que cobram por token. É tráfego web normal, igual abrir o navegador.

### Mitigações contra rate-limit / bloqueio de IP

| Mecanismo | Detalhe |
|-----------|---------|
| **Poll mínimo forçado** | Quando provider=OpenCode, intervalo nunca é < 300s, independente da config do usuário |
| **User-Agent de navegador** | `Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) Gecko/20100101 Firefox/148.0` — igual ao opencode-quota |
| **ETag / If-None-Match** | Envia ETag da resposta anterior. Se servidor suportar, recebe 304 sem body (zero banda) |
| **Exponential backoff** | Se receber 429 ou erro, dobra intervalo até próximo poll (máx 30 min) |
| **Jitter aleatório** | Adiciona ±15% de variação no intervalo pra não parecer robô |
| **Cache local** | Se request falhar, mantém últimos dados válidos no dashboard — nunca faz retry imediato |
| **Fallback visual** | Mostra "atualizando..." com último valor conhecido, igual ao Claude já faz |

```
T=0:    poll → OK (304 Not Modified, sem body)
T=300s: poll → OK (200, HTML com novos dados)
T=600s: poll → 429 (rate limited)
        → backoff: próximo poll em 600s (dobro)
        → dashboard mostra último valor válido
T=1200s: poll → OK
        → backoff resetado
```

---

## Painel de erro clicável

Quando a consulta falha, o header do dashboard mostra um indicador ⚠. Ao tocar, abre overlay com detalhes:

```
┌──────────────────────────────┐
│ [logo]  Claude Usage Stick ⚠ │  ← indicador clicável
│ ┌──────────────────────────┐ │
│ │    5h    │    7d         │ │  ← dados stale (último válido)
│ │   45%    │   30%         │ │
│ └──────────────────────────┘ │
└──────────────────────────────┘
         │ toque no ⚠
         ▼
┌──────────────────────────────┐
│ ⚠ Falha na consulta         │
│                              │
│ Erro HTTP 429                │
│ Too Many Requests            │
│                              │
│ Último OK: 14:32             │
│ Tentativa:  14:37            │
│ Próxima tentativa em 292s    │
│                              │
│   [toque em qualquer lugar   │
│        para fechar]          │
└──────────────────────────────┘
```

### Códigos de erro e sugestões

| Erro | Código | Causa | Sugestão ao usuário |
|------|--------|-------|---------------------|
| HTTP 401/403 | 401 | Cookie expirado | "Cookie de sessão expirou. Atualize nas Ajustes." |
| HTTP 429 | 429 | Rate limit | "Muitas consultas. Backoff automático ativo." |
| HTTP 5xx | 500 | Servidor fora do ar | "Servidor indisponível. Tentando novamente..." |
| Timeout | -1 | Rede lenta/bloqueio | "Sem resposta do servidor. Verifique o Wi-Fi." |
| Parse error | -2 | HTML mudou formato | "Formato da página mudou. Atualize o firmware." |
| DNS/TLS | -3 | Sem internet / cert | "Sem conexão. Verifique o Wi-Fi." |
| WiFi off | -4 | WiFi desconectado | "Wi-Fi desconectado." |

---

## Estrutura de diretórios

```
firmware/claude_stick_cyd/
│
├── providers/                     ← NOVO diretório
│   ├── provider.h                 ← classe abstrata AIProvider
│   ├── claude_provider.h          ← implementação Claude
│   ├── claude_provider.cpp
│   ├── opencode_provider.h        ← implementação OpenCode (scraper)
│   └── opencode_provider.cpp
│
├── opencode_logo.h                ← NOVO: assets LVGL do ícone OpenCode
│
├── state_app.h                    ← EDIT: +AIProvider*, +OpenCodeUsage struct
├── state_app.cpp                  ← EDIT: inicializa g_provider default
├── config.h                       ← EDIT: +constantes OpenCode
├── settings_actions.h             ← EDIT: +action 12 (toggle provider)
├── settings_actions.cpp           ← EDIT: +ação de toggle
├── storage.h                      ← EDIT: +load/save campos OpenCode
├── storage.cpp                    ← EDIT: +persist provider choice
├── ui_helpers.h                   ← EDIT: +cores OpenCode
├── ui_helpers.cpp                 ← EDIT: +mascot/accessory condicional
├── ui_dashboard.h                 ← EDIT: +error indicator callback
├── ui_dashboard.cpp               ← EDIT: logo dinâmico, tile condicional
├── ui_screens.h                   ← EDIT: +ui_error_detail()
├── ui_screens.cpp                 ← EDIT: overlay erro + settings OpenCode
├── web_server.h                   ← EDIT: +páginas provider-aware
├── web_server.cpp                 ← EDIT: +handler settings OpenCode
├── provisioning.cpp               ← EDIT: +AP SSID provider-aware
├── claude_stick_cyd.ino           ← EDIT: usa g_provider no pipeline
│
├── api.h / api.cpp                ← mantidos (reusados por ClaudeProvider)
├── status.h / status.cpp          ← mantidos (reusados por ClaudeProvider)
├── crypto.h / crypto.cpp          ← sem mudança
├── history.h / history.cpp        ← sem mudança
├── state_security.h / .cpp        ← sem mudança
├── state_dashboard.h / .cpp       ← sem mudança
├── certs.h / certs.cpp            ← sem mudança
├── touch.h                        ← sem mudança
├── wifi_manager.h                 ← sem mudança
├── logo_assets.h                  ← sem mudança (assets Claude)
├── lv_conf.h                      ← sem mudança
│
└── tools/
    └── gen_opencode_logo.py       ← NOVO: gera opencode_logo.h
```

---

## 1. Interface abstrata `providers/provider.h`

```cpp
#pragma once
#include <Arduino.h>
#include <lvgl.h>
#include "api.h"        // UsageData, ProbeResult
#include "status.h"     // ModelStatus

// Dados de consumo do OpenCode Go (dashboard scraper)
struct OpenCodeUsage {
    float   rollingPct;       // 5h usage % (0-100)
    float   weeklyPct;        // weekly usage %
    float   monthlyPct;       // monthly usage %
    long    rollingSec;       // segundos até reset 5h
    long    weeklySec;        // segundos até reset semanal
    long    monthlySec;       // segundos até reset mensal
    bool    ok;
    char    error[64];

    // Error detail (clicável)
    int     errorCode;        // 0=ok, HTTP status, ou negativo interno
    char    errorMsg[96];     // ex: "HTTP 429"
    char    errorHint[96];    // sugestão: "Cookie expirado. Atualize nas Ajustes."
    uint32_t errorAtMs;
    uint32_t lastOkMs;
    uint32_t nextRetryMs;
};

class AIProvider {
public:
    virtual ~AIProvider() {}

    // ---- Identidade ----
    virtual const char* name() = 0;
    virtual const char* shortName() = 0;

    // ---- Branding (descritores LVGL ARGB8888) ----
    virtual const lv_image_dsc_t* logoSmall() { return nullptr; }
    virtual const lv_image_dsc_t* logoWordmark() { return nullptr; }
    virtual const lv_image_dsc_t* logoBig() { return nullptr; }
    virtual const lv_image_dsc_t* logoXl() { return nullptr; }
    virtual uint32_t accentColor() { return 0xFFFFFF; }

    // ---- Dados: polling de API (Claude) ----
    virtual bool hasApiPolling() { return false; }
    virtual bool fetchUsage(const char* token, UsageData& out) { return false; }
    virtual int modelCount() { return 0; }
    virtual const char* modelName(int idx) { (void)idx; return ""; }
    virtual const char* modelId(int idx) { (void)idx; return ""; }
    virtual bool hasModelProbing() { return false; }
    virtual bool probeModel(const char* token, const char* modelId, ProbeResult& out) { return false; }
    virtual bool hasStatusEndpoint() { return false; }
    virtual bool fetchModelStatus(ModelStatus& out) { return false; }

    // ---- Dados: scraping de dashboard (OpenCode) ----
    virtual bool hasDashboardScraping() { return false; }
    virtual bool fetchDashboardUsage(const char* workspaceId,
                                     const char* authCookie,
                                     OpenCodeUsage& out) { return false; }

    // ---- Poll mínimo efetivo (overridable) ----
    virtual int effectivePollSec(int configured) { return configured; }

    // ---- Último erro (pra UI de detalhe) ----
    virtual int lastErrorCode() { return 0; }
    virtual const char* lastErrorMessage() { return ""; }
    virtual const char* lastErrorHint() { return ""; }

    // ---- Config de rede ----
    virtual const char* messagesEndpoint() { return ""; }
    virtual const char* userAgent() { return ""; }
    virtual const char* mDnsHostname() { return ""; }
    virtual const char* apSsid() { return ""; }
    virtual const char* nvsNamespace() { return ""; }
};
```

---

## 2. ClaudeProvider `providers/claude_provider.h/.cpp`

```cpp
class ClaudeProvider : public AIProvider {
    // hasApiPolling()         = true
    // hasModelProbing()       = true
    // hasStatusEndpoint()     = true
    // hasDashboardScraping()  = false
    //
    // name()           = "Claude Code"
    // shortName()      = "claude"
    // modelCount()     = 4
    // modelName(0)     = "Haiku"
    // modelId(0)       = "claude-haiku-4-5-20251001"
    // ...Sonnet, Opus, Fable
    //
    // fetchUsage()     → POST api.anthropic.com/v1/messages
    //                    (reusa lógica de api.cpp)
    // fetchModelStatus() → GET status.claude.com/api/v2/incidents
    //                    (reusa lógica de status.cpp)
    // probeModel()     → POST api.anthropic.com/v1/messages
    //
    // logoSmall()      = &img_clawd_sm
    // logoWordmark()   = &img_wordmark
    // logoBig()        = &img_clawd_big
    // logoXl()         = &img_clawd_xl
    // accentColor()    = 0xD97757 (coral)
    // mDnsHostname()   = "claude-stick"
    // apSsid()         = "ClaudeStick-Setup"
    // nvsNamespace()   = "claude"
    // userAgent()      = "claude-code/2.1.5"
    // messagesEndpoint() = "https://api.anthropic.com/v1/messages"
};
```

`api.h`/`api.cpp` e `status.h`/`status.cpp` **continuam existindo** com as mesmas funções (`fetchUsage`, `probeModel`, `fetchModelStatus`). O `ClaudeProvider` simplesmente chama essas funções. Zero mudança no comportamento existente.

---

## 3. OpenCodeProvider `providers/opencode_provider.h/.cpp`

```cpp
class OpenCodeProvider : public AIProvider {
    // hasApiPolling()         = false
    // hasModelProbing()       = false
    // hasStatusEndpoint()     = false
    // hasDashboardScraping()  = true
    //
    // name()           = "OpenCode Go"
    // shortName()      = "opencode"
    // modelCount()     = 0
    //
    // fetchDashboardUsage(workspaceId, authCookie, out)
    //   → GET https://opencode.ai/workspace/{id}/go + Cookie: auth={cookie}
    //   → streaming parser: lê resposta em chunks (512 bytes)
    //   → procura padrões SolidJS SSR com regex:
    //       rollingUsage:$R[N]={...usagePercent:X...resetInSec:Y...}
    //       weeklyUsage:$R[N]={...usagePercent:X...resetInSec:Y...}
    //       monthlyUsage:$R[N]={...usagePercent:X...resetInSec:Y...}
    //   → fecha conexão assim que acha os 3
    //   → preenche OpenCodeUsage + errorCode/msg/hint
    //
    // effectivePollSec(cfg)  = max(cfg, 300)
    //
    // logoSmall()      = &img_oc_sm
    // logoWordmark()   = &img_oc_wordmark
    // logoBig()        = &img_oc_big
    // logoXl()         = &img_oc_xl
    // accentColor()    = 0x22C55E (verde OpenCode)
    // mDnsHostname()   = "opencode-stick"
    // apSsid()         = "OpenCodeStick-Setup"
    // nvsNamespace()   = "opencode"
    // userAgent()      = "Mozilla/5.0 ... Firefox/148.0"
};
```

### Streaming parser (pseudocódigo)

```cpp
bool OpenCodeProvider::fetchDashboardUsage(const char* wsId, const char* cookie,
                                           OpenCodeUsage& out) {
    // 1. HTTP GET com WiFiClientSecure + HTTPClient
    // 2. Headers: Cookie, User-Agent, Accept, If-None-Match (ETag cache)
    // 3. Se HTTP != 200: preenche errorCode/msg/hint, retorna false
    //
    // 4. Streaming parse:
    WiFiClient* stream = http.getStreamPtr();
    char buf[512];
    int found = 0;  // bitmask: 1=rolling, 2=weekly, 4=monthly
    String chunk;

    while (stream->available() && found != 7) {
        int len = stream->readBytes(buf, min(512, stream->available()));
        chunk = String(buf, len);
        if (!(found & 1) && parseWindow(chunk, "rollingUsage",
                                        out.rollingPct, out.rollingSec)) found |= 1;
        if (!(found & 2) && parseWindow(chunk, "weeklyUsage",
                                        out.weeklyPct, out.weeklySec))   found |= 2;
        if (!(found & 4) && parseWindow(chunk, "monthlyUsage",
                                        out.monthlyPct, out.monthlySec)) found |= 4;
    }
    http.end();

    // 5. Fallback: tenta data-slot HTML se SSR não encontrou nada
    if (!found) { /* parse alternativo com data-slot="usage-item" */ }

    out.ok = (found != 0);
    if (!out.ok) {
        out.errorCode = -2; // parse error
        strncpy(out.errorMsg, "Formato da pagina mudou", sizeof(out.errorMsg));
        strncpy(out.errorHint, "Atualize o firmware.", sizeof(out.errorHint));
    } else {
        out.errorCode = 0;
        out.lastOkMs = millis();
    }
    return out.ok;
}

// Regex-free parser: strstr + indexOf
static bool parseWindow(const String& html, const char* prefix,
                        float& pct, long& sec) {
    int idx = html.indexOf(prefix);
    if (idx < 0) return false;

    int pctAt = html.indexOf("usagePercent:", idx);
    int secAt = html.indexOf("resetInSec:", idx);
    if (pctAt < 0 || secAt < 0) return false;

    int pctStart = pctAt + 13;
    int pctEnd = html.indexOf(',', pctStart);
    if (pctEnd < 0) pctEnd = html.indexOf('}', pctStart);
    pct = html.substring(pctStart, pctEnd).toFloat();

    int secStart = secAt + 11;
    int secEnd = html.indexOf(',', secStart);
    if (secEnd < 0) secEnd = html.indexOf('}', secStart);
    sec = html.substring(secStart, secEnd).toInt();

    return pct >= 0 && pct <= 100 && sec >= 0;
}
```

---

## 4. Configuração do usuário (settings)

### Action 12 — Toggle provider

Adicionado em `settings_actions.cpp`:

```cpp
case 12:  // Toggle provider (0=Claude, 1=OpenCode)
{
    int v = (g_providerIdx + 1) % 2;
    g_providerIdx = v;
    g_prefs.putInt("provider", v);
    // Recria o provider
    if (g_providerIdx == 0) g_provider = &g_claudeProvider;
    else                    g_provider = &g_opencodeProvider;
    // Redesenha tudo
    request_state(ST_LOADING);
}
```

### Campos OpenCode (visíveis só quando provider = OpenCode)

No menu **Ajustes** (touch) e **/settings** (web), abaixo do toggle:

| Action | Label | Descrição |
|--------|-------|-----------|
| 13 | Workspace ID | Campo de texto para o ID do workspace OpenCode |
| 14 | Auth Cookie | Campo de texto para o cookie de sessão (mascarado) |

NVS keys: `oc_wsid` (String), `oc_cookie` (String, criptografado via `g_blob`).

### Persistência (`storage.cpp`)

```cpp
// Em load_persisted():
g_providerIdx = g_prefs.getInt("provider", 0);
g_prefs.getString("oc_wsid", g_ocWorkspaceId, sizeof(g_ocWorkspaceId));
// g_ocCookie decifrado via crypto (igual g_token)

// Em factory_reset(): limpa ambos
```

---

## 5. Mudanças na UI

### Dashboard `ui_dashboard.cpp`

| Local | Mudança |
|-------|---------|
| Header ícone | `lv_image_set_src(icon, g_provider->logoSmall())` |
| Header wordmark | `lv_image_set_src(wordmark, g_provider->logoWordmark())` |
| Tile "MODELOS" | `if (g_provider->modelCount() > 0) build_tile_models()` |
| Tile "AGORA" | Mostra 2 janelas (Claude) ou 3 janelas (OpenCode) baseado no provider |
| Moment overlay | Usa `g_provider->logoXl()` + animação específica do provider |
| Indicador erro | Círculo verde/âmbar/vermelho ao lado da barra de refresh |
| Touch indicador | Callback → `ui_error_detail()` |

### Dashboard OpenCode (3 tiles)

| Tile | Conteúdo |
|------|----------|
| **AGORA** | 5h%, semanal%, mensal% com meters + countdown reset |
| **TENDÊNCIA** | Gráfico histórico 5h + projeção (dados do scraper acumulados no history) |
| **RITMO** | Heatmap 24h por período (dados do history) |
| **MODELOS** | Não renderizado (`modelCount() == 0`) |

### UI Helpers `ui_helpers.cpp`

```cpp
// cores: C_ACCENT → g_provider->accentColor() onde usado
// mascot: build_model_mascot() retorna early se modelCount() == 0
// accessory: build_accessory() retorna early se modelCount() == 0
```

### Telas `ui_screens.cpp`

| Tela | Mudança |
|------|---------|
| `ui_about()` | Logo + nome do provider ativo |
| `ui_settings()` | Novo row: toggle provider (Claude ↔ OpenCode). Campos condicionais |
| `ui_error_detail()` | Novo overlay com código, mensagem, hint, timestamps |
| `ui_loading()` | Logo do provider ativo |

### Web `web_server.cpp`

```cpp
// mDNS: g_provider->mDnsHostname()
// CSS: --accent: g_provider->accentColor()
// Título: g_provider->name()
// settings_page(): action 12, campos 13/14 condicionais
```

---

## 6. Logo assets (OpenCode)

Ícone OpenCode de `favicon.svg` (512×512, 3 cores: `#131010` fundo, `#FFFFFF` bracket, `#5A5858` bloco). Gerado via `tools/gen_opencode_logo.py`:

| Asset | Tamanho | Uso |
|-------|---------|-----|
| `img_oc_sm` | ~42×26 | Header dashboard |
| `img_oc_wordmark` | ~56×26 | Texto "opencode" ao lado do ícone |
| `img_oc_big` | ~144px | Loading / About |
| `img_oc_xl` | ~176px | Moment overlay |

---

## 7. Mudanças no `.ino`

```cpp
// Em bg_refresh():
if (g_provider->hasApiPolling()) {
    // Claude: fetch via Anthropic API
    UsageData u = {};
    bool ok = g_provider->fetchUsage(g_token, u);
    // ... lógica existente ...
} else if (g_provider->hasDashboardScraping()) {
    // OpenCode: fetch via dashboard scraper
    OpenCodeUsage u = {};
    int effectivePoll = g_provider->effectivePollSec(g_pollSec);
    bool ok = g_provider->fetchDashboardUsage(g_ocWorkspaceId, g_ocCookie, u);
    // ... preenche g_usage com dados compatíveis pra UI ...
}

// No loop():
uint32_t pollMs = (uint32_t)g_provider->effectivePollSec(g_pollSec) * 1000;
if (millis() - g_lastPollMs > pollMs) { bg_refresh(); }
```

---

## 8. Ordem de implementação

Cada passo deve compilar (`./build.sh`) antes de seguir:

| # | Arquivos | O que faz |
|---|----------|-----------|
| 1 | `tools/gen_opencode_logo.py` | Gera `opencode_logo.h` com assets LVGL |
| 2 | `providers/provider.h` | Interface abstrata com defaults vazios |
| 3 | `config.h` | Constantes OpenCode (endpoints, NVS keys, cores, OC_MIN_POLL_SEC=300) |
| 4 | `providers/claude_provider.h/.cpp` | Wrapper que chama api.cpp/status.cpp existentes |
| 5 | `providers/opencode_provider.h/.cpp` | Scraper com streaming parser + error handling |
| 6 | `state_app.h/.cpp` | `AIProvider* g_provider`, `int g_providerIdx`, `OpenCodeUsage g_ocUsage`, `char g_ocWorkspaceId[]`, `char g_ocCookie[]` |
| 7 | `storage.h/.cpp` | Persist provider choice + campos OpenCode no NVS |
| 8 | `settings_actions.h/.cpp` | Action 12 (toggle) + ações 13/14 (campos OpenCode) |
| 9 | `ui_helpers.h/.cpp` | Cores condicionais, mascot condicional |
| 10 | `ui_screens.h/.cpp` | `ui_error_detail()`, settings com campos condicionais, about provider-aware |
| 11 | `ui_dashboard.h/.cpp` | Logo dinâmico, tile modelos condicional, indicador de erro clicável |
| 12 | `web_server.h/.cpp` + `provisioning.cpp` | Páginas provider-aware |
| 13 | `claude_stick_cyd.ino` | Usa `g_provider->fetchUsage()` / `fetchDashboardUsage()` no pipeline |
| 14 | Teste final + upload | `./build.sh upload` |

---

## 9. Resultado esperado

| | Claude Code | OpenCode Go |
|---|---|---|
| **Fonte** | API Anthropic (headers `unified-*`) | Dashboard web (scraping SolidJS SSR) |
| **Tokens gastos** | 1 token por poll (body: `{"max_tokens":1}`) | Zero (GET em página HTML) |
| **Poll mínimo** | 30s (configurável) | 300s (forçado) |
| **Header logo** | Clawd + wordmark coral | Ícone OpenCode verde |
| **Tiles** | 4 (Agora, Modelos, Tendência, Ritmo) | 3 (Agora, Tendência, Ritmo) |
| **Janelas** | 5h + 7d | 5h + semanal + mensal |
| **Modelos** | Haiku, Sonnet, Opus, Fable (com mascots) | Não exibido |
| **Moments** | Clawd XL animado (bounce/sweat/KO) | Ícone com pulse + glow |
| **Erro** | Mensagem estática | Overlay clicável com código + sugestão |
| **mDNS** | `claude-stick.local` | `opencode-stick.local` |
| **Isolamento** | Default — OpenCode compila mas não executa | — |
