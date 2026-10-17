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

#endif
#endif
