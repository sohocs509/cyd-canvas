// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <Arduino.h>
#include <TFT_eSPI.h>

namespace hal {
void begin();
TFT_eSPI& display();
void backlight(uint8_t percent);
void led(bool r, bool g, bool b);
uint16_t ldrRaw();
bool bootPressed();

// Calibrated touch in screen pixels. Returns true while pressed.
bool touch(int16_t& x, int16_t& y);
bool hasCalibration();
void calibrate();  // interactive, 3 taps, stored in NVS
}
