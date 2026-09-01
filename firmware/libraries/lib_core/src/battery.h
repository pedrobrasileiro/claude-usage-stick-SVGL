#pragma once
#include <stdint.h>
#include "config.h"

// Leitura da bateria via ADC — só existe hardware (GPIO dedicado, conforme
// Manual Técnico Fikra 2.0) na variante Fikra. Nas demais, BATTERY_ADC_PIN
// não é definido e este módulo fica inerte.
#ifdef BATTERY_ADC_PIN

// 0-100, mapeado linearmente entre BATTERY_VOLT_EMPTY e BATTERY_VOLT_FULL.
int battery_read_percent();

// Verde/âmbar/vermelho conforme BATTERY_PCT_GREEN_MIN/YELLOW_MIN.
uint32_t battery_color(int pct);

// Não há pino de status de carga na placa — infere pela tendência de alta
// da tensão ao longo de BATTERY_CHARGE_WINDOW_MS (compara % não-arredondado,
// senão o arredondamento pro inteiro mascara a subida — carga é bem lenta).
bool battery_is_charging();

#endif // BATTERY_ADC_PIN
