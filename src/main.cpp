// SPDX-License-Identifier: Apache-2.0
#include <Arduino.h>
#include "config.h"
#include "console.h"
#include "hal.h"
#include "net.h"
#include "store.h"
#include "ui.h"

void setup() {
  Serial.begin(115200);
  hal::begin();

  // Hold BOOT at power-up (or never calibrated) to run touch calibration.
  bool forceCal = false;
  if (hal::bootPressed()) {
    delay(1500);
    forceCal = hal::bootPressed();
  }
  if (forceCal || !hal::hasCalibration()) hal::calibrate();

  bool sd = store::begin();
  Serial.printf("SD: %s\n", sd ? "ok" : "missing");
  ui::begin();
  ui::refreshIndex();
  net::begin();
  console::begin();
}

void loop() {
  ui::loop();
  console::poll();
  delay(2);
}
