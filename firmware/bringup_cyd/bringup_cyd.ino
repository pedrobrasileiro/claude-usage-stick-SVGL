/**
 * Scanner I2C exaustivo — testa TODOS os pares de GPIO plausíveis como
 * SDA/SCL, sem assumir pinout de nenhuma fonte específica. Só imprime
 * quando encontra algo (silencioso nos "nada encontrado" pra não afogar
 * o serial com ruído).
 */
#include <Arduino.h>
#include <Wire.h>

// GPIOs de uso geral em ESP32 clássico, excluindo: flash (6-11), UART0 (1,3),
// e os já confirmados como display SPI (2 DC, 12 MISO, 13 MOSI, 14 SCK, 15 CS).
static const int CANDIDATES[] = {4, 5, 16, 17, 18, 19, 21, 22, 23, 25, 26, 27, 32, 33, 34, 35, 36, 39};
static const int N = sizeof(CANDIDATES) / sizeof(CANDIDATES[0]);

void setup() {
    Serial.begin(115200);
    delay(1500);
    Serial.println("\n=== Scanner I2C exaustivo (todos os pares de GPIO) ===");
    Serial.printf("Testando %d pinos, %d pares (pode demorar)...\n", N, N * (N - 1));
}

void loop() {
    for (int i = 0; i < N; i++) {
        for (int j = 0; j < N; j++) {
            if (i == j) continue;
            int sda = CANDIDATES[i];
            int scl = CANDIDATES[j];
            // SCL precisa ser saída válida (pula pinos input-only 34,35,36,39 como SCL)
            if (scl == 34 || scl == 35 || scl == 36 || scl == 39) continue;

            Wire.end();
            delay(3);
            Wire.begin(sda, scl, 100000);
            delay(3);
            for (uint8_t addr = 1; addr < 127; addr++) {
                Wire.beginTransmission(addr);
                if (Wire.endTransmission() == 0) {
                    Serial.printf(">>> ACHOU! SDA=%d SCL=%d addr=0x%02X\n", sda, scl, addr);
                }
            }
        }
        Serial.printf("... pino base %d testado (%d/%d)\n", CANDIDATES[i], i + 1, N);
    }
    Serial.println("=== varredura completa, repetindo ===\n");
    delay(3000);
}
