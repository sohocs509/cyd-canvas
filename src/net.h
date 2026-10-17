// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <Arduino.h>

#define CYD_VERSION "0.1.0-dev"

namespace net {
void begin();                 // starts the network task
bool connected();
String ip();
String name();
String infoText(bool revealKey);  // device info screen
String statusLine();
bool setWifi(const String& ssid, const String& pass);
void setBrightness(int percent);
int brightness();
void screensChanged();        // republish Home Assistant discovery
void resetCredentials();
String apiKey();      // physical-access only (USB console, device screen)
String tlsPin();      // new API key + new certificate on next boot
}
