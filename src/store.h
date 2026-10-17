// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <FS.h>
#include <vector>

namespace store {
bool begin();
bool ready();

// Recursive lock around all SD access. Never take the UI lock while holding it.
struct Guard {
  Guard();
  ~Guard();
};

// Card ids are paths: 1-4 segments joined by '/', e.g. hosts/milo/health.
// On the SD card they are stored flat, with '/' written as '~'.
bool validId(const String& id);
String screenPath(const String& id);   // /screens/hosts~milo~health.json
String dataPath(const String& id);     // /data/hosts~milo~health.json
std::vector<String> listScreenIds();
bool validAssetName(const String& name);

// Parse JSON directly from the card (optionally filtered) without buffering the file in a String.
bool readJson(const String& path, JsonDocument& doc, JsonDocument* filter, size_t maxLen);
bool readFile(const String& path, String& out, size_t maxLen);
bool writeFileAtomic(const String& path, const uint8_t* data, size_t len);
bool removeFile(const String& path);
bool exists(const String& path);
std::vector<String> listNames(const char* dir, const char* ext);  // names without ext

// Streaming atomic write: data lands in <path>.tmp and replaces <path> on commit().
class Writer {
 public:
  ~Writer() { abort(); }
  bool open(const String& finalPath);
  bool write(const uint8_t* data, size_t len);
  bool commit();
  void abort();
  size_t size() const { return written_; }

 private:
  File f_;                       // per writer: two writers (an upload and the data autosave) must not share a handle
  String tmp_, fin_;
  bool open_ = false;
  size_t written_ = 0;
};

uint64_t totalBytes();
uint64_t usedBytes();
}
