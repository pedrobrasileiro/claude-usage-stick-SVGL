#ifndef RGB_LED_H
#define RGB_LED_H

#include <Arduino.h>

// Controle do LED RGB WS2812 embutido na placa (GPIO42, ver config.h).
// Usa neopixelWrite() nativo do core ESP32 — sem lib externa.

enum LedMode { LED_OFF, LED_SOLID, LED_CYCLE };

extern LedMode g_ledMode;
extern uint8_t g_ledR, g_ledG, g_ledB;   // última cor efetivamente escrita (p/ indicador na tela)

void led_init();
void led_set(uint8_t r, uint8_t g, uint8_t b);
void led_set_mode(LedMode m);
void led_tick();   // chamar no loop() — avança a animação do modo LED_CYCLE

#endif // RGB_LED_H
