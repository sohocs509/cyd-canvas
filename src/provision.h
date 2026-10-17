// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <Arduino.h>

// Wi-Fi setup over Bluetooth LE using the Improv Wi-Fi standard (https://www.improv-wifi.com/ble/).
// Setup is a separate boot mode: the board runs only the display and Bluetooth (no TLS pool, no web server,
// no SD), and restarts into normal mode as soon as working credentials are saved. Normal mode releases the
// Bluetooth memory at boot, so day-to-day running never pays for it.
namespace provision {
bool needed();                 // true when no Wi-Fi is saved or setup was requested
void requestAndRestart(bool networkLost = false);   // reboot into setup: asked for (menu/API) or the saved network is gone
void run(const char* deviceName);   // never returns: ends in a restart
}
