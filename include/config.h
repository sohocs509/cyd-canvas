// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <Arduino.h>

#ifdef BOARD_CYD_2432S028R
constexpr int SCREEN_W = 320, SCREEN_H = 240;
constexpr int PIN_SD_SCK = 18, PIN_SD_MISO = 19, PIN_SD_MOSI = 23, PIN_SD_CS = 5;
// Touch is bit-banged: SD owns the VSPI peripheral, the display owns HSPI.
constexpr int PIN_T_CLK = 25, PIN_T_MISO = 39, PIN_T_MOSI = 32, PIN_T_CS = 33, PIN_T_IRQ = 36;
constexpr int PIN_LED_R = 4, PIN_LED_G = 16, PIN_LED_B = 17;  // active low
constexpr int PIN_LDR = 34, PIN_BOOT = 0;
#else
#error "Select a board environment in platformio.ini"
#endif

constexpr size_t MAX_SCREEN_BYTES = 16 * 1024;
constexpr size_t MAX_ASSET_BYTES = 512 * 1024;
