#include "providers/claude_provider.h"
#include "api.h"
#include "status.h"
#include "logo_assets.h"

const lv_image_dsc_t* ClaudeProvider::logoSmall()    { return &img_clawd_sm; }
const lv_image_dsc_t* ClaudeProvider::logoWordmark() { return &img_wordmark; }
const lv_image_dsc_t* ClaudeProvider::logoBig()      { return &img_clawd_big; }
const lv_image_dsc_t* ClaudeProvider::logoXl()       { return &img_clawd_xl; }

bool ClaudeProvider::fetchUsage(const char* token, UsageData& out) {
    return ::fetchUsage(token, out);
}

const char* ClaudeProvider::modelName(int idx) {
    static const char* names[] = {"Haiku", "Sonnet", "Opus", "Fable"};
    return (idx >= 0 && idx < 4) ? names[idx] : "";
}

const char* ClaudeProvider::modelId(int idx) {
    static const char* ids[] = {
        "claude-haiku-4-5-20251001",
        "claude-sonnet-5",
        "claude-opus-4-8",
        "claude-fable-5",
    };
    return (idx >= 0 && idx < 4) ? ids[idx] : "";
}

bool ClaudeProvider::probeModel(const char* token, const char* modelId, ProbeResult& out) {
    return ::probeModel(token, modelId, out);
}

bool ClaudeProvider::fetchModelStatus(ModelStatus& out) {
    return ::fetchModelStatus(out);
}
