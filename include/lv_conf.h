/* SPDX-License-Identifier: Apache-2.0 */
#if 1 /* enable */
#ifndef LV_CONF_H
#define LV_CONF_H

#define LV_COLOR_DEPTH 16
#define LV_USE_STDLIB_MALLOC LV_STDLIB_CLIB
#define LV_USE_STDLIB_STRING LV_STDLIB_CLIB
#define LV_USE_STDLIB_SPRINTF LV_STDLIB_CLIB
#define LV_USE_OS LV_OS_NONE
#define LV_USE_LOG 0
#define LV_USE_TFT_ESPI 1

#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_24 1
#define LV_FONT_MONTSERRAT_32 1
#define LV_FONT_DEFAULT &lv_font_montserrat_16

#define LV_THEME_DEFAULT_DARK 1
#define LV_USE_LED 1
#define LV_USE_TABLE 1
#define LV_USE_CHART 1
#define LV_USE_ARC 1
#define LV_USE_BAR 1


/* Widgets and features this project never draws are compiled out to save flash (about 20 are on by default). */
#define LV_USE_ANIMIMG 0
#define LV_USE_BUTTONMATRIX 0
#define LV_USE_CALENDAR 0
#define LV_USE_CANVAS 0
#define LV_USE_CHECKBOX 0
#define LV_USE_DROPDOWN 0
#define LV_USE_IMAGEBUTTON 0
#define LV_USE_KEYBOARD 0
#define LV_USE_MENU 0
#define LV_USE_MSGBOX 0
#define LV_USE_ROLLER 0
#define LV_USE_SCALE 0
#define LV_USE_SLIDER 0
#define LV_USE_SPAN 0
#define LV_USE_SPINBOX 0
#define LV_USE_SPINNER 0
#define LV_USE_SWITCH 0
#define LV_USE_TABVIEW 0
#define LV_USE_TEXTAREA 0
#define LV_USE_TILEVIEW 0
#define LV_USE_WIN 0
#define LV_USE_GRID 0
#define LV_FONT_MONTSERRAT_14 0
#define LV_USE_OBSERVER 0
#define LV_DRAW_SW_SUPPORT_RGB888 0
#define LV_DRAW_SW_SUPPORT_XRGB8888 0
#define LV_DRAW_SW_SUPPORT_AL88 0
#define LV_DRAW_SW_SUPPORT_A8 0
#define LV_DRAW_SW_SUPPORT_L8 0
#define LV_DRAW_SW_SUPPORT_I1 0
#define LV_USE_THEME_SIMPLE 0
#define LV_USE_THEME_MONO 0

#endif
#endif
