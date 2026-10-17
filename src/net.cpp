// SPDX-License-Identifier: Apache-2.0
#include "net.h"
#include "config.h"
#include "dbg.h"
#include "hal.h"
#include "store.h"
#include "provision.h"
#include "tls.h"
#include "tlsmem.h"
#include "ui.h"
#include <ESPmDNS.h>
#include <Preferences.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WiFiUdp.h>
#include <esp_https_server.h>

// ---------- configuration (NVS) ----------
struct Cfg {
  String name, ssid, pass, apikey;
  String mqHost, mqUser, mqPass, mqCa, mqPrefix, haPrefix = "homeassistant";
  String ntp = "pool.ntp.org";
  int mqPort = 1883;
  bool mqTls = false;
  int brightness = 100;
  int maxCards = 48, minFreeMb = 32;
  bool ssOn = false;
  int ssIdle = 30, ssDwell = 8;
  String ssPrefix;
};
static Cfg cfg;
static void saveSlideshow();
static String certPem, keyPem, certFp, certPin;
static volatile uint32_t discoveryAfter = 0;
static volatile bool provisionRequested = false;
static volatile bool ntpChanged = false, wifiChanged = false, mqttChanged = false, discoveryDirty = true;

static String nvsGet(Preferences& p, const char* k, const String& def = "") { return p.isKey(k) ? p.getString(k) : def; }

static void loadCfg() {
  Preferences p;
  p.begin("cyd", false);
  uint8_t mac[6];
  WiFi.macAddress(mac);
  char def[16];
  snprintf(def, sizeof(def), "cyd-%02x%02x%02x", mac[3], mac[4], mac[5]);
  cfg.name = nvsGet(p, "name", def);
  cfg.ssid = nvsGet(p, "ssid");
  cfg.pass = nvsGet(p, "pass");
  cfg.apikey = nvsGet(p, "apikey");
  if (cfg.apikey.isEmpty()) {
    uint8_t r[16];
    esp_fill_random(r, sizeof(r));
    for (uint8_t b : r) { char h[3]; snprintf(h, 3, "%02x", b); cfg.apikey += h; }
    p.putString("apikey", cfg.apikey);
  }
  cfg.mqHost = nvsGet(p, "mq_host");
  cfg.mqPort = p.getInt("mq_port", 1883);
  cfg.mqUser = nvsGet(p, "mq_user");
  cfg.mqPass = nvsGet(p, "mq_pass");
  cfg.mqTls = p.getBool("mq_tls", false);
  cfg.mqCa = nvsGet(p, "mq_ca");
  cfg.mqPrefix = nvsGet(p, "mq_pfx", "cyd/" + cfg.name);
  cfg.haPrefix = nvsGet(p, "ha_pfx", "homeassistant");
  cfg.brightness = p.getInt("bright", 100);
  cfg.ntp = nvsGet(p, "ntp", "pool.ntp.org");
  cfg.maxCards = p.getInt("lim_cards", 48);
  cfg.minFreeMb = p.getInt("lim_free", 32);
  cfg.ssOn = p.getBool("ss_on", false);
  cfg.ssIdle = p.getInt("ss_idle", 30);
  cfg.ssDwell = p.getInt("ss_dwell", 8);
  cfg.ssPrefix = nvsGet(p, "ss_pfx");
  certPem = nvsGet(p, "tls_crt");
  keyPem = nvsGet(p, "tls_key");
  certPin = nvsGet(p, "tls_pin");
  p.end();
}

static void saveStr(const char* k, const String& v) {
  Preferences p;
  p.begin("cyd", false);
  p.putString(k, v);
  p.end();
}

static void ensureCert() {
  if (certPem.length() && keyPem.length() && certPin.length()) { certFp = tls::fingerprint(certPem); return; }
  Serial.println("net: generating TLS certificate...");
  if (tls::generate(cfg.name, certPem, keyPem, certPin)) {
    saveStr("tls_pin", certPin);
    saveStr("tls_crt", certPem);
    saveStr("tls_key", keyPem);
    certFp = tls::fingerprint(certPem);
  } else {
    Serial.println("net: certificate generation FAILED");
  }
}

void net::resetCredentials() {
  Preferences p;
  p.begin("cyd", false);
  p.remove("apikey"); p.remove("tls_crt"); p.remove("tls_key"); p.remove("tls_pin");
  p.end();
}

// ---------- public small helpers ----------
bool net::connected() { return WiFi.status() == WL_CONNECTED; }
String net::ip() { return connected() ? WiFi.localIP().toString() : String("-"); }
String net::name() { return cfg.name; }
int net::brightness() { return cfg.brightness; }
String net::apiKey() { return cfg.apikey; }
String net::tlsPin() { return certPin; }

