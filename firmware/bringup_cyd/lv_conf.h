/**
 * lv_conf.h — config do LVGL 9.2 para o bring-up da CYD (ESP32-2432S028).
 *
 * Compile com -DLV_CONF_INCLUDE_SIMPLE (ver build.sh) para que o LVGL ache
 * este arquivo pelo include path do sketch.
 *
 * Sem PSRAM nessa placa (ESP32 clássico, ~320KB RAM total) — pool do LVGL
 * reduzido em relação ao lv_conf.h do claude_stick (S3+PSRAM).
 */
#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

/*====================
   COLOR
 *====================*/
#define LV_COLOR_DEPTH 16
#define LV_COLOR_16_SWAP 0

/*=========================
   MEMÓRIA
   Pool interno do LVGL (objetos/estilos), em RAM comum — sem PSRAM.
   O buffer de render é pequeno (partial render, linhas), alocado no sketch.
 *=========================*/
#define LV_USE_STDLIB_MALLOC   LV_STDLIB_BUILTIN
#define LV_USE_STDLIB_STRING   LV_STDLIB_BUILTIN
#define LV_USE_STDLIB_SPRINTF  LV_STDLIB_BUILTIN
#define LV_MEM_SIZE            (32 * 1024U)

/*====================
   HAL / SISTEMA
 *====================*/
#define LV_USE_OS LV_OS_NONE
/* tick vem de lv_tick_set_cb(millis) no sketch */

/*====================
   RENDER
 *====================*/
#define LV_USE_DRAW_SW 1

/*====================
   FONTES
 *====================*/
#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_18 1
#define LV_FONT_DEFAULT &lv_font_montserrat_14

/*====================
   WIDGETS usados no bring-up
 *====================*/
#define LV_USE_LABEL  1
#define LV_USE_BUTTON 1

#endif /*LV_CONF_H*/
