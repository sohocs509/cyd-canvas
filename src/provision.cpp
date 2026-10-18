// SPDX-License-Identifier: Apache-2.0
#include "provision.h"
#include "config.h"
#include "hal.h"
#include "net.h"
#ifndef CYD_NO_BLE
#include <NimBLEDevice.h>
#endif
#include <Preferences.h>
#include <WiFi.h>

bool provision::needed() {
  Preferences p;
  p.begin("cyd", true);
  bool no = !p.isKey("ssid") || p.getString("ssid").isEmpty();
  bool flag = p.getBool("prov", false);
  p.end();
  return no || flag;
}

void provision::requestAndRestart(bool networkLost) {
  Preferences p;
  p.begin("cyd", false);
  p.putBool("prov", true);
  p.putInt("provwhy", networkLost ? 2 : 1);   // only a lost network makes setup keep retrying the saved one
  p.end();
  delay(200);
  ESP.restart();
}

#ifndef CYD_NO_BLE
// Improv Wi-Fi BLE service and characteristics.
#define IMPROV_SERVICE   "00467768-6228-2272-4663-277478268000"
#define IMPROV_STATE     "00467768-6228-2272-4663-277478268001"
#define IMPROV_ERROR     "00467768-6228-2272-4663-277478268002"
#define IMPROV_RPC_CMD   "00467768-6228-2272-4663-277478268003"
#define IMPROV_RPC_RES   "00467768-6228-2272-4663-277478268004"
#define IMPROV_CAPS      "00467768-6228-2272-4663-277478268005"

enum : uint8_t { ST_AUTH_REQUIRED = 0x01, ST_AUTHORIZED = 0x02, ST_PROVISIONING = 0x03, ST_PROVISIONED = 0x04 };
enum : uint8_t { ERR_NONE = 0x00, ERR_INVALID_RPC = 0x01, ERR_UNKNOWN_CMD = 0x02, ERR_CONNECT = 0x03, ERR_NOT_AUTH = 0x04, ERR_UNKNOWN = 0xFF };
enum : uint8_t { CMD_WIFI = 0x01, CMD_IDENTIFY = 0x02, CMD_STATE = 0x03, CMD_INFO = 0x04, CMD_SCAN = 0x05 };

static NimBLECharacteristic *chState, *chError, *chResult;
static volatile uint8_t state = ST_AUTH_REQUIRED, lastError = ERR_NONE;
static volatile uint32_t authUntil = 0;
static String wantSsid, wantPass;
static volatile bool connectJob = false, scanJob = false, identifyJob = false;
static String devName;
static std::string rpcBuf;

// ---------- Improv plumbing ----------
static void advertise() {
  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  adv->stop();
  // Main packet (31 bytes max): flags + the Improv service UUID + Improv service data. The name will not
  // fit beside them, so it goes in the scan response.
  NimBLEAdvertisementData ad;
  ad.setFlags(0x06);                                   // general discoverable, no BR/EDR
  ad.addServiceUUID(NimBLEUUID(IMPROV_SERVICE));
  uint8_t sd[6] = {state, 0x01 /* identify supported */, 0, 0, 0, 0};
  ad.setServiceData(NimBLEUUID((uint16_t)0x4677), sd, sizeof(sd));
  NimBLEAdvertisementData sr;
  sr.setName(devName.c_str());
  adv->setAdvertisementData(ad);
  adv->setScanResponseData(sr);
  adv->enableScanResponse(true);
  adv->start();
}

static void setState(uint8_t s) {
  state = s;
  chState->setValue(&s, 1);
  chState->notify();
  advertise();
}

static void setError(uint8_t e) {
  lastError = e;
  chError->setValue(&e, 1);
  chError->notify();
}

static void sendResult(uint8_t cmd, const std::vector<String>& strings) {
  std::string pkt;
  std::string body;
  for (const String& s : strings) {
    body.push_back((char)s.length());
    body.append(s.c_str(), s.length());
  }
  pkt.push_back((char)cmd);
  pkt.push_back((char)body.size());
  pkt += body;
  uint8_t sum = 0;
  for (char c : pkt) sum += (uint8_t)c;
  pkt.push_back((char)sum);
  chResult->setValue((const uint8_t*)pkt.data(), pkt.size());     // readable copy for clients that poll
  // Notify in 20-byte pieces (the smallest legal payload) with a short pause: a burst of long notifications
  // overruns the controller's queue and arrives truncated. Clients reassemble by the length byte.
  for (size_t i = 0; i < pkt.size(); i += 20) {
    chResult->notify((const uint8_t*)pkt.data() + i, min((size_t)20, pkt.size() - i));
    delay(25);
  }
}

