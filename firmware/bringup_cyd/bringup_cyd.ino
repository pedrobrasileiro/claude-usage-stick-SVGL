/**
 * Bring-up touch XPT2046 — ESP32-2432S028 (CYD).
 *
 * Pinagem confirmada no manual/schematic oficial do fabricante
 * (Manual_Pinagem_Foto.pdf, chip U3): TP_CLK=25, TP_CS=33, TP_DIN(MOSI)=32,
 * TP_OUT(MISO)=39, TP_IRQ=36. Barramento SPI separado do display (que usa
 * 12/13/14/15), sem compartilhamento.
 *
 * Inicializa o display ILI9341 (init já validado em bring-up anterior) e o
 * touch em SPI dedicado (HSPI). A cada toque: imprime raw x/y/z no Serial e
 * desenha um ponto na posição mapeada — toque nos 4 cantos da tela e
 * confira visualmente se o ponto aparece onde o dedo tocou.
 *
 * Toque e segure ~1s em qualquer ponto pra imprimir sugestão de calibração
 * (min/max acumulados) — colete tocando nos 4 cantos antes de ler.
 */
#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <SPI.h>
#include <XPT2046_Touchscreen.h>
#include "config.h"

Arduino_GFX *gfx = nullptr;
SPIClass displaySPI(VSPI);
SPIClass touchSPI(HSPI);
XPT2046_Touchscreen ts(TOUCH_CS, TOUCH_IRQ);

static uint16_t g_xMin = 65535, g_xMax = 0, g_yMin = 65535, g_yMax = 0;

static void print_calibration_hint() {
    Serial.println("\n--- calibracao acumulada ate agora ---");
    Serial.printf("#define TOUCH_X_MIN %u\n", g_xMin);
    Serial.printf("#define TOUCH_X_MAX %u\n", g_xMax);
    Serial.printf("#define TOUCH_Y_MIN %u\n", g_yMin);
    Serial.printf("#define TOUCH_Y_MAX %u\n", g_yMax);
    Serial.println("(toque nos 4 cantos da tela antes de considerar final)\n");
}

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n=== Bring-up touch XPT2046 (CYD) ===");

    displaySPI.begin(TFT_SCLK, TFT_MISO, TFT_MOSI);
    Arduino_DataBus *bus = new Arduino_HWSPI(TFT_DC, TFT_CS, TFT_SCLK, TFT_MOSI, TFT_MISO, &displaySPI);
    gfx = new Arduino_ILI9341(bus, TFT_RST, TFT_ROTATION, false);
    if (!gfx->begin(SPI_FREQ)) {
        Serial.println("FATAL: display nao inicializou");
        while (1) delay(1000);
    }
    gfx->invertDisplay(true);
    gfx->fillScreen(0x0000);
    gfx->setTextColor(0xFFFF);
    gfx->setCursor(10, 10);
    gfx->print("Toque na tela");

    ledcAttach(TFT_BL, 5000, 8);
    ledcWrite(TFT_BL, 255); // backlight no máximo

    touchSPI.begin(TOUCH_CLK, TOUCH_MISO, TOUCH_MOSI);
    if (!ts.begin(touchSPI)) {
        Serial.println("FATAL: touch XPT2046 nao respondeu no begin()");
        while (1) delay(1000);
    }
    ts.setRotation(1);
    Serial.println("Display e touch inicializados. Toque nos 4 cantos da tela.");
}

void loop() {
    if (ts.touched()) {
        TS_Point p = ts.getPoint();
        Serial.printf("raw x=%d y=%d z=%d\n", p.x, p.y, p.z);

        if ((uint16_t)p.x < g_xMin) g_xMin = p.x;
        if ((uint16_t)p.x > g_xMax) g_xMax = p.x;
        if ((uint16_t)p.y < g_yMin) g_yMin = p.y;
        if ((uint16_t)p.y > g_yMax) g_yMax = p.y;

        // Mapeamento provisório pra validação visual — ajustar min/max
        // conforme leitura real antes de promover pro app principal.
        int sx = map(p.x, TOUCH_X_MIN, TOUCH_X_MAX, 0, SCREEN_WIDTH - 1);
        int sy = map(p.y, TOUCH_Y_MIN, TOUCH_Y_MAX, 0, SCREEN_HEIGHT - 1);
        sx = constrain(sx, 0, SCREEN_WIDTH - 1);
        sy = constrain(sy, 0, SCREEN_HEIGHT - 1);

        gfx->fillCircle(sx, sy, 4, 0xF800);
        Serial.printf("mapeado: x=%d y=%d\n", sx, sy);

        print_calibration_hint();
        delay(300); // debounce simples
    }
}
