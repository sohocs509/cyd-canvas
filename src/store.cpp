// SPDX-License-Identifier: Apache-2.0
#include "store.h"
#include "dbg.h"
#include "config.h"
#include <SD.h>
#include <SPI.h>

static SPIClass sdSpi(VSPI);
static SemaphoreHandle_t lockH;
static bool mounted = false;

volatile const char* g_sdHolder = "-";
store::Guard::Guard() { xSemaphoreTakeRecursive(lockH, portMAX_DELAY); g_sdHolder = pcTaskGetName(nullptr); }
store::Guard::~Guard() { g_sdHolder = "-"; xSemaphoreGiveRecursive(lockH); }

bool store::ready() { return mounted; }

bool store::begin() {
  if (!lockH) lockH = xSemaphoreCreateRecursiveMutex();
  Guard g;
  sdSpi.begin(PIN_SD_SCK, PIN_SD_MISO, PIN_SD_MOSI, PIN_SD_CS);
  // 4 MHz: the CYD's SD wiring drops reads at 20 MHz (cards vanished from the index). Our files are tiny.
  mounted = SD.begin(PIN_SD_CS, sdSpi, 4000000);
  if (!mounted) return false;
  for (const char* d : {"/screens", "/assets", "/data"})
    if (!SD.exists(d)) SD.mkdir(d);
  return true;
}

bool store::validId(const String& id) {
  if (id.length() < 1 || id.length() > 64) return false;
  int segs = 1, segLen = 0;
  for (size_t i = 0; i < id.length(); i++) {
    char c = id[i];
    if (c == '/') {
      if (segLen == 0 || ++segs > 4) return false;
      segLen = 0;
      continue;
    }
    bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || (segLen > 0 && (c == '-' || c == '_'));
    if (!ok || ++segLen > 32) return false;
  }
  return segLen > 0;
}

static String flat(const String& id) { String s = id; s.replace('/', '~'); return s; }
String store::screenPath(const String& id) { return "/screens/" + flat(id) + ".json"; }
String store::dataPath(const String& id) { return "/data/" + flat(id) + ".json"; }

std::vector<String> store::listScreenIds() {
  std::vector<String> v = listNames("/screens", ".json");
  for (String& s : v) s.replace('~', '/');
  return v;
}

bool store::validAssetName(const String& n) {
  if (n.length() < 5 || n.length() > 32 || !n.endsWith(".bin")) return false;
  for (size_t i = 0; i < n.length(); i++) {
    char c = n[i];
    if (!(isalnum(c) || c == '-' || c == '_' || c == '.')) return false;
  }
  return n[0] != '.';
}

// A read that fails once is retried: a transient SD error must not make a card disappear.
static File openRead(const String& path) {
  for (int i = 0; i < 4; i++) {
    File f = SD.open(path, FILE_READ);
    if (f) return f;
    delay(5 * (i + 1));
  }
  return File();
}

bool store::exists(const String& path) {
  Guard g;
  return mounted && SD.exists(path);
}

bool store::readJson(const String& path, JsonDocument& doc, JsonDocument* filter, size_t maxLen) {
  Guard g;
  if (!mounted) return false;
  File f = openRead(path);
  if (!f || f.isDirectory()) return false;
  if (f.size() > maxLen) { f.close(); return false; }
  DeserializationError e = filter ? deserializeJson(doc, f, DeserializationOption::Filter(*filter)) : deserializeJson(doc, f);
  f.close();
  if (e) Serial.printf("store: %s: %s\n", path.c_str(), e.c_str());
  return !e;
}

bool store::readFile(const String& path, String& out, size_t maxLen) {
  Guard g;
  if (!mounted) return false;
  File f = openRead(path);
  if (!f || f.isDirectory()) return false;
  if (f.size() > maxLen) { f.close(); return false; }
  out = "";
  out.reserve(f.size());
  while (f.available()) out += (char)f.read();
  f.close();
  return true;
}

bool store::writeFileAtomic(const String& path, const uint8_t* data, size_t len) {
  Writer w;
  return w.open(path) && w.write(data, len) && w.commit();
}

bool store::removeFile(const String& path) {
  Guard g;
  return mounted && SD.remove(path);
}

std::vector<String> store::listNames(const char* dir, const char* ext) {
  std::vector<String> out;
  Guard g;
  if (!mounted) return out;
  File d = SD.open(dir);
  if (!d || !d.isDirectory()) return out;
  size_t el = strlen(ext);
  for (File f = d.openNextFile(); f; f = d.openNextFile()) {
    String n = f.name();
    if (!f.isDirectory() && n.endsWith(ext) && n.length() > el) out.push_back(n.substring(0, n.length() - el));
    f.close();
  }
  d.close();
  return out;
}

bool store::Writer::open(const String& finalPath) {
  Guard g;
  if (!mounted || open_) return false;
  fin_ = finalPath;
  tmp_ = finalPath + ".tmp";
  SD.remove(tmp_);
  f_ = SD.open(tmp_, FILE_WRITE);
  if (!f_) return false;
  open_ = true;
  written_ = 0;
  return true;
}

bool store::Writer::write(const uint8_t* data, size_t len) {
  Guard g;
  if (!open_) return false;
  if (f_.write(data, len) != len) return false;
  written_ += len;
  return true;
}

bool store::Writer::commit() {
  Guard g;
  if (!open_) return false;
  f_.close();
  open_ = false;
  SD.remove(fin_);
  if (!SD.rename(tmp_, fin_)) { SD.remove(tmp_); return false; }
  return true;
}

void store::Writer::abort() {
  Guard g;
  if (!open_) return;
  f_.close();
  SD.remove(tmp_);
  open_ = false;
}

uint64_t store::totalBytes() { Guard g; return mounted ? SD.totalBytes() : 0; }
uint64_t store::usedBytes() { Guard g; return mounted ? SD.usedBytes() : 0; }
