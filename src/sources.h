// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <utility>
#include <vector>

// Display-side data sources: a card lists where its values come from (see docs/design/info-hub.md).
// Phase 1 implements MQTT sources.
struct SourceDef {
  String card;                                   // owning card id
  String topic;                                  // MQTT topic filter ('+' and '#' allowed)
  String path;                                   // "a.b[0].c" into a JSON payload; empty or "$" = whole payload
  String key;                                    // scalar target key; "{n}" = n-th level of the actual topic (0-based)
  String table;                                  // table target key (rows), instead of key
  std::vector<std::pair<String, String>> row;    // column name -> expression ("{n}", "$" = the value)
  std::vector<std::pair<String, double>> map;    // string value -> number
  double scale = 1, offset = 0;
  uint32_t maxAge = 0;                           // seconds before the value counts as stale; 0 = never
};

namespace sources {
constexpr int MAX_PER_CARD = 16;
constexpr int MAX_TOPICS = 24;
constexpr int MAX_ROWS = 12;

bool parse(const String& card, JsonObjectConst o, SourceDef& out);   // false: not an mqtt source or invalid
bool topicMatches(const String& filter, const String& topic);
bool extract(const String& payload, const String& path, bool& isNum, double& num, String& str);

// A message arrived on a topic outside the device's own prefix: apply it to every card that listens.
void dispatch(const std::vector<SourceDef>& defs, const String& topic, const String& payload);
// Drop table rows that have gone stale and refresh the table (call every few seconds).
void tick(const std::vector<SourceDef>& defs);
}