void net::setBrightness(int pct) {
  cfg.brightness = constrain(pct, 0, 100);
  hal::backlight(cfg.brightness);
  Preferences p;
  p.begin("cyd", false);
  p.putInt("bright", cfg.brightness);
  p.end();
}

bool net::setWifi(const String& ssid, const String& pass) {
  if (ssid.isEmpty() || ssid.length() > 32 || pass.length() > 63) return false;
  cfg.ssid = ssid;
  cfg.pass = pass;
  saveStr("ssid", ssid);
  saveStr("pass", pass);
  wifiChanged = true;
  return true;
}

String net::statusLine() {
  if (!connected()) return cfg.ssid.isEmpty() ? "Wi-Fi: not set up" : "Wi-Fi: connecting...";
  return cfg.name + "  " + ip();
}

String net::infoText(bool reveal) {
  String s = "Name: " + cfg.name + "\n";
  s += "Version: " CYD_VERSION "\n";
  s += "Wi-Fi: " + (connected() ? cfg.ssid + "  " + ip() : String("not connected")) + "\n";
  s += "MQTT: " + (cfg.mqHost.isEmpty() ? String("not configured") : cfg.mqHost) + "\n";
  s += "TLS pin:\n" + certPin + "\n";
  s += "API key: " + (reveal ? cfg.apikey : String("(hidden - tap button)"));
  return s;
}

// ---------- HTTPS API ----------
static const char* statusText(int c) {
  switch (c) {
    case 200: return "200 OK";
    case 201: return "201 Created";
    case 400: return "400 Bad Request";
    case 401: return "401 Unauthorized";
    case 404: return "404 Not Found";
    case 408: return "408 Request Timeout";
    case 413: return "413 Payload Too Large";
    case 503: return "503 Service Unavailable";
    default: return "500 Internal Server Error";
  }
}

static httpd_handle_t httpd = nullptr;

// Keep-alive is allowed (a handshake costs ~2 s). The server never forces a close, so TIME_WAIT lands on
// the host and not on this device's 16 socket slots; a new client replaces an idle one (LRU purge).
static esp_err_t reply(httpd_req_t* r, int code, const String& body) {
  httpd_resp_set_status(r, statusText(code));
  httpd_resp_set_type(r, "application/json");
  return httpd_resp_send(r, body.c_str(), body.length());
}
static esp_err_t err(httpd_req_t* r, int code, const char* msg) {
  JsonDocument d;
  d["error"] = msg;
  String s;
  serializeJson(d, s);
  return reply(r, code, s);
}

static bool authOk(httpd_req_t* r) {
  char key[80] = {0};
  if (httpd_req_get_hdr_value_str(r, "X-Api-Key", key, sizeof(key)) != ESP_OK) return false;
  size_t n = cfg.apikey.length();
  if (strlen(key) != n) return false;
  uint8_t diff = 0;
  for (size_t i = 0; i < n; i++) diff |= key[i] ^ cfg.apikey[i];
  return diff == 0;
}

static bool readBody(httpd_req_t* r, String& out, size_t max) {
  if (r->content_len > max) return false;
  out = "";
  out.reserve(r->content_len);
  char buf[512];
  size_t left = r->content_len;
  int stalls = 0;
  while (left) {
    int g = httpd_req_recv(r, buf, min(left, sizeof(buf)));
    if (g == HTTPD_SOCK_ERR_TIMEOUT) { if (++stalls > 2) return false; continue; }
    if (g <= 0) return false;
    stalls = 0;
    out.concat(buf, g);
    left -= g;
  }
  return true;
}

static std::vector<String> split(const String& path) {
  std::vector<String> v;
  int i = 1;
  while (i <= (int)path.length()) {
    int j = path.indexOf('/', i);
    if (j < 0) j = path.length();
    v.push_back(path.substring(i, j));
    i = j + 1;
  }
  return v;
}

static String statusJson() {
  JsonDocument d;
  d["name"] = cfg.name;
  d["version"] = CYD_VERSION;
  d["uptime_s"] = millis() / 1000;
  d["heap_free"] = ESP.getFreeHeap();
  d["heap_largest"] = ESP.getMaxAllocHeap();
  d["tls_pool_free"] = tlsmem::freeBytes();
  d["rssi"] = WiFi.RSSI();
  d["sd_total_mb"] = (uint32_t)(store::totalBytes() >> 20);
  d["sd_used_mb"] = (uint32_t)(store::usedBytes() >> 20);
  d["screens"] = ui::screenCount();
  d["current"] = ui::current();
  d["brightness"] = cfg.brightness;
  d["slideshow"] = cfg.ssOn ? "ON" : "OFF";
  d["ldr"] = hal::ldrRaw();
  d["mqtt"] = cfg.mqHost.isEmpty() ? "off" : "configured";
  extern const char* g_resetReason;
  d["reset_reason"] = g_resetReason;
  d["sd_ok"] = store::ready();
  d["alerts"] = ui::alertCount();
  d["evicted"] = ui::evictedTotal();
  d["sd_free_mb"] = (uint32_t)(store::freeBytes() >> 20);
  d["cards_max"] = cfg.maxCards;
  d["slideshow"] = ui::slideshowRunning() ? "running" : (cfg.ssOn ? "armed" : "off");
  d["time_valid"] = ui::clockValid();
  if (ui::clockValid()) {
    time_t t = time(nullptr);
    char b[24];
    strftime(b, sizeof(b), "%Y-%m-%dT%H:%M:%SZ", gmtime(&t));
    d["time_utc"] = b;
  }
  String s;
  serializeJson(d, s);
  return s;
}

