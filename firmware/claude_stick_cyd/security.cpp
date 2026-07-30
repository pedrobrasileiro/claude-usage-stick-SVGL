#include <Arduino.h>
#include "state_security.h"
#include "state_app.h"
#include "storage.h"
#include "config.h"

// Confere PIN contra g_blob, aplicando lockout exponencial (10s base,
// dobra a cada falha, sem limite de tentativas e sem wipe).
bool verify_pin_or_lockout(const String &pin, char *errMsg, size_t errLen, bool *wipedOut,
                            char *tokenOut, size_t tokenOutLen) {
  if (wipedOut) *wipedOut = false;
  if (millis() < g_lockoutUntil) {
    int rem = (g_lockoutUntil - millis()) / 1000;
    snprintf(errMsg, errLen, TRS("Aguarde %ds", "Wait %ds"), rem);
    return false;
  }
  char tmp[200];
  char *dst = tokenOut ? tokenOut : tmp;
  size_t dstLen = tokenOut ? tokenOutLen : sizeof(tmp);
  if (decryptToken(g_blob, pin.c_str(), dst, dstLen)) {
    g_pinAttempts = 0; save_attempts();
    return true;
  }
  g_pinAttempts++; save_attempts();
  int wait = LOCKOUT_BASE_SEC * (1 << (g_pinAttempts - 1));
  if (wait > 3600) wait = 3600;
  g_lockoutStartMs = millis();
  g_lockoutUntil = millis() + (uint32_t)wait * 1000;
  snprintf(errMsg, errLen, TRS("PIN errado. Aguarde %ds", "Wrong PIN. Wait %ds"), wait);
  return false;
}

bool settings_unlocked() { return millis() < g_settingsUnlockedUntil; }
