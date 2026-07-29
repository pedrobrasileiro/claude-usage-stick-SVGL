#pragma once
#include <Arduino.h>
#include "config.h"
#include "crypto.h"

// ============================================================
// Estado de segurança: token cifrado, PIN, lockout e sessão de
// Ajustes. Consumido por 3 frentes: tela PIN touch, handlers web
// (/settings) e portal de provisionamento — ver security.cpp.
// ============================================================

extern EncryptedBlob g_blob;
extern bool g_hasToken;                    // existe blob salvo no NVS
extern char g_token[200];                  // token decifrado (só em RAM)
extern char g_pinEntry[PIN_LEN + 1];       // dígitos sendo digitados na tela de PIN
extern bool g_pinForSettings;              // ST_PIN entrado pra gatear /Ajustes (não pra decifrar no boot)
extern int  g_pinAttempts;                 // tentativas erradas (persistido)
extern uint32_t g_lockoutUntil;            // millis até liberar nova tentativa
extern uint32_t g_settingsUnlockedUntil;   // millis até expirar sessão de Ajustes desbloqueado

// Confere PIN contra g_blob, aplicando o mesmo lockout exponencial/wipe do
// desbloqueio de boot. Retorna true só quando o PIN bate (e zera as
// tentativas); preenche errMsg com mensagem pronta pra UI em caso de espera/
// erro/wipe. wipedOut (se não-nulo) sai true quando o limite de tentativas
// estourou e factory_reset() já rodou. tokenOut/tokenOutLen (se não-nulo)
// recebem o token decifrado — sem eles o decrypt só serve pra validar o PIN.
bool verify_pin_or_lockout(const String &pin, char *errMsg, size_t errLen, bool *wipedOut = nullptr,
                            char *tokenOut = nullptr, size_t tokenOutLen = 0);
bool settings_unlocked();