static esp_err_t putScreen(httpd_req_t* r, const String& id) {
  String body;
  if (!readBody(r, body, MAX_SCREEN_BYTES)) return err(r, 413, "body too large or unreadable");
  JsonDocument doc;
  if (deserializeJson(doc, body)) return err(r, 400, "invalid JSON");
  if (doc["schema"].as<int>() != 1) return err(r, 400, "schema must be 1");
  if (String(doc["id"] | "") != id) return err(r, 400, "id in body must match URL");
  if (!doc["widgets"].is<JsonArray>()) return err(r, 400, "widgets must be an array");
  if (!doc["dwell"].isNull() && (!doc["dwell"].is<int>() || (int)doc["dwell"] < 2 || (int)doc["dwell"] > 120))
    return err(r, 400, "dwell must be an integer from 2 to 120 seconds");
  if (!doc["priority"].isNull()) {
    String p = doc["priority"] | "";
    if (p != "low" && p != "normal" && p != "high" && p != "critical") return err(r, 400, "priority must be low, normal, high or critical");
  }
  if (!doc["expires"].isNull()) {
    int64_t exp;
    if (!doc["expires"].is<const char*>() || !ui::parseUtc(doc["expires"], exp))
      return err(r, 400, "expires must look like 2026-10-20T18:00:00Z (UTC)");
    if (ui::clockValid() && exp <= time(nullptr)) return err(r, 400, "expires is already in the past");
  }
  if (!store::writeFileAtomic(store::screenPath(id), (const uint8_t*)body.c_str(), body.length()))
    return err(r, 500, "SD write failed");
  ui::refreshIndex();
  ui::cardUpdated(id);
  if (doc["data"].is<JsonObject>()) ui::replaceData(id, doc["data"].as<JsonObjectConst>());
  net::screensChanged();
  return reply(r, 201, "{\"ok\":true}");
}

static esp_err_t putAsset(httpd_req_t* r, const String& name) {
  if (r->content_len == 0 || r->content_len > MAX_ASSET_BYTES) return err(r, 413, "asset size out of range");
  store::Writer w;
  if (!w.open("/assets/" + name)) return err(r, 500, "SD open failed");
  uint8_t buf[1024];
  size_t left = r->content_len;
  bool first = true;
  int stalls = 0;
  while (left) {
    int g = httpd_req_recv(r, (char*)buf, min(left, sizeof(buf)));
    if (g == HTTPD_SOCK_ERR_TIMEOUT) { if (++stalls > 2) return err(r, 408, "upload stalled"); continue; }
    if (g <= 0) return err(r, 400, "upload interrupted");
    stalls = 0;
    if (first && buf[0] != 0x19) return err(r, 400, "not an LVGL binary image (convert with cyd-canvas)");
    first = false;
    if (!w.write(buf, g)) return err(r, 500, "SD write failed");
    left -= g;
  }
  return w.commit() ? reply(r, 201, "{\"ok\":true}") : err(r, 500, "commit failed");
}

