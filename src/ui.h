// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

namespace ui {
void begin();
void loop();                 // call often; runs LVGL and BOOT-button navigation
void refreshIndex();         // rescan /screens; safe from any task
bool show(const String& id); // request navigation (applied in loop())
void showMenu();
String current();            // "" when the menu is showing
void setStatusLine(const String& s);  // shown at the bottom of the menu

// Merge readings into a screen's data and update it live if open. Safe from any task.
bool applyData(const String& id, JsonObjectConst data);  // false if no such screen
void forgetScreen(const String& id);  // drop cached data after delete
size_t screenCount();
String debugData(const String& id);  // what the device holds for a screen

bool clockValid();                              // true once NTP has set the time
bool parseUtc(const char* iso, int64_t& epoch); // "2026-10-20T18:00:00Z" only
void replaceData(const String& id, JsonObjectConst data);  // embedded "data" of an uploaded screen
String indexJson();                             // [{id,title,menu,expires}]  // what the device holds for a screen
}
