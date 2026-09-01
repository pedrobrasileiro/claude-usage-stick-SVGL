#include "battery.h"

#ifdef BATTERY_ADC_PIN
#include <Arduino.h>
#include "ui_helpers.h"

// Percentual sem arredondar — usado internamente pela detecção de carga,
// já que a carga é lenta demais pra confiar na resolução de 1% inteiro.
static float battery_read_percent_f() {
  long sum = 0;
  for (int i = 0; i < BATTERY_ADC_SAMPLES; i++) sum += analogRead(BATTERY_ADC_PIN);
  float raw = sum / (float)BATTERY_ADC_SAMPLES;
  float v = (raw / (float)BATTERY_ADC_RESOLUTION) * 3.3f * BATTERY_DIVIDER_RATIO;
  float pct = (v - BATTERY_VOLT_EMPTY) / (BATTERY_VOLT_FULL - BATTERY_VOLT_EMPTY) * 100.0f;
  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;
  return pct;
}

int battery_read_percent() {
  float pct = battery_read_percent_f();
  long sum = 0;
  for (int i = 0; i < BATTERY_ADC_SAMPLES; i++) sum += analogRead(BATTERY_ADC_PIN);
  float raw = sum / (float)BATTERY_ADC_SAMPLES;
  float v = (raw / (float)BATTERY_ADC_RESOLUTION) * 3.3f * BATTERY_DIVIDER_RATIO;
  Serial.printf("[BATT] raw=%.1f v=%.3f pct=%.1f\n", raw, v, pct);
  return (int)(pct + 0.5f);
}

uint32_t battery_color(int pct) {
  if (pct > BATTERY_PCT_GREEN_MIN) return C_OK;
  if (pct >= BATTERY_PCT_YELLOW_MIN) return C_WARN;
  return C_BAD;
}

bool battery_is_charging() {
  static uint32_t lastWindowMs = 0;
  static float baselinePct = -1.0f;
  static bool charging = false;

  float pct = battery_read_percent_f();
  uint32_t now = millis();
  if (baselinePct < 0.0f) {
    baselinePct = pct;
    lastWindowMs = now;
  } else if (now - lastWindowMs >= BATTERY_CHARGE_WINDOW_MS) {
    charging = (pct - baselinePct) >= BATTERY_CHARGE_MIN_DELTA;
    baselinePct = pct;
    lastWindowMs = now;
  }
  return charging;
}

#endif // BATTERY_ADC_PIN