static esp_err_t putConfig(httpd_req_t* r) {
  String body;
  if (!readBody(r, body, 4096)) return err(r, 413, "body too large");
  JsonDocument d;
  if (deserializeJson(d, body)) return err(r, 400, "invalid JSON");
  Preferences p;
  p.begin("cyd", false);
  if (d["ntp"].is<const char*>() && strlen(d["ntp"]) > 0 && strlen(d["ntp"]) < 64) {
    cfg.ntp = d["ntp"].as<String>();
    p.putString("ntp", cfg.ntp);
    ntpChanged = true;
  }
  if (d["slideshow"].is<JsonObject>()) {
    JsonObjectConst w = d["slideshow"];
    if (w["enabled"].is<bool>()) cfg.ssOn = w["enabled"];
    if (w["idle"].is<int>()) cfg.ssIdle = constrain((int)w["idle"], 3, 3600);
    if (w["dwell"].is<int>()) cfg.ssDwell = constrain((int)w["dwell"], 2, 600);
    if (w["prefix"].is<const char*>() && strlen(w["prefix"]) <= 32) cfg.ssPrefix = w["prefix"].as<String>();
    saveSlideshow();
    ui::setSlideshow(cfg.ssOn, cfg.ssIdle, cfg.ssDwell, cfg.ssPrefix);
  }
  if (d["limits"].is<JsonObject>()) {
    JsonObjectConst l = d["limits"];
    if (l["max_cards"].is<int>()) cfg.maxCards = constrain((int)l["max_cards"], 4, 200);
    if (l["min_free_mb"].is<int>()) cfg.minFreeMb = constrain((int)l["min_free_mb"], 1, 4096);
    p.putInt("lim_cards", cfg.maxCards);
    p.putInt("lim_free", cfg.minFreeMb);
    ui::setLimits(cfg.maxCards, cfg.minFreeMb);
  }
  JsonObjectConst m = d["mqtt"];
  if (!m.isNull()) {
    if (m["host"].is<const char*>()) { cfg.mqHost = m["host"].as<String>(); p.putString("mq_host", cfg.mqHost); }
    if (m["port"].is<int>()) { cfg.mqPort = m["port"]; p.putInt("mq_port", cfg.mqPort); }
    if (m["user"].is<const char*>()) { cfg.mqUser = m["user"].as<String>(); p.putString("mq_user", cfg.mqUser); }
    if (m["password"].is<const char*>()) { cfg.mqPass = m["password"].as<String>(); p.putString("mq_pass", cfg.mqPass); }
    if (m["tls"].is<bool>()) { cfg.mqTls = m["tls"]; p.putBool("mq_tls", cfg.mqTls); }
    if (m["ca"].is<const char*>()) { cfg.mqCa = m["ca"].as<String>(); p.putString("mq_ca", cfg.mqCa); }
    if (m["prefix"].is<const char*>()) { cfg.mqPrefix = m["prefix"].as<String>(); p.putString("mq_pfx", cfg.mqPrefix); }
    if (m["discovery_prefix"].is<const char*>()) { cfg.haPrefix = m["discovery_prefix"].as<String>(); p.putString("ha_pfx", cfg.haPrefix); }
    mqttChanged = true;
  }
  p.end();
  return reply(r, 200, "{\"ok\":true}");
}

static String configJson() {
  JsonDocument d;
  d["name"] = cfg.name;
  d["ntp"] = cfg.ntp;
  d["limits"]["max_cards"] = cfg.maxCards;
  d["limits"]["min_free_mb"] = cfg.minFreeMb;
  d["slideshow"]["enabled"] = cfg.ssOn;
  d["slideshow"]["idle"] = cfg.ssIdle;
  d["slideshow"]["dwell"] = cfg.ssDwell;
  d["slideshow"]["prefix"] = cfg.ssPrefix;
  d["mqtt"]["host"] = cfg.mqHost;
  d["mqtt"]["port"] = cfg.mqPort;
  d["mqtt"]["user"] = cfg.mqUser;
  d["mqtt"]["password_set"] = !cfg.mqPass.isEmpty();
  d["mqtt"]["tls"] = cfg.mqTls;
  d["mqtt"]["ca_set"] = !cfg.mqCa.isEmpty();
  d["mqtt"]["prefix"] = cfg.mqPrefix;
  d["mqtt"]["discovery_prefix"] = cfg.haPrefix;
  String s;
  serializeJson(d, s);
  return s;
}

char g_route[72] = "";
volatile uint32_t g_routeSince = 0;
struct RouteTrack {
  RouteTrack(httpd_req_t* r) { snprintf(g_route, sizeof(g_route), "%d %s", (int)r->method, r->uri); g_routeSince = millis(); }
  ~RouteTrack() { g_route[0] = 0; }
};