static void handlePacket(const std::string& p) {
  uint8_t sum = 0;
  for (size_t i = 0; i + 1 < p.size(); i++) sum += (uint8_t)p[i];
  if ((uint8_t)p.back() != sum) { setError(ERR_INVALID_RPC); return; }
  uint8_t cmd = p[0], len = p[1];
  switch (cmd) {
    case CMD_WIFI: {
      if (state != ST_AUTHORIZED) { setError(ERR_NOT_AUTH); return; }
      if (len < 2) { setError(ERR_INVALID_RPC); return; }
      uint8_t sl = p[2];
      if ((size_t)sl + 3 >= p.size()) { setError(ERR_INVALID_RPC); return; }
      uint8_t pl = p[3 + sl];
      if ((size_t)sl + pl + 4 > p.size() - 1 || sl == 0 || sl > 32 || pl > 63) { setError(ERR_INVALID_RPC); return; }
      wantSsid = String(p.substr(3, sl).c_str());
      wantPass = String(p.substr(4 + sl, pl).c_str());
      setError(ERR_NONE);
      setState(ST_PROVISIONING);
      connectJob = true;                       // done in the main loop, not in the BLE callback
      break;
    }
    case CMD_IDENTIFY: identifyJob = true; break;
    case CMD_STATE: {
      setError(lastError);
      chState->setValue((const uint8_t*)&state, 1);
      chState->notify();
      sendResult(CMD_STATE, {});
      break;
    }
    case CMD_INFO:
      sendResult(CMD_INFO, {"cyd-canvas", CYD_VERSION, "ESP32", devName});
      break;
    case CMD_SCAN: scanJob = true; break;
    default: setError(ERR_UNKNOWN_CMD);
  }
}

class RpcCb : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* c, NimBLEConnInfo&) override {
    auto v = c->getValue();
    rpcBuf.append((const char*)v.data(), v.size());       // clients split packets at the MTU
    if (rpcBuf.size() >= 3 && rpcBuf.size() >= (size_t)(uint8_t)rpcBuf[1] + 3) {
      std::string pkt = rpcBuf.substr(0, (uint8_t)rpcBuf[1] + 3);
      rpcBuf.clear();
      handlePacket(pkt);
    } else if (rpcBuf.size() > 255) {
      rpcBuf.clear();
      setError(ERR_INVALID_RPC);
    }
  }
};

class ServerCb : public NimBLEServerCallbacks {
  void onDisconnect(NimBLEServer*, NimBLEConnInfo&, int) override { rpcBuf.clear(); advertise(); }
};

// ---------- screen ----------
static TFT_eSPI* tft;
static const int BTN_X = 60, BTN_Y = 150, BTN_W = 200, BTN_H = 56;

static void drawStatic() {
  tft->fillScreen(TFT_BLACK);
  tft->setTextColor(TFT_CYAN, TFT_BLACK);
  tft->drawCentreString("Wi-Fi setup", SCREEN_W / 2, 8, 4);
  tft->setTextColor(TFT_WHITE, TFT_BLACK);
  tft->drawCentreString("Open improv-wifi.com in Chrome", SCREEN_W / 2, 48, 2);
  tft->drawCentreString("(or the Home Assistant app), pick", SCREEN_W / 2, 66, 2);
  tft->drawCentreString(("this device: " + devName).c_str(), SCREEN_W / 2, 84, 2);
  tft->drawCentreString("Then tap ALLOW here.", SCREEN_W / 2, 112, 2);
}

static void drawStatus(const String& line, uint16_t color, bool button) {
  tft->fillRect(0, 140, SCREEN_W, SCREEN_H - 140, TFT_BLACK);
  if (button) {
    tft->fillRoundRect(BTN_X, BTN_Y, BTN_W, BTN_H, 10, 0x0400);
    tft->setTextColor(TFT_WHITE, 0x0400);
    tft->drawCentreString("ALLOW", SCREEN_W / 2, BTN_Y + 16, 4);
  }
  tft->setTextColor(color, TFT_BLACK);
  tft->drawCentreString(line.c_str(), SCREEN_W / 2, button ? BTN_Y + BTN_H + 10 : 170, 2);
}

static bool tryConnect(const String& ssid, const String& pass, uint32_t ms) {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), pass.c_str());
  uint32_t t = millis();
  while (millis() - t < ms) {
    if (WiFi.status() == WL_CONNECTED) return true;
    delay(150);
  }
  return false;
}

static void saveAndRestart(const String& ssid, const String& pass) {
  Preferences p;
  p.begin("cyd", false);
  p.putString("ssid", ssid);
  p.putString("pass", pass);
  p.putBool("prov", false);
  p.end();
  delay(2500);                                  // let the client read the result
  ESP.restart();
}

