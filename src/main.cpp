// SPDX-License-Identifier: Apache-2.0
#include <Arduino.h>
#include "config.h"
#include "console.h"
#include "hal.h"
#include "net.h"
#include "store.h"
#include "tlsmem.h"
#include "provision.h"
#include "ui.h"

#include <WiFi.h>
#include <esp_bt.h>
#include <esp_heap_caps.h>

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

static void stage(const char* what) {
  Serial.printf("heap @ %-14s free=%u largest=%u\n", what, (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT), (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
}

void setup() {
  g_resetReason = resetReason();
  // Nothing uses Bluetooth at runtime: hand its reserved DRAM back to the heap (must happen before BT init).
  // Normal running needs no Bluetooth: give its DRAM back. Wi-Fi setup is a separate boot mode that keeps it.
  size_t freeBefore = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  bool provMode = provision::needed();
  esp_bt_controller_mem_release(provMode ? ESP_BT_MODE_CLASSIC_BT : ESP_BT_MODE_BTDM);
  size_t freeAfterRelease = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  bool pool = provMode ? false : tlsmem::begin(48 * 1024);   // before anything else fragments the heap
  Serial.begin(115200);
  Serial.printf("reset reason: %s\n", g_resetReason);
  Serial.printf("heap: %u before BT release, %u after (setup mode: %s)\n", (unsigned)freeBefore, (unsigned)freeAfterRelease, provMode ? "yes" : "no");
  Serial.printf("tls pool: %s\n", pool ? "reserved 48 KB" : "FAILED (using normal heap)");
  stage("after pool");
  hal::begin();
  stage("after hal");

  // Hold BOOT at power-up (or never calibrated) to run touch calibration.
  bool forceCal = false;
  if (hal::bootPressed()) {
    delay(1500);
    forceCal = hal::bootPressed();
  }
  if (forceCal || !hal::hasCalibration()) hal::calibrate();

  if (provMode) {
    uint8_t mac[6];
    WiFi.macAddress(mac);
    char nm[24];
    snprintf(nm, sizeof(nm), "cyd-%02x%02x%02x", mac[3], mac[4], mac[5]);
    provision::run(nm);           // never returns
  }
  bool sd = store::begin();
  Serial.printf("SD: %s\n", sd ? "ok" : "missing");
  stage("after store");
  ui::begin();
  stage("after ui");
  ui::refreshIndex();
  net::begin();
  stage("after net::begin");
  console::begin();
}

void loop() {
  ui::loop();
  console::poll();
  delay(2);
}