static esp_err_t api(httpd_req_t* r) {
  RouteTrack track(r);
  if (!authOk(r)) return err(r, 401, "missing or wrong X-Api-Key");
  String path = r->uri;
  int q = path.indexOf('?');
  if (q >= 0) path = path.substring(0, q);
  std::vector<String> s = split(path);  // ["api", ...]
  int m = r->method;
  String a = s.size() > 1 ? s[1] : "";
  String id;  // everything after /api/<route>/ (card ids contain slashes)
  for (size_t i = 2; i < s.size(); i++) id += (i > 2 ? "/" : "") + s[i];

  if (a == "status" && m == HTTP_GET) return reply(r, 200, statusJson());

  if (a == "config") {
    if (m == HTTP_GET) return reply(r, 200, configJson());
    if (m == HTTP_PUT) return putConfig(r);
  }

  if (a == "index" && m == HTTP_GET) return reply(r, 200, ui::indexJson());

  if (a == "screens") {
    if (id.isEmpty() && m == HTTP_GET) {
      JsonDocument d;
      JsonArray arr = d.to<JsonArray>();
      for (auto& n : store::listScreenIds()) arr.add(n);
      String o;
      serializeJson(d, o);
      return reply(r, 200, o);
    }
    if (!store::validId(id)) return err(r, 400, "invalid screen id");
    if (m == HTTP_GET) {
      String t;
      return store::readFile(store::screenPath(id), t, MAX_SCREEN_BYTES) ? reply(r, 200, t) : err(r, 404, "no such screen");
    }
    if (m == HTTP_PUT) return putScreen(r, id);
    if (m == HTTP_DELETE) {
      if (!store::removeFile(store::screenPath(id))) return err(r, 404, "no such screen");
      store::removeFile(store::dataPath(id));
      ui::forgetScreen(id);
      net::screensChanged();
      return reply(r, 200, "{\"ok\":true}");
    }
  }

  if (a == "assets") {
    if (id.isEmpty() && m == HTTP_GET) {
      JsonDocument d;
      JsonArray arr = d.to<JsonArray>();
      for (auto& n : store::listNames("/assets", ".bin")) arr.add(n + ".bin");
      String o;
      serializeJson(d, o);
      return reply(r, 200, o);
    }
    if (!store::validAssetName(id)) return err(r, 400, "invalid asset name (letters, digits, - _ . and a .bin suffix)");
    if (m == HTTP_PUT) return putAsset(r, id);
    if (m == HTTP_DELETE) return store::removeFile("/assets/" + id) ? reply(r, 200, "{\"ok\":true}") : err(r, 404, "no such asset");
  }

  if (a == "data" && m == HTTP_POST) {
    if (!store::validId(id)) return err(r, 400, "invalid screen id");
    String body;
    if (!readBody(r, body, 8192)) return err(r, 413, "body too large");
    JsonDocument d;
    if (deserializeJson(d, body) || !d.is<JsonObject>()) return err(r, 400, "body must be a JSON object");
    return ui::applyData(id, d.as<JsonObjectConst>()) ? reply(r, 200, "{\"ok\":true}") : err(r, 404, "no such screen");
  }

  if (a == "show" && m == HTTP_POST) return ui::show(id) ? reply(r, 200, "{\"ok\":true}") : err(r, 404, "no such screen");
  if (a == "ack" && m == HTTP_POST) {
    if (!id.isEmpty() && !store::validId(id)) return err(r, 400, "invalid screen id");
    return ui::ack(id) ? reply(r, 200, "{\"ok\":true}") : err(r, 404, "no unacknowledged critical card");
  }
  if (a == "provision" && m == HTTP_POST) {
    provisionRequested = true;       // the network task reboots into Bluetooth setup after this reply is sent
    return reply(r, 200, "{\"ok\":true,\"note\":\"rebooting into Wi-Fi setup\"}");
  }
  if (a == "menu" && m == HTTP_POST) { ui::showMenu(); return reply(r, 200, "{\"ok\":true}"); }
  if (a == "brightness" && m == HTTP_POST) {
    String body;
    JsonDocument d;
    if (!readBody(r, body, 128) || deserializeJson(d, body) || !d["value"].is<int>()) return err(r, 400, "body must be {\"value\":0-100}");
    net::setBrightness(d["value"]);
    return reply(r, 200, "{\"ok\":true}");
  }
  return err(r, 404, "unknown route");
}

static void startHttps() {
  if (httpd || certPem.isEmpty()) return;
  httpd_ssl_config_t c = HTTPD_SSL_CONFIG_DEFAULT();
  c.httpd.stack_size = 8192;
  c.httpd.max_open_sockets = 1;   // each TLS session pins ~44 KB; two at once starve the Wi-Fi driver
  c.httpd.max_uri_handlers = 6;
  c.httpd.lru_purge_enable = true;
  c.httpd.recv_wait_timeout = 5;
  c.httpd.send_wait_timeout = 5;
  c.httpd.uri_match_fn = httpd_uri_match_wildcard;
  c.cacert_pem = (const uint8_t*)certPem.c_str();
  c.cacert_len = certPem.length() + 1;
  c.prvtkey_pem = (const uint8_t*)keyPem.c_str();
  c.prvtkey_len = keyPem.length() + 1;
  if (httpd_ssl_start(&httpd, &c) != ESP_OK) { Serial.println("net: https start failed"); httpd = nullptr; return; }
  for (httpd_method_t m : {HTTP_GET, HTTP_PUT, HTTP_POST, HTTP_DELETE}) {
    httpd_uri_t u = {};
    u.uri = "/api/*";
    u.method = m;
    u.handler = api;
    httpd_register_uri_handler(httpd, &u);
  }
  Serial.println("net: https listening on :443");
}

