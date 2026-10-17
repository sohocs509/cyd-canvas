// SPDX-License-Identifier: Apache-2.0
#include "console.h"
#include <esp_heap_caps.h>
#include "config.h"
#include "hal.h"
#include "net.h"
#include "store.h"
#include "tlsmem.h"
#include <WiFi.h>
#include <WiFiClient.h>
#include "ui.h"
#include <ArduinoJson.h>

extern uint32_t ui_flushes, ui_nonzero, ui_lastw, ui_lasth;
static String line;

void console::begin() { Serial.println("cyd-canvas console ready (type help)"); }

static void handle(String l) {
  l.trim();
  if (l.isEmpty()) return;
  int sp = l.indexOf(' ');
  String cmd = sp < 0 ? l : l.substring(0, sp);
  String rest = sp < 0 ? "" : l.substring(sp + 1);
  rest.trim();

  if (cmd == "help") {
    Serial.println("ls | put <id> <json> | data <id> <json> | show <id> | menu | rm <id> | cal | info");
  } else if (cmd == "info") {
    Serial.printf("flushes=%u last=%ux%u nonzero=%u ", (unsigned)ui_flushes, (unsigned)ui_lastw, (unsigned)ui_lasth, (unsigned)ui_nonzero);
    Serial.printf("largest=%u heap=%u sd=%llu/%llu MB screens=%u\n", (unsigned)ESP.getMaxAllocHeap(), (unsigned)ESP.getFreeHeap(),
                  store::usedBytes() >> 20, store::totalBytes() >> 20, (unsigned)ui::screenCount());
  } else if (cmd == "ls") {
    for (auto& n : store::listScreenIds()) Serial.println(n);
  } else if (cmd == "put") {
    int s2 = rest.indexOf(' ');
    String id = s2 < 0 ? rest : rest.substring(0, s2);
    String body = s2 < 0 ? "" : rest.substring(s2 + 1);
    JsonDocument doc;
    if (!store::validId(id) || deserializeJson(doc, body) || String(doc["id"] | "") != id) {
      Serial.println("ERR bad id or json (id must match)");
      return;
    }
    bool ok = store::writeFileAtomic(store::screenPath(id), (const uint8_t*)body.c_str(), body.length());
    ui::refreshIndex();
    ui::cardUpdated(id);
    Serial.println(ok ? "OK" : "ERR write");
  } else if (cmd == "data") {
    int s2 = rest.indexOf(' ');
    String id = s2 < 0 ? rest : rest.substring(0, s2);
    JsonDocument doc;
    if (s2 < 0 || deserializeJson(doc, rest.substring(s2 + 1)) || !doc.is<JsonObject>()) {
      Serial.println("ERR bad json");
      return;
    }
    ui::applyData(id, doc.as<JsonObjectConst>());
    Serial.println("OK");
  } else if (cmd == "dbg") {
    Serial.println(ui::debugData(rest));
  } else if (cmd == "show") {
    Serial.println(ui::show(rest) ? "OK" : "ERR no such screen");
  } else if (cmd == "menu") {
    ui::showMenu();
    Serial.println("OK");
  } else if (cmd == "rm") {
    bool ok = store::validId(rest) && store::removeFile(store::screenPath(rest));
    store::removeFile(store::dataPath(rest));
    ui::forgetScreen(rest);
    Serial.println(ok ? "OK" : "ERR");
  } else if (cmd == "wifi") {
    int s2 = rest.indexOf(' ');
    String ssid = s2 < 0 ? rest : rest.substring(0, s2);
    String pw = s2 < 0 ? "" : rest.substring(s2 + 1);
    Serial.println(net::setWifi(ssid, pw) ? "OK" : "ERR bad ssid/password");
  } else if (cmd == "probe") {
    IPAddress ip;
    bool alive = rest.isEmpty() ? net::probe() : (ip.fromString(rest) && net::probe(ip));
    Serial.printf("probe %s -> %s\n", rest.isEmpty() ? "gateway" : rest.c_str(), alive ? "ALIVE" : "NO ANSWER");
  } else if (cmd == "nettest") {
    Serial.printf("wifi status=%d rssi=%d ip=%s gw=%s mac=%s\n", (int)WiFi.status(), WiFi.RSSI(), WiFi.localIP().toString().c_str(),
                  WiFi.gatewayIP().toString().c_str(), WiFi.BSSIDstr().c_str());
    // nettest [host port]: outbound TCP connects from the device itself (default: the router's port 80)
    String host = WiFi.gatewayIP().toString();
    int port = 80;
    int sp = rest.indexOf(' ');
    if (sp > 0) { host = rest.substring(0, sp); port = rest.substring(sp + 1).toInt(); }
    WiFiClient c;
    uint32_t t = millis();
    bool ok = c.connect(host.c_str(), port, 3000);
    Serial.printf("connect %s:%d -> %s in %lu ms\n", host.c_str(), port, ok ? "OK" : "FAIL", (unsigned long)(millis() - t));
    c.stop();
  } else if (cmd == "heap") {
    Serial.printf("tls pool: free=%u largest=%u\n", (unsigned)tlsmem::freeBytes(), (unsigned)tlsmem::largestFree());
    Serial.printf("integrity=%s\n", heap_caps_check_integrity_all(true) ? "OK" : "CORRUPT");
    heap_caps_print_heap_info(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    void* a = heap_caps_malloc(17000, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    void* b = heap_caps_malloc(17000, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    Serial.printf("two 17000-byte allocs: %s %s\n", a ? "ok" : "FAIL", b ? "ok" : "FAIL");
    if (a) heap_caps_free(a);
    if (b) heap_caps_free(b);
  } else if (cmd == "key") {
    Serial.println(net::apiKey());
    Serial.println(net::tlsPin());
  } else if (cmd == "net") {
    Serial.println(net::statusLine());
  } else if (cmd == "cal") {
    hal::calibrate();
    ui::showMenu();
    Serial.println("OK");
  } else {
    Serial.println("ERR unknown command");
  }
}

void console::poll() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      handle(line);
      line = "";
    } else if (line.length() < MAX_SCREEN_BYTES + 64) {
      line += c;
    }
  }
}
