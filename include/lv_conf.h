#ifndef LV_CONF_H
#define LV_CONF_H

/*====================
   COLOR SETTINGS
 *====================*/

#define LV_COLOR_DEPTH 16

/*=========================
   MEMORY SETTINGS
 *=========================*/

#define LV_MEM_CUSTOM 0
#define LV_MEM_SIZE (48U * 1024U)

/*====================
   HAL SETTINGS
 *====================*/

#define LV_TICK_CUSTOM 0

#define LV_DPI_DEF 130

/*========================
   DRAWING CONFIGURATION
 *========================*/

#define LV_DRAW_COMPLEX 1

#define LV_SHADOW_CACHE_SIZE 0

#define LV_CIRCLE_CACHE_SIZE 4

/*==================
   GPU CONFIGURATION
 *==================*/

#define LV_USE_GPU 0

/*==================
   LOGGING
 *==================*/

#define LV_USE_LOG 0

/*==================
   ASSERTS
 *==================*/

#define LV_USE_ASSERT_NULL 1
#define LV_USE_ASSERT_MALLOC 1
#define LV_USE_ASSERT_STYLE 0
#define LV_USE_ASSERT_MEM_INTEGRITY 0
#define LV_USE_ASSERT_OBJ 0

/*==================
   FONT USAGE
 *==================*/

#define LV_FONT_MONTSERRAT_8  0
#define LV_FONT_MONTSERRAT_10 0
#define LV_FONT_MONTSERRAT_12 0
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_18 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_22 1
#define LV_FONT_MONTSERRAT_24 1
#define LV_FONT_MONTSERRAT_26 0
#define LV_FONT_MONTSERRAT_28 0
#define LV_FONT_MONTSERRAT_30 0
#define LV_FONT_MONTSERRAT_32 0
#define LV_FONT_MONTSERRAT_34 0
#define LV_FONT_MONTSERRAT_36 0
#define LV_FONT_MONTSERRAT_38 0
#define LV_FONT_MONTSERRAT_40 0
#define LV_FONT_MONTSERRAT_42 0
#define LV_FONT_MONTSERRAT_44 0
#define LV_FONT_MONTSERRAT_46 0
#define LV_FONT_MONTSERRAT_48 0
#define LV_FONT_MONTSERRAT_50 0
#define LV_FONT_MONTSERRAT_52 0
#define LV_FONT_MONTSERRAT_54 0
#define LV_FONT_MONTSERRAT_56 0
#define LV_FONT_MONTSERRAT_58 0
#define LV_FONT_MONTSERRAT_60 0

#define LV_FONT_DEFAULT &lv_font_montserrat_14

/*==================
   TEXT SETTINGS
 *==================*/

#define LV_TXT_ENC LV_TXT_ENC_UTF8

#define LV_USE_BIDI 0
#define LV_USE_ARABIC_PERSIAN_CHARS 1

/*==================
   WIDGETS
 *==================*/

#define LV_USE_ARC 1
#define LV_USE_BAR 1
#define LV_USE_BTN 1
#define LV_USE_BTNMATRIX 1
#define LV_USE_CANVAS 1
#define LV_USE_CHECKBOX 1
#define LV_USE_DROPDOWN 1
#define LV_USE_IMG 1
#define LV_USE_LABEL 1
#define LV_USE_LINE 1
#define LV_USE_ROLLER 1
#define LV_USE_SLIDER 1
#define LV_USE_SWITCH 1
#define LV_USE_TABLE 1
#define LV_USE_TEXTAREA 1

/*==================
   EXTRA COMPONENTS
 *==================*/

#define LV_USE_ANIMIMG 0
#define LV_USE_CALENDAR 0
#define LV_USE_CHART 1
#define LV_USE_COLORWHEEL 0
#define LV_USE_IMGBTN 0
#define LV_USE_KEYBOARD 0
#define LV_USE_LED 0
#define LV_USE_LIST 1
#define LV_USE_MENU 0
#define LV_USE_METER 0
#define LV_USE_MSG 0
#define LV_USE_SPAN 0
#define LV_USE_SPINBOX 0
#define LV_USE_SPINNER 1
#define LV_USE_TABVIEW 0
#define LV_USE_TILEVIEW 1
#define LV_USE_WIN 0

/*==================
   THEMES
 *==================*/

#define LV_USE_THEME_DEFAULT 1
#define LV_USE_THEME_BASIC 1
#define LV_USE_THEME_MONO 0

/*==================
   LAYOUTS
 *==================*/

#define LV_USE_FLEX 1
#define LV_USE_GRID 1

/*==================
   FILE SYSTEM
 *==================*/

#define LV_USE_FS_STDIO 0
#define LV_USE_FS_POSIX 0
#define LV_USE_FS_WIN32 0
#define LV_USE_FS_FATFS 0

/*==================
   IMAGES
 *==================*/

#define LV_USE_PNG 0
#define LV_USE_BMP 0
#define LV_USE_SJPG 0
#define LV_USE_GIF 0
#define LV_USE_QRCODE 0

/*==================
   OTHERS
 *==================*/

#define LV_USE_SNAPSHOT 0
#define LV_USE_MONITOR 0
#define LV_USE_PERF_MONITOR 0
#define LV_USE_MEM_MONITOR 0
#define LV_USE_REFR_DEBUG 0

/*==================
   DEBUG
 *==================*/

#define LV_USE_DEBUG 0

#endif /* LV_CONF_H */