// ---------- MQTT + Home Assistant discovery ----------
static WiFiClient plainClient;
static WiFiClientSecure secureClient;
static PubSubClient mqtt;
static uint32_t lastMqttTry = 0, lastState = 0;
static String lastScreen = "?";

static String pfx() { return cfg.mqPrefix; }

static void publishLarge(const String& topic, const String& payload, bool retain) {
  if (!mqtt.beginPublish(topic.c_str(), payload.length(), retain)) return;
  for (size_t i = 0; i < payload.length(); i += 256) mqtt.write((const uint8_t*)payload.c_str() + i, min((size_t)256, payload.length() - i));
  mqtt.endPublish();
}

static void publishState() {
  JsonDocument d;
  d["screen"] = ui::current();
  d["brightness"] = cfg.brightness;
  d["ldr"] = hal::ldrRaw();
  d["rssi"] = WiFi.RSSI();
  d["heap"] = ESP.getFreeHeap();
  String s;
  serializeJson(d, s);
  publishLarge(pfx() + "/state", s, false);
  lastScreen = ui::current();
  lastState = millis();
}

static void publishDiscovery() {
  String uid = cfg.name;
  auto device = [&](JsonDocument& d) {
    d["device"]["identifiers"][0] = uid;
    d["device"]["name"] = cfg.name;
    d["device"]["model"] = "ESP32-2432S028R";
    d["device"]["manufacturer"] = "cyd-canvas";
    d["device"]["sw_version"] = CYD_VERSION;
    d["availability_topic"] = pfx() + "/status";
    d["payload_available"] = "online";
    d["payload_not_available"] = "offline";
  };
  auto put = [&](const char* comp, const char* obj, JsonDocument& d) {
    String s;
    serializeJson(d, s);
    publishLarge(cfg.haPrefix + "/" + comp + "/" + uid + "/" + obj + "/config", s, true);
  };

  JsonDocument sel;
  sel["name"] = "Screen";
  sel["unique_id"] = uid + "_screen";
  sel["command_topic"] = pfx() + "/cmd";
  sel["command_template"] = "{% if value == 'menu' %}menu{% else %}show {{ value }}{% endif %}";
  sel["state_topic"] = pfx() + "/state";
  sel["value_template"] = "{{ value_json.screen if value_json.screen else 'menu' }}";
  JsonArray opts = sel["options"].to<JsonArray>();
  opts.add("menu");
  int n = 0;
  for (auto& id : store::listScreenIds())
    if (n++ < 40) opts.add(id);
  device(sel);
  put("select", "screen", sel);

  JsonDocument br;
  br["name"] = "Brightness";
  br["unique_id"] = uid + "_brightness";
  br["command_topic"] = pfx() + "/cmd";
  br["command_template"] = "brightness {{ value | int }}";
  br["state_topic"] = pfx() + "/state";
  br["value_template"] = "{{ value_json.brightness }}";
  br["min"] = 0; br["max"] = 100; br["step"] = 5;
  br["unit_of_measurement"] = "%";
  device(br);
  put("number", "brightness", br);

  JsonDocument sw;
  sw["name"] = "Slideshow";
  sw["unique_id"] = uid + "_slideshow";
  sw["command_topic"] = pfx() + "/cmd";
  sw["payload_on"] = "slideshow on";
  sw["payload_off"] = "slideshow off";
  sw["state_topic"] = pfx() + "/state";
  sw["value_template"] = "{{ value_json.slideshow }}";
  sw["state_on"] = "ON";
  sw["state_off"] = "OFF";
  device(sw);
  put("switch", "slideshow", sw);

  JsonDocument ldr;
  ldr["name"] = "Light level";
  ldr["unique_id"] = uid + "_ldr";
  ldr["state_topic"] = pfx() + "/state";
  ldr["value_template"] = "{{ value_json.ldr }}";
  ldr["state_class"] = "measurement";
  device(ldr);
  put("sensor", "ldr", ldr);

  JsonDocument rssi;
  rssi["name"] = "Wi-Fi signal";
  rssi["unique_id"] = uid + "_rssi";
  rssi["state_topic"] = pfx() + "/state";
  rssi["value_template"] = "{{ value_json.rssi }}";
  rssi["device_class"] = "signal_strength";
  rssi["unit_of_measurement"] = "dBm";
  rssi["entity_category"] = "diagnostic";
  device(rssi);
  put("sensor", "rssi", rssi);

  JsonDocument back;
  back["name"] = "Back to menu";
  back["unique_id"] = uid + "_menu";
  back["command_topic"] = pfx() + "/cmd";
  back["payload_press"] = "menu";
  device(back);
  put("button", "menu", back);
  discoveryDirty = false;
}

