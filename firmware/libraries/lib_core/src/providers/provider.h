#pragma once
#include <Arduino.h>
#include <lvgl.h>

// Forward declarations — definicoes completas em api.h e status.h
struct UsageData;
struct ProbeResult;
struct ModelStatus;

// ============================================================
// Interface abstrata de provedor de IA.
// Cada provedor (Claude Code, OpenCode Go) implementa os métodos
// que fazem sentido para ele. Métodos não suportados retornam
// false/nullptr — o código consumidor verifica antes de usar.
// ============================================================

// Dados de consumo do OpenCode Go (dashboard scraper).
// Análogo ao UsageData do Claude, mas com 3 janelas ($/tempo).
struct OpenCodeUsage {
    float   rollingPct;       // 5h usage % (0-100)
    float   weeklyPct;        // weekly usage %
    float   monthlyPct;       // monthly usage %
    long    rollingSec;       // segundos ateh reset 5h
    long    weeklySec;        // segundos ateh reset semanal
    long    monthlySec;       // segundos ateh reset mensal
    bool    ok;
    char    error[64];

    int     errorCode;        // 0=ok, HTTP status, ou negativo interno
    char    errorMsg[96];     // ex: "HTTP 429"
    char    errorHint[96];    // sugestao: "Cookie expirado. Atualize nas Ajustes."
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
    virtual const lv_image_dsc_t* logoSmall()     { return nullptr; }
    virtual const lv_image_dsc_t* logoWordmark()  { return nullptr; }
    virtual const lv_image_dsc_t* logoBig()       { return nullptr; }
    virtual const lv_image_dsc_t* logoXl()        { return nullptr; }
    virtual uint32_t accentColor()                { return 0xFFFFFF; }

    // ---- Dados: polling de API (Claude) ----
    virtual bool hasApiPolling()                              { return false; }
    virtual bool fetchUsage(const char* token, UsageData& out){ (void)token; (void)out; return false; }
    virtual int modelCount()                                  { return 0; }
    virtual const char* modelName(int idx)                    { (void)idx; return ""; }
    virtual const char* modelId(int idx)                      { (void)idx; return ""; }
    virtual bool hasModelProbing()                            { return false; }
    virtual bool probeModel(const char* token, const char* modelId, ProbeResult& out) {
        (void)token; (void)modelId; (void)out; return false;
    }
    virtual bool hasStatusEndpoint()                          { return false; }
    virtual bool fetchModelStatus(ModelStatus& out)           { (void)out; return false; }

    // ---- Dados: scraping de dashboard (OpenCode) ----
    virtual bool hasDashboardScraping()                       { return false; }
    virtual bool fetchDashboardUsage(const char* workspaceId,
                                     const char* authCookie,
                                     OpenCodeUsage& out) {
        (void)workspaceId; (void)authCookie; (void)out; return false;
    }

    // ---- Poll minimo efetivo (overridable por provider) ----
    virtual int effectivePollSec(int configured) { return configured; }

    // ---- Ultimo erro (pra UI de detalhe) ----
    virtual int lastErrorCode()     { return 0; }
    virtual const char* lastErrorMessage() { return ""; }
    virtual const char* lastErrorHint()    { return ""; }

    // ---- Config de rede ----
    virtual const char* messagesEndpoint() { return ""; }
    virtual const char* userAgent()        { return ""; }
    virtual const char* mDnsHostname()     { return ""; }
    virtual const char* apSsid()           { return ""; }
    virtual const char* nvsNamespace()     { return ""; }
};
