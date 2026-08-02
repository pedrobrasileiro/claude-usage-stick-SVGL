#pragma once
#include "provider.h"

class ClaudeProvider : public AIProvider {
public:
    const char* name() override       { return "Claude Code"; }
    const char* shortName() override  { return "claude"; }

    // Branding
    const lv_image_dsc_t* logoSmall() override;
    const lv_image_dsc_t* logoWordmark() override;
    const lv_image_dsc_t* logoBig() override;
    const lv_image_dsc_t* logoXl() override;
    uint32_t accentColor() override   { return 0xD97757; }

    // API polling
    bool hasApiPolling() override     { return true; }
    bool fetchUsage(const char* token, UsageData& out) override;
    int  modelCount() override        { return 4; }
    const char* modelName(int idx) override;
    const char* modelId(int idx) override;
    bool hasModelProbing() override   { return true; }
    bool probeModel(const char* token, const char* modelId, ProbeResult& out) override;
    bool hasStatusEndpoint() override { return true; }
    bool fetchModelStatus(ModelStatus& out) override;

    // Rede
    const char* messagesEndpoint() override { return "https://api.anthropic.com/v1/messages"; }
    const char* userAgent() override        { return "claude-code/2.1.5"; }
    const char* mDnsHostname() override     { return "claude-stick"; }
    const char* apSsid() override           { return "ClaudeStick-Setup"; }
    const char* nvsNamespace() override     { return "claude"; }
};
