#include "rgb_led.h"
#include "config.h"

#define LED_BRIGHT 60   // 0-255, limita o brilho do WS2812

LedMode g_ledMode = LED_OFF;
uint8_t g_ledR = 0, g_ledG = 0, g_ledB = 0;

static int      s_cycleStep = 0;
static uint32_t s_cycleNext = 0;

void led_init() { led_set(0, 0, 0); }

void led_set(uint8_t r, uint8_t g, uint8_t b) {
  g_ledR = r; g_ledG = g; g_ledB = b;
  neopixelWrite(RGB_LED_PIN,
                (uint8_t)((uint16_t)r * LED_BRIGHT / 255),
                (uint8_t)((uint16_t)g * LED_BRIGHT / 255),
                (uint8_t)((uint16_t)b * LED_BRIGHT / 255));
}

void led_set_mode(LedMode m) {
  g_ledMode = m;
  if (m == LED_OFF) led_set(0, 0, 0);
  else if (m == LED_CYCLE) { s_cycleStep = 0; s_cycleNext = 0; }
}

void led_tick() {
  if (g_ledMode != LED_CYCLE) return;
  uint32_t now = millis();
  if (now < s_cycleNext) return;
  static const uint8_t seq[3][3] = { {255, 0, 0}, {0, 255, 0}, {0, 0, 255} };
  const uint8_t *c = seq[s_cycleStep % 3];
  led_set(c[0], c[1], c[2]);
  s_cycleStep++;
  s_cycleNext = now + 400;
}
