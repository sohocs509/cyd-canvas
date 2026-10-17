// SPDX-License-Identifier: Apache-2.0
#include "console.h"
#include "config.h"
#include "hal.h"
#include "net.h"
#include "store.h"
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
    Serial.printf("heap=%u sd=%llu/%llu MB screens=%u\n", ESP.getFreeHeap(),
                  store::usedBytes() >> 20, store::totalBytes() >> 20, (unsigned)ui::screenCount());
  } else if (cmd == "ls") {
    for (auto& n : store::listNames("/screens", ".json")) Serial.println(n);
  } else if (cmd == "put") {
    int s2 = rest.indexOf(' ');
    String id = s2 < 0 ? rest : rest.substring(0, s2);
    String body = s2 < 0 ? "" : rest.substring(s2 + 1);
    JsonDocument doc;
    if (!store::validId(id) || deserializeJson(doc, body) || String(doc["id"] | "") != id) {
      Serial.println("ERR bad id or json (id must match)");
      return;
    }
    bool ok = store::writeFileAtomic("/screens/" + id + ".json", (const uint8_t*)body.c_str(), body.length());
    ui::refreshIndex();
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
  } else if (cmd == "show") {
    Serial.println(ui::show(rest) ? "OK" : "ERR no such screen");
  } else if (cmd == "menu") {
    ui::showMenu();
    Serial.println("OK");
  } else if (cmd == "rm") {
    bool ok = store::validId(rest) && store::removeFile("/screens/" + rest + ".json");
    store::removeFile("/data/" + rest + ".json");
    ui::forgetScreen(rest);
    Serial.println(ok ? "OK" : "ERR");
  } else if (cmd == "wifi") {
    int s2 = rest.indexOf(' ');
    String ssid = s2 < 0 ? rest : rest.substring(0, s2);
    String pw = s2 < 0 ? "" : rest.substring(s2 + 1);
    Serial.println(net::setWifi(ssid, pw) ? "OK" : "ERR bad ssid/password");
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