static void onMqtt(char* topic, uint8_t* payload, unsigned int len) {
  String t = topic, body;
  body.concat((const char*)payload, len);
  String p = pfx();
  if (t == p + "/cmd") {
    body.trim();
    if (body == "menu") ui::showMenu();
    else if (body.startsWith("show ")) ui::show(body.substring(5));
    else if (body.startsWith("brightness ")) net::setBrightness(body.substring(11).toInt());
    else if (body == "slideshow on") net::setSlideshow(true);
    else if (body == "slideshow off") net::setSlideshow(false);
  } else if (t.startsWith(p + "/data/")) {
    String id = t.substring(p.length() + 6);
    JsonDocument d;
    if (store::validId(id) && !deserializeJson(d, body) && d.is<JsonObject>()) ui::applyData(id, d.as<JsonObjectConst>());
  }
}

static void mqttService() {
  if (cfg.mqHost.isEmpty() || !net::connected()) return;
  if (mqttChanged) { mqtt.disconnect(); mqttChanged = false; lastMqttTry = 0; discoveryDirty = true; }
  if (!mqtt.connected()) {
    if (millis() - lastMqttTry < 5000 && lastMqttTry) return;
    lastMqttTry = millis();
    if (cfg.mqTls) {
      if (cfg.mqCa.isEmpty()) return;  // never fall back to unverified TLS
      secureClient.setCACert(cfg.mqCa.c_str());
      mqtt.setClient(secureClient);
    } else {
      mqtt.setClient(plainClient);
    }
    mqtt.setServer(cfg.mqHost.c_str(), cfg.mqPort);
    mqtt.setBufferSize(3072);
    mqtt.setCallback(onMqtt);
    String st = pfx() + "/status";
    bool ok = cfg.mqUser.isEmpty()
                  ? mqtt.connect(cfg.name.c_str(), nullptr, nullptr, st.c_str(), 1, true, "offline")
                  : mqtt.connect(cfg.name.c_str(), cfg.mqUser.c_str(), cfg.mqPass.c_str(), st.c_str(), 1, true, "offline");
    if (!ok) return;
    mqtt.publish(st.c_str(), "online", true);
    mqtt.subscribe((pfx() + "/cmd").c_str());
    mqtt.subscribe((pfx() + "/data/#").c_str());
    discoveryDirty = true;
    Serial.println("net: mqtt connected");
  }
  mqtt.loop();
  if (discoveryDirty && (int32_t)(millis() - discoveryAfter) >= 0) publishDiscovery();
  if (ui::current() != lastScreen || millis() - lastState > 30000) publishState();
}

// A real network round trip, never answered from a cache: a DNS query for a name that cannot exist.
// Any reply (even NXDOMAIN) proves the TCP/IP stack and the radio are both moving packets.
bool net::probe(IPAddress server) {
  if (server == IPAddress((uint32_t)0)) server = WiFi.gatewayIP();
  WiFiUDP udp;
  static const uint8_t q[] = {0x4c, 0x59, 0x01, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                              5, 'p', 'r', 'o', 'b', 'e', 7, 'i', 'n', 'v', 'a', 'l', 'i', 'd', 0, 0x00, 0x01, 0x00, 0x01};
  if (!udp.begin(0)) return false;
  udp.beginPacket(server, 53);
  udp.write(q, sizeof(q));
  udp.endPacket();
  uint32_t t = millis();
  while (millis() - t < 2000) {
    if (udp.parsePacket() > 0) { udp.stop(); return true; }
    delay(20);
  }
  udp.stop();
  return false;
}

void net::screensChanged() { discoveryDirty = true; discoveryAfter = millis() + 4000; }

