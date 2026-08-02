#pragma once
#include "provider.h"
#include "config.h"

class OpenCodeProvider : public AIProvider {
public:
    const char* name() override       { return "OpenCode Go"; }
    const char* shortName() override  { return "opencode"; }

    // Branding
    const lv_image_dsc_t* logoSmall() override;
    const lv_image_dsc_t* logoWordmark() override;
    const lv_image_dsc_t* logoBig() override;
    const lv_image_dsc_t* logoXl() override;
    uint32_t accentColor() override   { return 0x22C55E; }

    // Dashboard scraping (sem API polling nem model probing)
    bool hasApiPolling() override         { return false; }
    bool hasDashboardScraping() override  { return true; }
    bool fetchDashboardUsage(const char* workspaceId,
                             const char* authCookie,
                             OpenCodeUsage& out) override;

    // Poll minimo 300s (scraping nao pode ser agressivo)
    int effectivePollSec(int configured) override {
        return configured < OC_MIN_POLL_SEC ? OC_MIN_POLL_SEC : configured;
    }

    // Ultimo erro
    int lastErrorCode() override         { return _lastCode; }
    const char* lastErrorMessage() override { return _lastMsg; }
    const char* lastErrorHint() override    { return _lastHint; }

    // Rede
    const char* mDnsHostname() override  { return "opencode-stick"; }
    const char* apSsid() override        { return "OpenCodeStick-Setup"; }
    const char* nvsNamespace() override  { return "opencode"; }

private:
    bool parseWindow(const String& html, const char* prefix, float& pct, long& sec);
    bool parseDataSlot(const String& html, OpenCodeUsage& out);

    int    _lastCode = 0;
    char   _lastMsg[96] = {0};
    char   _lastHint[96] = {0};
    String _etag;
    uint32_t _backoffMs = 0;
};
