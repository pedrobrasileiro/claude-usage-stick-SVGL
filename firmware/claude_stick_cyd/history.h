#pragma once
#include <stdint.h>

// ============================================================
// Histórico / heatmap — ring buffer de amostras (5h/7d) e acúmulo
// de consumo por hora do dia, persistidos em LittleFS.
// ============================================================

#define HIST_MAX 160
struct Sample { uint32_t t; uint8_t h5; uint8_t d7; };   // t = epoch (0 = relógio não sincronizado)

#define NDAYS 31
struct DayHeat { uint32_t day; float burn[24]; };   // day = dias locais desde epoch

extern Sample g_hist[HIST_MAX];
extern int g_histN;
extern int g_histHead;
extern float g_hourBurn[24];   // consumo por hora do dia (todo o tempo)
extern float g_lastH5;         // última utilização 5h (delta do heatmap)
extern DayHeat g_days[NDAYS];
extern int g_dayN;

void hist_push(float h5, float d7);
int hist_idx(int i);
uint32_t day_key();
void accumulate_heat(float h5);
void heat_mode_data(int mode, float out[24]);   // 0=hoje 1=7d 2=30d 3=tudo
void save_history();   // LittleFS
void load_history();
