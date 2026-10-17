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

static const char* resetReason() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON: return "power-on";
    case ESP_RST_SW: return "software restart";
    case ESP_RST_PANIC: return "crash (panic)";
    case ESP_RST_INT_WDT: return "interrupt watchdog";
    case ESP_RST_TASK_WDT: return "task watchdog";
    case ESP_RST_WDT: return "watchdog";
    case ESP_RST_BROWNOUT: return "BROWNOUT (power dip)";
    case ESP_RST_DEEPSLEEP: return "deep sleep";
    case ESP_RST_EXT: return "reset pin";
    default: return "unknown";
  }
}
const char* g_resetReason = "";

void setup() {
  g_resetReason = resetReason();
  // Nothing uses Bluetooth at runtime: hand its reserved DRAM back to the heap (must happen before BT init).
  esp_bt_controller_mem_release(ESP_BT_MODE_BTDM);
  bool pool = tlsmem::begin(48 * 1024);   // before anything else fragments the heap
  Serial.begin(115200);
  Serial.printf("reset reason: %s\n", g_resetReason);
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
