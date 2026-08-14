#pragma once
#define LV_CONF_INCLUDE_SIMPLE 1

#define LV_COLOR_DEPTH 16
#define LV_COLOR_16_SWAP 1   // если цвета выйдут "перевёрнутыми" — поставь 0

#define LV_MEM_CUSTOM 0
#define LV_MEM_SIZE (48U * 1024U)

#define LV_TICK_CUSTOM 1
#define LV_TICK_CUSTOM_INCLUDE "Arduino.h"
#define LV_TICK_CUSTOM_SYS_TIME_EXPR (millis())

#define LV_USE_LABEL 1
#define LV_USE_ARC   1
#define LV_USE_IMG   0
#define LV_USE_ANIMIMG 0

#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_22 1
#define LV_FONT_MONTSERRAT_32 1
#define LV_FONT_DEFAULT &lv_font_montserrat_14

#define LV_USE_LOG 0
#define LV_USE_ASSERT_NULL 1
