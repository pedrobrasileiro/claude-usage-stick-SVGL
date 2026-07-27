#ifndef CONFIG_H
#define CONFIG_H

// ============================================
// ESP32-2432S028 "Cheap Yellow Display" — pinos
// (pinout público padrão desse modelo; confirmar no bring-up)
// ============================================

// Display SPI (ILI9341)
#define TFT_MOSI  13
#define TFT_MISO  12
#define TFT_SCLK  14
#define TFT_CS    15
#define TFT_DC    2
#define TFT_RST   -1   // não conectado nessa placa
#define TFT_BL    21

// Touch SPI (XPT2046) — mesmo barramento do display, CS/IRQ próprios
#define TOUCH_CS   33
#define TOUCH_IRQ  36

// Calibração touch resistivo (ajustar no bring-up conforme leitura real)
#define TOUCH_X_MIN 200
#define TOUCH_X_MAX 3800
#define TOUCH_Y_MIN 200
#define TOUCH_Y_MAX 3800

// Display — nativo 240x320 (retrato); paisagem via rotation
#define SCREEN_WIDTH   320
#define SCREEN_HEIGHT  240
#define TFT_ROTATION   1      // ajustar se orientação sair errada
#define SPI_FREQ       40000000UL

#endif // CONFIG_H
