#include "state_security.h"

EncryptedBlob g_blob;
bool g_hasToken = false;
char g_token[200] = {0};
char g_pinEntry[PIN_LEN + 1] = {0};
bool g_pinForSettings = false;
int  g_pinAttempts = 0;
uint32_t g_lockoutUntil = 0;
uint32_t g_settingsUnlockedUntil = 0;

// verify_pin_or_lockout() e settings_unlocked() ainda estão implementadas em
// claude_stick_cyd.ino nesta etapa da extração (ver security.cpp, passo
// seguinte do refactor).
