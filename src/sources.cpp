// SPDX-License-Identifier: Apache-2.0
#include "sources.h"
#include "ui.h"
#include <map>

namespace {
struct RowState {
  std::vector<String> cells;
  uint32_t at = 0;
};
// (card + '\n' + table) -> topic -> row
std::map<String, std::map<String, RowState>> tables;

String level(const String& topic, int n) {
  int start = 0;
  for (int i = 0; i < n; i++) {
    start = topic.indexOf('/', start);
    if (start < 0) return "";
    start++;
  }
  int end = topic.indexOf('/', start);
  return end < 0 ? topic.substring(start) : topic.substring(start, end);
}

// Replace {n} with topic level n and "$" with the extracted value.
String expand(const String& expr, const String& topic, const String& value) {
  String out;
  for (size_t i = 0; i < expr.length(); i++) {
    char c = expr[i];
    if (c == '{') {
      int j = expr.indexOf('}', i);
      if (j > 0) { out += level(topic, expr.substring(i + 1, j).toInt()); i = j; continue; }
    } else if (c == '$') {
      out += value;
      continue;
    }
    out += c;
  }
  return out;
}

String numText(double v) {
  char b[24];
  if (v == (long)v) snprintf(b, sizeof(b), "%ld", (long)v);
  else snprintf(b, sizeof(b), "%.2f", v);
  return b;
}

void publishTable(const SourceDef& d) {
  auto it = tables.find(d.card + "\n" + d.table);
  JsonDocument doc;
  JsonArray arr = doc[d.table].to<JsonArray>();
  if (it != tables.end()) {
    for (auto& kv : it->second) {
      if ((int)arr.size() >= sources::MAX_ROWS) break;
      JsonObject o = arr.add<JsonObject>();
      for (size_t c = 0; c < d.row.size() && c < kv.second.cells.size(); c++) o[d.row[c].first] = kv.second.cells[c];
    }
  }
  ui::applyData(d.card, doc.as<JsonObjectConst>());
}
}  // namespace

bool sources::parse(const String& card, JsonObjectConst o, SourceDef& out) {
  if (String(o["type"] | "") != "mqtt") return false;
  out = SourceDef();
  out.card = card;
  out.topic = String(o["topic"] | "");
  if (out.topic.isEmpty() || out.topic.length() > 96) return false;
  out.path = String(o["path"] | "");
  out.key = String(o["key"] | "");
  out.table = String(o["table"] | "");
  if (out.key.isEmpty() == out.table.isEmpty()) return false;       // exactly one of key / table
  if (!out.table.isEmpty()) {
    JsonObjectConst r = o["row"];
    if (r.isNull() || r.size() == 0 || r.size() > 4) return false;
    for (JsonPairConst kv : r) out.row.push_back({String(kv.key().c_str()), String(kv.value().as<const char*>() ? kv.value().as<const char*>() : "$")});
  }
  for (JsonPairConst kv : o["map"].as<JsonObjectConst>()) out.map.push_back({String(kv.key().c_str()), kv.value().as<double>()});
  out.scale = o["scale"] | 1.0;
  out.offset = o["offset"] | 0.0;
  out.maxAge = constrain((int)(o["max_age"] | 0), 0, 86400);
  return true;
}

bool sources::topicMatches(const String& f, const String& t) {
  size_t fi = 0, ti = 0;
  while (true) {
    int fe = f.indexOf('/', fi), te = t.indexOf('/', ti);
    String fs = fe < 0 ? f.substring(fi) : f.substring(fi, fe);
    if (fs == "#") return true;
    if (ti > t.length()) return false;
    String ts = te < 0 ? t.substring(ti) : t.substring(ti, te);
    if (fs != "+" && fs != ts) return false;
    if (fe < 0 || te < 0) return fe < 0 && te < 0;
    fi = fe + 1;
    ti = te + 1;
  }
}

bool sources::extract(const String& payload, const String& path, bool& isNum, double& num, String& str) {
  String p = payload;
  p.trim();
  if (p.isEmpty()) return false;
  auto scalarFromText = [&](const String& s) {
    char* end;
    double v = strtod(s.c_str(), &end);
    if (end != s.c_str() && *end == 0) { isNum = true; num = v; return; }
    isNum = false;
    str = s;
  };
  if (path.isEmpty() || path == "$") {
    if (p[0] == '{' || p[0] == '[') return false;                  // a whole object is not a scalar
    scalarFromText(p);
    return true;
  }
  JsonDocument doc;
  if (deserializeJson(doc, p)) return false;
  JsonVariantConst v = doc.as<JsonVariantConst>();
  size_t i = 0;
  while (i < path.length() && !v.isNull()) {
    if (path[i] == '.') { i++; continue; }
    if (path[i] == '[') {
      int j = path.indexOf(']', i);
      if (j < 0) return false;
      v = v[(size_t)path.substring(i + 1, j).toInt()];
      i = j + 1;
      continue;
    }
    size_t j = i;
    while (j < path.length() && path[j] != '.' && path[j] != '[') j++;
    v = v[path.substring(i, j)];
    i = j;
  }
  if (v.isNull()) return false;
  if (v.is<bool>()) { isNum = true; num = v.as<bool>() ? 1 : 0; return true; }
  if (v.is<double>()) { isNum = true; num = v.as<double>(); return true; }
  if (v.is<const char*>()) { scalarFromText(String(v.as<const char*>())); return true; }
  return false;
}

void sources::dispatch(const std::vector<SourceDef>& defs, const String& topic, const String& payload) {
  for (const SourceDef& d : defs) {
    if (!topicMatches(d.topic, topic)) continue;
    bool isNum = false;
    double num = 0;
    String str;
    if (!extract(payload, d.path, isNum, num, str)) continue;
    bool mapped = false;
    if (!isNum) {                                                  // a string may map to a number (ok -> 1)
      for (auto& m : d.map) if (m.first == str) { num = m.second; isNum = true; mapped = true; break; }
    }
    if (isNum && !mapped) num = num * d.scale + d.offset;          // mapped values are already final
    String shown = isNum ? numText(num) : str;
    if (!d.table.isEmpty()) {
      RowState r;
      for (auto& col : d.row) r.cells.push_back(expand(col.second, topic, shown));
      r.at = millis();
      tables[d.card + "\n" + d.table][topic] = r;
      publishTable(d);
    } else {
      JsonDocument doc;
      String key = expand(d.key, topic, shown);
      if (isNum) doc[key] = num; else doc[key] = str;
      ui::applyData(d.card, doc.as<JsonObjectConst>());
    }
  }
}

void sources::tick(const std::vector<SourceDef>& defs) {
  for (const SourceDef& d : defs) {
    if (d.table.isEmpty() || d.maxAge == 0) continue;
    auto it = tables.find(d.card + "\n" + d.table);
    if (it == tables.end()) continue;
    bool changed = false;
    for (auto r = it->second.begin(); r != it->second.end();) {
      if ((uint32_t)(millis() - r->second.at) > d.maxAge * 1000UL) { r = it->second.erase(r); changed = true; }
      else ++r;
    }
    if (changed) publishTable(d);
  }
}
