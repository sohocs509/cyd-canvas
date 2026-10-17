// SPDX-License-Identifier: Apache-2.0
#include <Arduino.h>
#include "config.h"
#include "console.h"
#include "hal.h"
#include "net.h"
#include "store.h"
#include "tlsmem.h"
#include "ui.h"

#include <esp_bt.h>

void setup() {
  // Nothing uses Bluetooth at runtime: hand its reserved DRAM back to the heap (must happen before BT init).
  esp_bt_controller_mem_release(ESP_BT_MODE_BTDM);
  bool pool = tlsmem::begin(48 * 1024);   // before anything else fragments the heap
  Serial.begin(115200);
  Serial.printf("tls pool: %s\n", pool ? "reserved 48 KB" : "FAILED (using normal heap)");
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