void provision::run(const char* deviceName) {
  devName = deviceName;
  tft = &hal::display();
  hal::backlight(100);
  drawStatic();

  NimBLEDevice::init(devName.c_str());
  NimBLEServer* server = NimBLEDevice::createServer();
  server->setCallbacks(new ServerCb());
  NimBLEService* svc = server->createService(IMPROV_SERVICE);
  chState = svc->createCharacteristic(IMPROV_STATE, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
  chError = svc->createCharacteristic(IMPROV_ERROR, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
  NimBLECharacteristic* rpc = svc->createCharacteristic(IMPROV_RPC_CMD, NIMBLE_PROPERTY::WRITE);
  rpc->setCallbacks(new RpcCb());
  chResult = svc->createCharacteristic(IMPROV_RPC_RES, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
  NimBLECharacteristic* caps = svc->createCharacteristic(IMPROV_CAPS, NIMBLE_PROPERTY::READ);
  uint8_t cap = 0x01, zero = 0, st = ST_AUTH_REQUIRED;
  caps->setValue(&cap, 1);
  chError->setValue(&zero, 1);
  chState->setValue(&st, 1);
  chResult->setValue(&zero, 1);
  advertise();
  Serial.printf("provision: advertising as '%s' (Improv Wi-Fi over BLE)\n", devName.c_str());

  // credentials we already have: keep trying them, so a router reboot never strands the display in setup
  Preferences p;
  p.begin("cyd", true);
  String savedSsid = p.getString("ssid", ""), savedPass = p.getString("pass", "");
  bool retrySaved = p.getInt("provwhy", 1) == 2;
  p.end();
  if (!retrySaved) savedSsid = "";

  uint32_t lastShown = 0, lastRetry = millis();
  String shown = "";
  drawStatus("Waiting for a phone or laptop...", TFT_YELLOW, true);
  String line;
  for (;;) {
    int16_t x, y;
    bool touched = hal::touch(x, y);
    if (touched && state == ST_AUTH_REQUIRED && x >= BTN_X && x <= BTN_X + BTN_W && y >= BTN_Y && y <= BTN_Y + BTN_H) {
      authUntil = millis() + 60000;
      setError(ERR_NONE);
      setState(ST_AUTHORIZED);
    }
    while (Serial.available()) {                 // developer shortcut: type "allow" over USB
      char ch = Serial.read();
      if (ch == '\n' || ch == '\r') {
        line.trim();
        if (line == "allow") { authUntil = millis() + 60000; setError(ERR_NONE); setState(ST_AUTHORIZED); Serial.println("authorized"); }
        else if (line == "reboot") ESP.restart();
        else if (line == "scan") { scanJob = true; }
        else if (line == "exit") {              // leave setup without changing the saved network
          Preferences q;
          q.begin("cyd", false);
          q.putBool("prov", false);
          q.end();
          Serial.println("leaving setup");
          ESP.restart();
        }
        line = "";
      } else if (line.length() < 40) line += ch;
    }
    if (state == ST_AUTHORIZED && (int32_t)(millis() - authUntil) >= 0) setState(ST_AUTH_REQUIRED);

    if (identifyJob) {
      identifyJob = false;
      for (int i = 0; i < 4; i++) { hal::led(false, true, false); delay(150); hal::led(false, false, false); delay(150); }
    }
    if (scanJob) {
      scanJob = false;
      WiFi.mode(WIFI_STA);
      WiFi.disconnect();
      delay(100);
      int n = WiFi.scanNetworks(false, false, false, 300);
      Serial.printf("provision: scan found %d networks\n", n);
      for (int i = 0; i < n && i < 15; i++)
        Serial.printf("  net %d: %s (%d dBm)%s\n", i, WiFi.SSID(i).c_str(), (int)WiFi.RSSI(i), WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? " open" : "");
      for (int i = 0; i < n && i < 15; i++)
        sendResult(CMD_SCAN, {WiFi.SSID(i), String(WiFi.RSSI(i)), WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "NO" : "YES"});
      sendResult(CMD_SCAN, {});
      WiFi.scanDelete();
    }
    if (connectJob) {
      connectJob = false;
      drawStatus("Connecting to " + wantSsid + "...", TFT_YELLOW, false);
      if (tryConnect(wantSsid, wantPass, 20000)) {
        setState(ST_PROVISIONED);
        sendResult(CMD_WIFI, {});
        drawStatus("Connected! Restarting...", TFT_GREEN, false);
        saveAndRestart(wantSsid, wantPass);
      }
      setError(ERR_CONNECT);
      setState(ST_AUTHORIZED);
      authUntil = millis() + 30000;
      shown = "";
    }
    // retry the saved network every 45 s when setup was entered because it was unreachable
    if (!savedSsid.isEmpty() && state != ST_PROVISIONING && millis() - lastRetry > 45000) {
      lastRetry = millis();
      if (tryConnect(savedSsid, savedPass, 12000)) {
        Preferences q;
        q.begin("cyd", false);
        q.putBool("prov", false);
        q.end();
        drawStatus("Saved network is back. Restarting...", TFT_GREEN, false);
        delay(1500);
        ESP.restart();
      }
      WiFi.disconnect(true);
    }
    if (millis() - lastShown > 500) {
      lastShown = millis();
      String now = state == ST_AUTHORIZED ? "Allowed: " + String((authUntil - millis()) / 1000) + " s left"
                   : lastError == ERR_CONNECT ? "Could not connect. Check the password." : "Waiting for a phone or laptop...";
      if (now != shown) {
        shown = now;
        drawStatus(now, state == ST_AUTHORIZED ? TFT_GREEN : (lastError == ERR_CONNECT ? TFT_RED : TFT_YELLOW), state == ST_AUTH_REQUIRED);
      }
    }
    delay(20);
  }
}
#else
void provision::run(const char* deviceName) {
  Serial.printf("Wi-Fi setup over Bluetooth is not compiled into this build; use the serial wifi command (%s)\n", deviceName);
  for (;;) delay(1000);
}
#endif
