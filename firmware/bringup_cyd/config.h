#ifndef CONFIG_H
#define CONFIG_H

// ============================================
// ESP32-2432S028 "Cheap Yellow Display" — pinos
// Confirmado no manual/schematic oficial do fabricante
// (Manual_Pinagem_Foto.pdf, chip U3 = XPT2046).
// ============================================

// Display SPI (ILI9341)
#define TFT_MOSI  13
#define TFT_MISO  12
#define TFT_SCLK  14
#define TFT_CS    15
#define TFT_DC    2
#define TFT_RST   -1   // não conectado nessa placa
#define TFT_BL    21

// Touch SPI (XPT2046) — barramento separado do display, HSPI dedicado
#define TOUCH_CLK   25
#define TOUCH_CS    33
#define TOUCH_MOSI  32
#define TOUCH_MISO  39
#define TOUCH_IRQ   36

// Calibração touch resistivo — validada tocando os 4 cantos (raw x 283-3645,
// raw y 452-3695), com margem
#define TOUCH_X_MIN 270
#define TOUCH_X_MAX 3660
#define TOUCH_Y_MIN 440
#define TOUCH_Y_MAX 3710

// Display — nativo 240x320 (retrato); paisagem via rotation
#define SCREEN_WIDTH   320
#define SCREEN_HEIGHT  240
#define TFT_ROTATION   1      // ajustar se orientação sair errada
#define SPI_FREQ       40000000UL

#endif // CONFIG_H