// ---------- task ----------
static void netTask(void*) {
  ensureCert();
  bool wifiStarted = false;
  uint32_t lastTry = 0;
  for (;;) {
    if (wifiChanged) { wifiChanged = false; WiFi.disconnect(); wifiStarted = false; }
    if (!cfg.ssid.isEmpty() && !wifiStarted) {
      WiFi.mode(WIFI_STA);
      WiFi.setHostname(cfg.name.c_str());
      WiFi.setSleep(false);
      WiFi.setTxPower(WIFI_POWER_11dBm);   // the AP is metres away; full power only adds USB current peaks
      WiFi.begin(cfg.ssid.c_str(), cfg.pass.c_str());
      wifiStarted = true;
      lastTry = millis();
    }
    if (wifiStarted && WiFi.status() != WL_CONNECTED && millis() - lastTry > 30000) {
      WiFi.disconnect();
      wifiStarted = false;  // retry
    }
    static bool wasUp = false;
    bool up = net::connected();
    if (up && !wasUp) {
      configTime(0, 0, cfg.ntp.c_str(), "pool.ntp.org");
      MDNS.begin(cfg.name.c_str());
      MDNS.addService("https", "tcp", 443);
      startHttps();
      Serial.printf("net: up %s\n", WiFi.localIP().toString().c_str());
    }
    wasUp = up;
    if (ntpChanged && up) { ntpChanged = false; configTime(0, 0, cfg.ntp.c_str(), "pool.ntp.org"); }
    {
      static int lowCount = 0;
      if (up && ESP.getMaxAllocHeap() < 36000) {
        if (++lowCount > 3000) { Serial.println("net: heap too fragmented for TLS, restarting"); delay(100); ESP.restart(); }
      } else {
        lowCount = 0;
      }
    }
    {
      // diagnostics: report whenever the largest free block drops under what a TLS handshake needs
      static uint32_t lastLog = 0;
      size_t big = ESP.getMaxAllocHeap();
      if (big < 36000 && millis() - lastLog > 500) {
        lastLog = millis();
        Serial.printf("heap: LOW largest=%u free=%u minfree=%u\n", (unsigned)big, (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMinFreeHeap());
      }
    }
    {
      static uint32_t lastStuck = 0;
      if (g_route[0] && millis() - g_routeSince > 8000 && millis() - lastStuck > 2000) {
        lastStuck = millis();
        Serial.printf("STUCK handler '%s' for %us  sd-holder=%s lv-holder=%s free=%u\n", g_route,
                      (unsigned)((millis() - g_routeSince) / 1000), (const char*)g_sdHolder, (const char*)g_lvHolder,
                      (unsigned)ESP.getFreeHeap());
      }
    }
    {
      // Self-check: Wi-Fi can stay associated while no packet moves (seen as send buffers full, EAGAIN).
      // Three failed round trips: reconnect. Six: restart; the display is unattended and state is on the SD card.
      static uint32_t lastProbe = 0;
      static int probeFails = 0;
      if (up && millis() - lastProbe > 15000) {
        lastProbe = millis();
        if (net::probe()) {
          probeFails = 0;
        } else {
          probeFails++;
          Serial.printf("net: probe failed (%d)\n", probeFails);
          if (probeFails == 3) {
            Serial.println("net: reconnecting Wi-Fi");
            WiFi.disconnect();
            wifiStarted = false;
          } else if (probeFails >= 6) {
            Serial.println("net: network still dead, restarting");
            delay(100);
            ESP.restart();
          }
        }
      }
    }
    {
      static uint32_t offlineSince = 0;
      if (up || cfg.ssid.isEmpty()) offlineSince = 0;
      else if (!offlineSince) offlineSince = millis() ? millis() : 1;
      bool unreachable = offlineSince && millis() - offlineSince > 300000;     // saved network gone for 5 minutes
      if (provisionRequested || unreachable) {
        Serial.println(unreachable ? "net: saved Wi-Fi unreachable for 5 min, entering setup" : "net: setup requested");
        vTaskDelay(pdMS_TO_TICKS(600));
        provision::requestAndRestart(unreachable);
      }
    }
    ui::setStatusLine(net::statusLine());
    mqttService();
    vTaskDelay(pdMS_TO_TICKS(up ? 20 : 500));
  }
}

static void saveSlideshow() {
  Preferences p;
  p.begin("cyd", false);
  p.putBool("ss_on", cfg.ssOn);
  p.putInt("ss_idle", cfg.ssIdle);
  p.putInt("ss_dwell", cfg.ssDwell);
  p.putString("ss_pfx", cfg.ssPrefix);
  p.end();
}

void net::setSlideshow(bool on) {
  cfg.ssOn = on;
  saveSlideshow();
  ui::setSlideshow(cfg.ssOn, cfg.ssIdle, cfg.ssDwell, cfg.ssPrefix);
}

void net::begin() {
  WiFi.mode(WIFI_STA);  // MAC is valid after this
  WiFi.onEvent([](WiFiEvent_t ev, WiFiEventInfo_t info) {
    if (ev == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) Serial.printf("wifi: disconnected reason=%d\n", info.wifi_sta_disconnected.reason);
    else if (ev == ARDUINO_EVENT_WIFI_STA_CONNECTED) Serial.println("wifi: associated");
    else if (ev == ARDUINO_EVENT_WIFI_STA_GOT_IP) Serial.println("wifi: got ip");
    else if (ev == ARDUINO_EVENT_WIFI_STA_LOST_IP) Serial.println("wifi: LOST ip");
  });
  loadCfg();
  ui::setSlideshow(cfg.ssOn, cfg.ssIdle, cfg.ssDwell, cfg.ssPrefix);
  ui::setLimits(cfg.maxCards, cfg.minFreeMb);
  hal::backlight(cfg.brightness);
  xTaskCreatePinnedToCore(netTask, "net", 12288, nullptr, 1, nullptr, 0);
}
