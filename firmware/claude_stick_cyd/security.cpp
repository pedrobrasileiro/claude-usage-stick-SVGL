#include <Arduino.h>
#include "state_security.h"
#include "state_app.h"
#include "storage.h"
#include "config.h"

// Confere PIN contra g_blob, aplicando o mesmo lockout exponencial/wipe do
// desbloqueio de boot. Retorna true só quando o PIN bate (e zera as
// tentativas); preenche errMsg com mensagem pronta pra UI em caso de espera/
// erro/wipe. wipedOut (se não-nulo) sai true quando o limite de tentativas
// estourou e factory_reset() já rodou. tokenOut/tokenOutLen (se não-nulo)
// recebem o token decifrado — sem eles o decrypt só serve pra validar o PIN.
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
  if (g_pinAttempts >= MAX_PIN_ATTEMPTS) {
    Serial.println("[PIN] limite estourado -> wipe");
    factory_reset();
    g_forceWifi = true; g_forceToken = true;
    if (wipedOut) *wipedOut = true;
    snprintf(errMsg, errLen, "%s",
             TRS("PIN errado demais vezes — tudo apagado. Configure de novo.",
                 "Wrong PIN too many times — everything wiped. Reconfigure."));
    request_state(ST_PROVISION);
    return false;
  }
  int wait = LOCKOUT_BASE_SEC * (1 << (g_pinAttempts - 1));
  if (wait > 3600) wait = 3600;
  g_lockoutUntil = millis() + (uint32_t)wait * 1000;
  snprintf(errMsg, errLen, TRS("PIN errado (%d/%d). Aguarde %ds", "Wrong PIN (%d/%d). Wait %ds"),
           g_pinAttempts, MAX_PIN_ATTEMPTS, wait);
  return false;
}

bool settings_unlocked() { return millis() < g_settingsUnlockedUntil; }
