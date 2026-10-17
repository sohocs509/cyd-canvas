// SPDX-License-Identifier: Apache-2.0
#include "ui.h"
#include "config.h"
#include "hal.h"
#include "dbg.h"
#include "net.h"
#include "store.h"
#include <lvgl.h>
#include <SD.h>
#include <algorithm>
#include <deque>
#include <map>
#include <set>

// ---------- state ----------
static SemaphoreHandle_t lvLock;
volatile const char* g_lvHolder = "-";
struct LvGuard {
  LvGuard() { xSemaphoreTakeRecursive(lvLock, portMAX_DELAY); g_lvHolder = pcTaskGetName(nullptr); }
  ~LvGuard() { g_lvHolder = "-"; xSemaphoreGiveRecursive(lvLock); }
};

struct Meta { std::vector<String> assets; int64_t mtime = 0; uint8_t dwell = 0; uint8_t prio = 1; String id, title, menu, expiresStr; std::vector<String> chartKeys; int64_t expires = 0; };
static std::vector<Meta> metas;

struct ScreenData {
  std::map<String, double> nums;
  std::map<String, String> blobs;                // arrays/objects/strings as JSON text
  std::map<String, std::deque<float>> hist;      // chart keys only
  bool dirty = false;
  bool loaded = false;
};
static std::map<String, ScreenData> data;

enum Kind { K_VALUE, K_BAR, K_ARC, K_CHART, K_TABLE, K_LED };
struct Bind {
  Kind kind;
  lv_obj_t* obj = nullptr;
  lv_obj_t* aux = nullptr;
  String key, fmt, unit;
  double mn = 0, mx = 100;
  lv_color_t base;
  std::vector<lv_chart_series_t*> ser;
  std::vector<String> serKeys;
  int points = 60;
  std::vector<String> cols;
  int rows = 6;
};
static std::vector<Bind> binds;
static std::map<String, std::vector<std::pair<double, lv_color_t>>> thresholds;

enum MergeMode { MERGE, REPLACE, SEED };
static bool mergeData(const String& id, JsonObjectConst d, MergeMode mode);

struct SlideCfg { bool on = false; int idle = 30, dwell = 8; String prefix; };
static SlideCfg ss;
static uint32_t lastInput = 0, ssNextAt = 0;
static bool ssActive = false;
static size_t ssCursor = 0;
static void slideshowTick();
static void maintain();
struct Limits { int maxCards = 48, minFreeMb = 32; };
static Limits lim;
static std::map<String, int64_t> usedAt;      // last use as epoch seconds (0 while the clock is unset)
static std::map<String, uint32_t> useSeq;     // tie-break and no-clock ordering
static uint32_t useCounter = 0, evicted = 0;
static String lastUploaded;
static bool maintainFlag = false;
static uint32_t maintainAt = 0;
static void markUsed(const String& id) {
  useSeq[id] = ++useCounter;
  usedAt[id] = ui::clockValid() ? (int64_t)time(nullptr) : 0;
}
static void sweepExpired();
static void checkAlerts();
static String curId;          // "" = menu
static lv_obj_t* curScr = nullptr;
static lv_obj_t* statusLbl = nullptr;
static String statusText;
static String pendingNav;     // "" none, "\x01" menu, otherwise screen id
static bool navPending = false;
// NOTE: always clear indexDirty BEFORE rebuilding: an upload that sets it during a rebuild must stay flagged.
static volatile bool indexDirty = true;
static uint32_t lastPersist = 0;
static lv_display_t* disp;
static uint16_t dispBuf[SCREEN_W * 8];   // small render strip: RAM is scarcer than flush calls

// ---------- LVGL glue ----------
static uint32_t tickCb() { return millis(); }

uint32_t ui_flushes = 0, ui_nonzero = 0, ui_lastw = 0, ui_lasth = 0;
static void flushCb(lv_display_t* d, const lv_area_t* a, uint8_t* px) {
  uint32_t w = a->x2 - a->x1 + 1, h = a->y2 - a->y1 + 1;
  ui_flushes++;
  ui_lastw = w; ui_lasth = h; ui_nonzero = 0;
  for (uint32_t i = 0; i < w * h; i++) if (((uint16_t*)px)[i]) ui_nonzero++;
  TFT_eSPI& t = hal::display();
  t.startWrite();
  t.setAddrWindow(a->x1, a->y1, w, h);
  t.pushColors((uint16_t*)px, w * h, true);
  t.endWrite();
  lv_display_flush_ready(d);
}

static void touchCb(lv_indev_t*, lv_indev_data_t* d) {
  int16_t x, y;
  if (hal::touch(x, y)) {
    lastInput = millis();
    if (ssActive) { ssActive = false; pendingNav = "\x01"; navPending = true; }   // a tap ends the slideshow
    d->point.x = x;
    d->point.y = y;
    d->state = LV_INDEV_STATE_PRESSED;
  } else {
    d->state = LV_INDEV_STATE_RELEASED;
  }
}

// LVGL file system on the SD card, drive letter S:
static void* fsOpen(lv_fs_drv_t*, const char* path, lv_fs_mode_t mode) {
  if (mode != LV_FS_MODE_RD) return nullptr;
  store::Guard g;
  File* f = new File(SD.open(path, FILE_READ));
  if (!*f) { delete f; return nullptr; }
  return f;
}
static lv_fs_res_t fsClose(lv_fs_drv_t*, void* p) {
  store::Guard g;
  File* f = (File*)p;
  f->close();
  delete f;
  return LV_FS_RES_OK;
}
static lv_fs_res_t fsRead(lv_fs_drv_t*, void* p, void* buf, uint32_t n, uint32_t* br) {
  store::Guard g;
  *br = ((File*)p)->read((uint8_t*)buf, n);
  return LV_FS_RES_OK;
}
static lv_fs_res_t fsSeek(lv_fs_drv_t*, void* p, uint32_t pos, lv_fs_whence_t w) {
  store::Guard g;
  File* f = (File*)p;
  uint32_t base = w == LV_FS_SEEK_SET ? 0 : w == LV_FS_SEEK_CUR ? f->position() : f->size();
  return f->seek(base + pos) ? LV_FS_RES_OK : LV_FS_RES_UNKNOWN;
}
static lv_fs_res_t fsTell(lv_fs_drv_t*, void* p, uint32_t* pos) {
  store::Guard g;
  *pos = ((File*)p)->position();
  return LV_FS_RES_OK;
}

// ---------- helpers ----------
// A user-supplied printf format is only safe if it has exactly one floating-point conversion
// ([flags][width][.prec] f/e/g) and nothing else except "%%". Anything else (%n, %s, %d...) is
// replaced by the default.
static String safeFmt(const char* in) {
  const char* def = "%.1f";
  if (!in || !*in || strlen(in) > 15) return def;
  int conversions = 0;
  for (const char* p = in; *p; p++) {
    if (*p != '%') continue;
    p++;
    if (*p == '%') continue;
    while (*p && strchr("-+ 0#", *p)) p++;
    int digits = 0;
    while (*p >= '0' && *p <= '9') { p++; digits++; }
    if (digits > 2) return def;
    if (*p == '.') { p++; digits = 0; while (*p >= '0' && *p <= '9') { p++; digits++; } if (digits > 2) return def; }
    if (!*p || !strchr("feEgG", *p)) return def;
    conversions++;
  }
  return conversions == 1 ? String(in) : String(def);
}

static lv_color_t parseColor(const char* s, lv_color_t def) {
  if (!s || s[0] != '#' || strlen(s) != 7) return def;
  return lv_color_hex(strtoul(s + 1, nullptr, 16));
}

static const lv_font_t* pickFont(int px) {
  switch (px) {
    case 12: return &lv_font_montserrat_12;
    case 24: return &lv_font_montserrat_24;
    case 32: return &lv_font_montserrat_32;
    default: return &lv_font_montserrat_16;
  }
}

static lv_color_t thresholdColor(const String& key, double v, lv_color_t def) {
  auto it = thresholds.find(key);
  if (it == thresholds.end()) return def;
  lv_color_t c = def;
  for (auto& p : it->second)
    if (v >= p.first) c = p.second;
  return c;
}

static ScreenData& loadData(const String& id) {
  ScreenData& sd = data[id];
  if (!sd.loaded) {
    sd.loaded = true;
    String txt;
    if (store::readFile(store::dataPath(id), txt, 8192)) {
      JsonDocument doc;
      if (!deserializeJson(doc, txt)) {
        for (JsonPair kv : doc.as<JsonObject>()) {
          if (kv.value().is<double>()) sd.nums[kv.key().c_str()] = kv.value().as<double>();
          else { String s; serializeJson(kv.value(), s); sd.blobs[kv.key().c_str()] = s; }
        }
      }
    }
  }
  return sd;
}

static void persist(const String& id) {
  ScreenData& sd = data[id];
  JsonDocument doc;
  for (auto& kv : sd.nums) doc[kv.first] = kv.second;
  for (auto& kv : sd.blobs) {
    JsonDocument v;
    if (!deserializeJson(v, kv.second)) doc[kv.first] = v;
  }
  String out;
  serializeJson(doc, out);
  store::writeFileAtomic(store::dataPath(id), (const uint8_t*)out.c_str(), out.length());
  sd.dirty = false;
}

// ---------- widget updates ----------
static void updateBind(Bind& b, ScreenData& sd) {
  auto n = sd.nums.find(b.key);
  bool have = n != sd.nums.end();
  double v = have ? n->second : 0;
  lv_color_t col = thresholdColor(b.key, v, b.base);
  switch (b.kind) {
    case K_VALUE: {
      if (!have) { lv_label_set_text(b.obj, "--"); break; }
      char buf[48], num[32];
      snprintf(num, sizeof(num), b.fmt.c_str(), v);
      snprintf(buf, sizeof(buf), "%s%s", num, b.unit.c_str());
      lv_label_set_text(b.obj, buf);
      lv_obj_set_style_text_color(b.obj, col, 0);
      break;
    }
    case K_BAR:
      if (have) lv_bar_set_value(b.obj, (int32_t)(v * 100), LV_ANIM_OFF);
      lv_obj_set_style_bg_color(b.obj, col, LV_PART_INDICATOR);
      break;
    case K_ARC:
      if (have) {
        lv_arc_set_value(b.obj, (int32_t)(v * 100));
        char buf[16];
        snprintf(buf, sizeof(buf), "%.0f", v);
        lv_label_set_text(b.aux, buf);
      }
      lv_obj_set_style_arc_color(b.obj, col, LV_PART_INDICATOR);
      break;
    case K_LED:
      if (v > 0) lv_led_on(b.obj); else lv_led_off(b.obj);
      break;
    case K_CHART:
      for (size_t i = 0; i < b.ser.size(); i++) {
        lv_chart_set_all_values(b.obj, b.ser[i], LV_CHART_POINT_NONE);
        auto h = sd.hist.find(b.serKeys[i]);
        if (h == sd.hist.end()) continue;
        size_t start = h->second.size() > (size_t)b.points ? h->second.size() - b.points : 0;
        for (size_t k = start; k < h->second.size(); k++)
          lv_chart_set_next_value(b.obj, b.ser[i], (int32_t)(h->second[k] * 100));
      }
      lv_chart_refresh(b.obj);
      break;
    case K_TABLE: {
      auto bl = sd.blobs.find(b.key);
      if (bl == sd.blobs.end()) break;
      JsonDocument doc;
      if (deserializeJson(doc, bl->second)) break;
      JsonArray rowsArr = doc.as<JsonArray>();
      uint32_t r = 0;
      for (JsonVariant row : rowsArr) {
        if ((int)r >= b.rows) break;
        for (size_t c = 0; c < b.cols.size(); c++) {
          String cell = row.is<JsonObject>() ? String(row[b.cols[c]].as<const char*>() ? row[b.cols[c]].as<const char*>() : "")
                                              : String(row[c].as<const char*>() ? row[c].as<const char*>() : "");
          if (cell.isEmpty() && row.is<JsonObject>() && !row[b.cols[c]].isNull()) { cell = row[b.cols[c]].as<String>(); }
          lv_table_set_cell_value(b.obj, r, c, cell.c_str());
        }
        r++;
      }
      for (; (int)r < b.rows; r++)
        for (size_t c = 0; c < b.cols.size(); c++) lv_table_set_cell_value(b.obj, r, c, "");
      break;
    }
  }
}

static void updateAll() {
  if (binds.empty()) return;
  ScreenData& sd = data[curId];
  for (Bind& b : binds) updateBind(b, sd);
}

// ---------- screen building ----------
static void plain(lv_obj_t* o) {
  lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
}

static void geom(lv_obj_t* o, JsonObjectConst w, int defW, int defH) {
  lv_obj_set_pos(o, w["x"] | 0, w["y"] | 0);
  int ww = w["w"] | defW, hh = w["h"] | defH;
  if (ww > 0 && hh > 0) lv_obj_set_size(o, ww, hh);
}

static lv_obj_t* backBtn = nullptr;
static lv_obj_t* ackBtn = nullptr;
static std::set<String> acked;
static std::map<String, uint32_t> critSeq;
static uint32_t seqCounter = 0;
static bool alertActive = false;

static bool isCritical(const String& id) {
  for (const Meta& m : metas) if (m.id == id) return m.prio == 3;
  return false;
}
static void goBack() {
  if (!curId.isEmpty() && isCritical(curId)) acked.insert(curId);   // leaving a critical card acknowledges it
  pendingNav = "\x01";
  navPending = true;
}
static void onLongPress(lv_event_t*) { goBack(); }

static lv_obj_t* buildScreen(const String& id, JsonDocument& doc) {
  lv_obj_t* scr = lv_obj_create(nullptr);
  lv_obj_set_style_bg_color(scr, parseColor(doc["bg"] | "#000000", lv_color_black()), 0);
  lv_obj_set_style_pad_all(scr, 0, 0);
  lv_obj_set_style_border_width(scr, 0, 0);
  lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(scr, onLongPress, LV_EVENT_LONG_PRESSED, nullptr);

  thresholds.clear();
  for (JsonPairConst kv : doc["thresholds"].as<JsonObjectConst>()) {
    auto& vec = thresholds[kv.key().c_str()];
    for (JsonArrayConst p : kv.value().as<JsonArrayConst>())
      vec.push_back({p[0].as<double>(), parseColor(p[1] | "#ffffff", lv_color_white())});
  }

  for (JsonObjectConst w : doc["widgets"].as<JsonArrayConst>()) {
    String type = w["type"] | "";
    lv_color_t col = parseColor(w["color"] | "#ffffff", lv_color_white());
    Bind b;
    b.key = w["key"] | "";
    b.base = col;
    b.mn = w["min"] | 0.0;
    b.mx = w["max"] | 100.0;

    if (type == "label") {
      lv_obj_t* l = lv_label_create(scr);
      lv_label_set_text(l, w["text"] | "");
      lv_obj_set_style_text_font(l, pickFont(w["font"] | 16), 0);
      lv_obj_set_style_text_color(l, col, 0);
      lv_obj_set_pos(l, w["x"] | 0, w["y"] | 0);
      plain(l);
    } else if (type == "value") {
      b.kind = K_VALUE;
      b.fmt = safeFmt(w["fmt"] | "%.1f");
      b.unit = w["unit"] | "";
      b.obj = lv_label_create(scr);
      lv_label_set_text(b.obj, "--");
      lv_obj_set_style_text_font(b.obj, pickFont(w["font"] | 24), 0);
      lv_obj_set_style_text_color(b.obj, col, 0);
      lv_obj_set_pos(b.obj, w["x"] | 0, w["y"] | 0);
      plain(b.obj);
      binds.push_back(b);
    } else if (type == "bar") {
      b.kind = K_BAR;
      b.obj = lv_bar_create(scr);
      geom(b.obj, w, 120, 14);
      lv_bar_set_range(b.obj, (int32_t)(b.mn * 100), (int32_t)(b.mx * 100));
      lv_obj_set_style_bg_color(b.obj, col, LV_PART_INDICATOR);
      plain(b.obj);
      binds.push_back(b);
    } else if (type == "arc") {
      b.kind = K_ARC;
      b.obj = lv_arc_create(scr);
      geom(b.obj, w, 80, 80);
      lv_arc_set_range(b.obj, (int32_t)(b.mn * 100), (int32_t)(b.mx * 100));
      lv_obj_remove_style(b.obj, nullptr, LV_PART_KNOB);
      lv_obj_set_style_arc_color(b.obj, col, LV_PART_INDICATOR);
      plain(b.obj);
      b.aux = lv_label_create(b.obj);
      lv_label_set_text(b.aux, "--");
      lv_obj_center(b.aux);
      binds.push_back(b);
    } else if (type == "led") {
      b.kind = K_LED;
      b.obj = lv_led_create(scr);
      geom(b.obj, w, 16, 16);
      lv_led_set_color(b.obj, col);
      lv_led_off(b.obj);
      plain(b.obj);
      binds.push_back(b);
    } else if (type == "chart") {
      b.kind = K_CHART;
      b.points = w["points"] | 60;
      b.obj = lv_chart_create(scr);
      geom(b.obj, w, 200, 100);
      lv_chart_set_type(b.obj, String(w["style"] | "line") == "bar" ? LV_CHART_TYPE_BAR : LV_CHART_TYPE_LINE);
      lv_chart_set_point_count(b.obj, b.points);
      lv_chart_set_axis_range(b.obj, LV_CHART_AXIS_PRIMARY_Y, (int32_t)(b.mn * 100), (int32_t)(b.mx * 100));
      lv_obj_set_style_size(b.obj, 0, 0, LV_PART_INDICATOR);
      plain(b.obj);
      for (JsonObjectConst s : w["series"].as<JsonArrayConst>()) {
        b.serKeys.push_back(String(s["key"] | ""));
        b.ser.push_back(lv_chart_add_series(b.obj, parseColor(s["color"] | "#3fdc6c", col), LV_CHART_AXIS_PRIMARY_Y));
      }
      binds.push_back(b);
    } else if (type == "table") {
      b.kind = K_TABLE;
      b.rows = w["rows"] | 6;
      for (JsonVariantConst c : w["cols"].as<JsonArrayConst>()) b.cols.push_back(String(c.as<const char*>()));
      b.obj = lv_table_create(scr);
      lv_obj_set_pos(b.obj, w["x"] | 0, w["y"] | 0);
      int tw = w["w"] | 300;
      lv_obj_set_width(b.obj, tw);
      lv_table_set_column_count(b.obj, b.cols.size());
      lv_table_set_row_count(b.obj, b.rows);
      for (size_t c = 0; c < b.cols.size(); c++) lv_table_set_column_width(b.obj, c, tw / b.cols.size());
      lv_obj_set_style_text_font(b.obj, &lv_font_montserrat_12, 0);
      plain(b.obj);
      binds.push_back(b);
    } else if (type == "image") {
      lv_obj_t* img = lv_image_create(scr);
      String src = String("S:/assets/") + (w["src"] | "");
      lv_image_set_src(img, src.c_str());
      lv_obj_set_pos(img, w["x"] | 0, w["y"] | 0);
      plain(img);
    }
  }
  return scr;
}

static bool infoReveal = false;
static lv_obj_t* infoLbl = nullptr;
static void refreshInfo() { if (infoLbl) lv_label_set_text(infoLbl, net::infoText(infoReveal).c_str()); }
static void onReveal(lv_event_t*) { infoReveal = !infoReveal; refreshInfo(); }
static void onSlideToggle(lv_event_t*) {
  net::setSlideshow(!ss.on);   // persists and updates the UI config
  ui::refreshIndex();          // rebuild the menu with the new label
}
static void onInfoItem(lv_event_t*) { pendingNav = "\x02"; navPending = true; }

static lv_obj_t* buildInfo() {
  infoReveal = false;
  lv_obj_t* scr = lv_obj_create(nullptr);
  lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
  lv_obj_set_style_pad_all(scr, 0, 0);
  lv_obj_set_style_border_width(scr, 0, 0);
  lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
  infoLbl = lv_label_create(scr);
  lv_obj_set_style_text_font(infoLbl, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(infoLbl, lv_color_white(), 0);
  lv_obj_set_pos(infoLbl, 8, 8);
  lv_obj_set_width(infoLbl, SCREEN_W - 56);
  refreshInfo();
  lv_obj_t* btn = lv_button_create(scr);
  lv_obj_set_size(btn, 190, 32);
  lv_obj_set_pos(btn, 8, SCREEN_H - 40);
  lv_obj_t* l = lv_label_create(btn);
  lv_label_set_text(l, "Show / hide API key");
  lv_obj_center(l);
  lv_obj_add_event_cb(btn, onReveal, LV_EVENT_CLICKED, nullptr);
  return scr;
}

static void onMenuItem(lv_event_t* e) {
  size_t i = (size_t)lv_event_get_user_data(e);
  if (i < metas.size()) { pendingNav = metas[i].id; navPending = true; }
}

// High and critical cards are listed together first; " " sorts before any real group name.
static String groupOf(const Meta& m) { return m.prio >= 2 ? String(" Alerts") : m.menu; }

static void rebuildIndexLocked() {
  metas.clear();
  JsonDocument filter;
  filter["id"] = true; filter["title"] = true; filter["menu"] = true; filter["expires"] = true; filter["priority"] = true; filter["dwell"] = true; filter["widgets"][0]["src"] = true;
  filter["widgets"][0]["type"] = true;
  filter["widgets"][0]["series"][0]["key"] = true;
  for (const String& id : store::listScreenIds()) {
    JsonDocument doc;
    if (!store::readJson(store::screenPath(id), doc, &filter, MAX_SCREEN_BYTES)) continue;
    Meta m;
    m.id = id;
    int slash = id.lastIndexOf('/');
    m.title = doc["title"] | (slash < 0 ? id : id.substring(slash + 1)).c_str();
    m.menu = doc["menu"] | "";
    if (m.menu.isEmpty() && slash > 0) m.menu = id.substring(0, slash);   // group by parent path
    {
      String p = doc["priority"] | "normal";
      m.prio = p == "low" ? 0 : p == "high" ? 2 : p == "critical" ? 3 : 1;
    }
    m.mtime = store::mtime(store::screenPath(id));
    for (JsonObject w : doc["widgets"].as<JsonArray>())
      if (w["src"].is<const char*>()) m.assets.push_back(String(w["src"].as<const char*>()));
    m.dwell = (uint8_t)constrain((int)(doc["dwell"] | 0), 0, 120);
    m.expiresStr = doc["expires"] | "";
    if (!m.expiresStr.isEmpty() && !ui::parseUtc(m.expiresStr.c_str(), m.expires)) m.expires = 0;
    for (JsonObject w : doc["widgets"].as<JsonArray>())
      if (String(w["type"] | "") == "chart")
        for (JsonObject s : w["series"].as<JsonArray>()) m.chartKeys.push_back(String(s["key"] | ""));
    metas.push_back(m);
  }
  std::sort(metas.begin(), metas.end(), [](const Meta& a, const Meta& b) {
    String ga = groupOf(a), gb = groupOf(b);
    if (ga != gb) return ga < gb;
    if (a.prio != b.prio) return a.prio > b.prio;
    return a.title < b.title;
  });
}

static lv_obj_t* buildMenu() {
  lv_obj_t* scr = lv_obj_create(nullptr);
  lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
  lv_obj_set_style_pad_all(scr, 0, 0);
  lv_obj_set_style_border_width(scr, 0, 0);
  lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* title = lv_label_create(scr);
  lv_label_set_text(title, "cyd-canvas");
  lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
  lv_obj_set_style_text_color(title, lv_color_hex(0x4aa3ff), 0);
  lv_obj_set_pos(title, 8, 4);

  lv_obj_t* list = lv_list_create(scr);
  lv_obj_set_pos(list, 0, 26);
  lv_obj_set_size(list, SCREEN_W, SCREEN_H - 26 - 18);
  String group = "\x01";
  if (metas.empty()) {
    lv_list_add_text(list, store::ready() ? "No screens yet - upload one via the API" : "SD card not found");
  }
  for (size_t i = 0; i < metas.size(); i++) {
    String g = groupOf(metas[i]);
    if (g != group) {
      group = g;
      if (!group.isEmpty()) lv_list_add_text(list, group == " Alerts" ? "Alerts" : group.c_str());
    }
    lv_obj_t* btn = lv_list_add_button(list, metas[i].prio == 3 ? LV_SYMBOL_WARNING : nullptr, metas[i].title.c_str());
    if (metas[i].prio >= 2) {
      lv_color_t col = metas[i].prio == 3 ? lv_color_hex(0xff4d4d) : lv_color_hex(0xffc400);
      for (int k = 0; k < (int)lv_obj_get_child_count(btn); k++) lv_obj_set_style_text_color(lv_obj_get_child(btn, k), col, 0);
    } else if (metas[i].prio == 0) {
      for (int k = 0; k < (int)lv_obj_get_child_count(btn); k++) lv_obj_set_style_text_color(lv_obj_get_child(btn, k), lv_color_hex(0x888888), 0);
    }
    lv_obj_add_event_cb(btn, onMenuItem, LV_EVENT_CLICKED, (void*)i);
  }
  lv_list_add_text(list, "System");
  lv_obj_t* sb = lv_list_add_button(list, LV_SYMBOL_PLAY, ss.on ? "Slideshow: on (tap to stop)" : "Slideshow: off (tap to start)");
  lv_obj_add_event_cb(sb, onSlideToggle, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* ib = lv_list_add_button(list, LV_SYMBOL_SETTINGS, "Device info");
  lv_obj_add_event_cb(ib, onInfoItem, LV_EVENT_CLICKED, nullptr);
  statusLbl = lv_label_create(scr);
  lv_obj_set_style_text_font(statusLbl, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(statusLbl, lv_color_hex(0x888888), 0);
  lv_label_set_text(statusLbl, statusText.c_str());
  lv_obj_set_pos(statusLbl, 8, SCREEN_H - 16);
  return scr;
}

static void navigate(const String& target) {
  lv_obj_t* old = curScr;
  binds.clear();
  statusLbl = nullptr;
  infoLbl = nullptr;
  if (target == "\x02") {
    curId = "\x02";
    curScr = buildInfo();
  } else if (target == "\x01") {
    if (indexDirty) { indexDirty = false; rebuildIndexLocked(); }
    curId = "";
    curScr = buildMenu();
  } else {
    JsonDocument doc;
    if (!store::readJson(store::screenPath(target), doc, nullptr, MAX_SCREEN_BYTES)) {
      navigate("\x01");
      return;
    }
    loadData(target);
    if (doc["data"].is<JsonObject>()) mergeData(target, doc["data"].as<JsonObjectConst>(), SEED);
    curId = target;
    markUsed(target);
    curScr = buildScreen(target, doc);
    updateAll();
  }
  if (ackBtn) {
    if (!curId.isEmpty() && isCritical(curId) && !acked.count(curId)) lv_obj_remove_flag(ackBtn, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(ackBtn, LV_OBJ_FLAG_HIDDEN);
  }
  if (backBtn) {
    if (curId.isEmpty()) lv_obj_add_flag(backBtn, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_remove_flag(backBtn, LV_OBJ_FLAG_HIDDEN);
  }
  lv_screen_load(curScr);
  if (old) lv_obj_delete(old);
}

// ---------- public ----------
void ui::begin() {
  lvLock = xSemaphoreCreateRecursiveMutex();
  lv_init();
  lv_tick_set_cb(tickCb);

  lv_fs_drv_t* fs = new lv_fs_drv_t;
  lv_fs_drv_init(fs);
  fs->letter = 'S';
  fs->open_cb = fsOpen; fs->close_cb = fsClose; fs->read_cb = fsRead;
  fs->seek_cb = fsSeek; fs->tell_cb = fsTell;
  lv_fs_drv_register(fs);

  disp = lv_display_create(SCREEN_W, SCREEN_H);
  lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
  lv_display_set_flush_cb(disp, flushCb);
  lv_display_set_buffers(disp, dispBuf, nullptr, sizeof(dispBuf), LV_DISPLAY_RENDER_MODE_PARTIAL);

  lv_indev_t* in = lv_indev_create();
  lv_indev_set_type(in, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(in, touchCb);

  // Floating back button on the top layer, shown on every screen except the menu.
  backBtn = lv_button_create(lv_layer_top());
  lv_obj_set_size(backBtn, 44, 34);
  lv_obj_align(backBtn, LV_ALIGN_TOP_RIGHT, 0, 0);
  lv_obj_set_style_bg_color(backBtn, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(backBtn, 160, 0);
  lv_obj_set_style_radius(backBtn, 6, 0);
  lv_obj_set_style_shadow_width(backBtn, 0, 0);
  lv_obj_t* bl = lv_label_create(backBtn);
  lv_label_set_text(bl, LV_SYMBOL_CLOSE);
  lv_obj_center(bl);
  lv_obj_add_event_cb(backBtn, onLongPress, LV_EVENT_CLICKED, nullptr);
  lv_obj_add_flag(backBtn, LV_OBJ_FLAG_HIDDEN);

  // Explicit ACKNOWLEDGE button, shown only on unacknowledged critical cards (bottom-right).
  ackBtn = lv_button_create(lv_layer_top());
  lv_obj_set_size(ackBtn, 150, 40);
  lv_obj_align(ackBtn, LV_ALIGN_BOTTOM_RIGHT, -6, -6);
  lv_obj_set_style_bg_color(ackBtn, lv_color_hex(0xb32424), 0);
  lv_obj_set_style_radius(ackBtn, 8, 0);
  lv_obj_t* al = lv_label_create(ackBtn);
  lv_label_set_text(al, LV_SYMBOL_OK "  ACKNOWLEDGE");
  lv_obj_center(al);
  lv_obj_add_event_cb(ackBtn, onLongPress, LV_EVENT_CLICKED, nullptr);
  lv_obj_add_flag(ackBtn, LV_OBJ_FLAG_HIDDEN);

  LvGuard g;
  navigate("\x01");
}

void ui::loop() {
  static bool lastBoot = false;
  bool b = hal::bootPressed();
  if (b) lastInput = millis();
  if (ssActive && b && !lastBoot) ssActive = false;
  if (b && !lastBoot && !curId.isEmpty()) { LvGuard gg; goBack(); }
  lastBoot = b;

  LvGuard g;
  if (navPending) {
    navPending = false;
    navigate(pendingNav);
  } else if (indexDirty) {
    indexDirty = false;
    rebuildIndexLocked();   // clear first: a PUT during the rebuild must stay flagged
    if (curId.isEmpty()) navigate("\x01");
  }
  checkAlerts();
  slideshowTick();
  {
    // A missing/wedged card is retried every 10 s, so the display recovers by itself once it is back.
    static uint32_t lastMount = 0;
    if (!store::ready() && millis() - lastMount > 10000) {
      lastMount = millis();
      if (store::remount()) { Serial.println("store: SD card is back"); indexDirty = true; maintainSoon(); }
    }
  }
  static uint32_t lastInfo = 0;
  if (curId == "\x02" && millis() - lastInfo > 2000) { lastInfo = millis(); refreshInfo(); }
  static uint32_t lastSweep = 0;
  if (millis() - lastSweep > 20000 || (lastSweep == 0 && ui::clockValid())) {
    lastSweep = millis() ? millis() : 1;
    sweepExpired();
    maintain();
  }
  if (maintainFlag && (int32_t)(millis() - maintainAt) >= 0) { maintainFlag = false; maintain(); }
  if (millis() - lastPersist > 60000) {
    lastPersist = millis();
    for (auto& kv : data)
      if (kv.second.dirty) persist(kv.first);
  }
  lv_timer_handler();
}

void ui::refreshIndex() { indexDirty = true; }
bool ui::show(const String& id) {
  if (!store::validId(id) || !store::exists(store::screenPath(id))) return false;
  pendingNav = id; navPending = true;
  return true;
}
void ui::showMenu() { pendingNav = "\x01"; navPending = true; }
String ui::current() { return curId == "\x02" ? String("info") : curId; }
size_t ui::screenCount() { return metas.size(); }
String ui::debugData(const String& id) {
  LvGuard g;
  auto it = data.find(id);
  if (it == data.end()) return "no data entry for " + id;
  String s = "nums=" + String((unsigned)it->second.nums.size()) + " blobs=" + String((unsigned)it->second.blobs.size()) + " hist:";
  for (auto& h : it->second.hist) s += " " + h.first + "=" + String((unsigned)h.second.size());
  return s;
}

void ui::setStatusLine(const String& s) {
  LvGuard g;
  statusText = s;
  if (statusLbl) lv_label_set_text(statusLbl, s.c_str());
}

void ui::forgetScreen(const String& id) {
  LvGuard g;
  data.erase(id);
  acked.erase(id); critSeq.erase(id);
  indexDirty = true;
  if (curId == id) { pendingNav = "\x01"; navPending = true; }
}

bool ui::clockValid() { return time(nullptr) > 1700000000; }

bool ui::parseUtc(const char* s, int64_t& out) {
  int Y, M, D, h, m, sec;
  char z = 0;
  if (!s || strlen(s) != 20 || sscanf(s, "%4d-%2d-%2dT%2d:%2d:%2d%c", &Y, &M, &D, &h, &m, &sec, &z) != 7 || z != 'Z') return false;
  if (Y < 2020 || Y > 2200 || M < 1 || M > 12 || D < 1 || D > 31 || h > 23 || m > 59 || sec > 60) return false;
  int y = Y - (M <= 2);
  int era = y / 400;
  unsigned yoe = y - era * 400;
  unsigned doy = (153 * (M + (M > 2 ? -3 : 9)) + 2) / 5 + D - 1;
  unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  out = ((int64_t)era * 146097 + doe - 719468) * 86400 + h * 3600 + m * 60 + sec;
  return true;
}

static bool mergeData(const String& id, JsonObjectConst d, MergeMode mode) {
  LvGuard g;
  if (indexDirty) { indexDirty = false; rebuildIndexLocked(); }
  const Meta* m = nullptr;
  for (const Meta& x : metas) if (x.id == id) { m = &x; break; }
  if (!m) return false;
  if (mode == REPLACE) { data[id] = ScreenData(); data[id].loaded = true; }
  ScreenData& sd = loadData(id);
  auto isChartKey = [&](const String& k) { return std::find(m->chartKeys.begin(), m->chartKeys.end(), k) != m->chartKeys.end(); };
  for (JsonPairConst kv : d) {
    String k = kv.key().c_str();
    JsonVariantConst v = kv.value();
    bool have = sd.nums.count(k) || sd.blobs.count(k) || (sd.hist.count(k) && !sd.hist[k].empty());
    if (mode == SEED && have) continue;
    if (v.is<JsonArrayConst>() && v.size() && v[0].is<double>()) {   // numeric series = chart history
      if (isChartKey(k)) {
        auto& h = sd.hist[k];
        h.clear();
        JsonArrayConst a = v.as<JsonArrayConst>();
        size_t from = a.size() > 240 ? a.size() - 240 : 0;
        for (size_t i = from; i < a.size(); i++) h.push_back(a[i].as<float>());
      }
      sd.nums[k] = v[v.size() - 1].as<double>();
      continue;
    }
    if (v.is<bool>()) sd.nums[k] = v.as<bool>() ? 1 : 0;
    else if (v.is<double>()) sd.nums[k] = v.as<double>();
    else { String s; serializeJson(v, s); sd.blobs[k] = s; continue; }
    if (mode != SEED && isChartKey(k)) {
      auto& h = sd.hist[k];
      h.push_back((float)sd.nums[k]);
      while (h.size() > 240) h.pop_front();
    }
  }
  sd.dirty = true;
  markUsed(id);
  if (curId == id) updateAll();
  return true;
}

bool ui::applyData(const String& id, JsonObjectConst d) { return mergeData(id, d, MERGE); }
void ui::replaceData(const String& id, JsonObjectConst d) { mergeData(id, d, REPLACE); }

String ui::indexJson() {
  LvGuard g;
  if (indexDirty) { indexDirty = false; rebuildIndexLocked(); }
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  for (const Meta& m : metas) {
    JsonObject o = arr.add<JsonObject>();
    o["id"] = m.id; o["title"] = m.title; o["menu"] = m.menu;
    o["priority"] = m.prio == 0 ? "low" : m.prio == 2 ? "high" : m.prio == 3 ? "critical" : "normal";
    if (m.expiresStr.isEmpty()) o["expires"] = nullptr; else o["expires"] = m.expiresStr;
  }
  String s;
  serializeJson(doc, s);
  return s;
}

static void sweepExpired() {
  if (!ui::clockValid()) return;
  if (indexDirty) { indexDirty = false; rebuildIndexLocked(); }
  int64_t now = time(nullptr);
  std::vector<String> dead;
  for (const Meta& m : metas) if (m.expires && now >= m.expires) dead.push_back(m.id);
  for (const String& id : dead) {
    store::removeFile(store::screenPath(id));
    store::removeFile(store::dataPath(id));
    data.erase(id);
    acked.erase(id); critSeq.erase(id);
    if (curId == id) { pendingNav = "\x01"; navPending = true; }
    indexDirty = true;
    Serial.printf("expired: removed %s\n", id.c_str());
  }
  if (!dead.empty()) net::screensChanged();
}

// ---------- priority / alerts ----------
static String nextAlert() {
  String best;
  uint32_t bestSeq = 0;
  for (const Meta& m : metas) {
    if (m.prio != 3 || acked.count(m.id)) continue;
    uint32_t& sq = critSeq[m.id];
    if (!sq) sq = ++seqCounter;
    if (best.isEmpty() || sq > bestSeq) { best = m.id; bestSeq = sq; }
  }
  return best;
}

static void checkAlerts() {
  if (indexDirty) return;
  String a = nextAlert();
  if (a.isEmpty()) {
    if (alertActive) {
      alertActive = false;
      hal::led(false, false, false);
      hal::backlight(net::brightness());
    }
    return;
  }
  if (!alertActive) { alertActive = true; hal::backlight(100); }   // wake a dimmed display
  if (curId != a && !(navPending && pendingNav == a)) { pendingNav = a; navPending = true; }
  hal::led((millis() / 500) % 2 == 0, false, false);              // blink red until acknowledged
}

void ui::cardUpdated(const String& id) {
  LvGuard g;
  markUsed(id);
  lastUploaded = id;
  ui::maintainSoon();
  acked.erase(id);
  critSeq[id] = ++seqCounter;
}

bool ui::ack(const String& id) {
  LvGuard g;
  if (indexDirty) { indexDirty = false; rebuildIndexLocked(); }
  bool any = false;
  for (const Meta& m : metas)
    if (m.prio == 3 && (id.isEmpty() || m.id == id) && !acked.count(m.id)) {
      acked.insert(m.id);
      any = true;
      if (curId == m.id) { pendingNav = ""; navPending = true; }   // leave the card that was just acknowledged
    }
  return any || (!id.isEmpty() && isCritical(id));
}

int ui::alertCount() {
  LvGuard g;
  int n = 0;
  for (const Meta& m : metas) if (m.prio == 3 && !acked.count(m.id)) n++;
  return n;
}

// ---------- slideshow ----------
void ui::setSlideshow(bool on, int idleSec, int dwellSec, const String& prefix) {
  LvGuard g;
  ss.on = on;
  ss.idle = constrain(idleSec, 3, 3600);
  ss.dwell = constrain(dwellSec, 2, 600);
  ss.prefix = prefix;
  if (!on) ssActive = false;
  lastInput = millis();
}
bool ui::slideshowOn() { return ss.on; }
bool ui::slideshowRunning() { return ssActive; }

static void slideshowTick() {
  if (!ss.on || alertActive || navPending || indexDirty) return;
  uint32_t now = millis();
  if (!ssActive) {
    if (now - lastInput < (uint32_t)ss.idle * 1000UL) return;
    ssActive = true;
    ssNextAt = 0;
  }
  if ((int32_t)(now - ssNextAt) < 0) return;
  std::vector<const Meta*> eligible;
  for (const Meta& m : metas) {
    if (m.prio == 0 || (!ss.prefix.isEmpty() && !m.id.startsWith(ss.prefix))) continue;
    eligible.push_back(&m);
    if (m.prio >= 2) eligible.push_back(&m);     // high/critical get twice the screen time
  }
  if (eligible.empty()) { ssActive = false; lastInput = now; return; }
  ssCursor = (ssCursor + 1) % eligible.size();
  const Meta* pick = eligible[ssCursor];
  pendingNav = pick->id;
  navPending = true;
  ssNextAt = now + (uint32_t)(pick->dwell ? pick->dwell : ss.dwell) * 1000UL;
}

// ---------- housekeeping ----------
void ui::setLimits(int maxCards, int minFreeMb) {
  LvGuard g;
  lim.maxCards = constrain(maxCards, 4, 200);
  lim.minFreeMb = constrain(minFreeMb, 1, 4096);
  maintainSoon();
}
int ui::limitMaxCards() { return lim.maxCards; }
int ui::limitMinFreeMb() { return lim.minFreeMb; }
uint32_t ui::evictedTotal() { return evicted; }
void ui::maintainSoon() { maintainFlag = true; maintainAt = millis() + 1500; }

static void maintain() {
  if (!store::ready()) return;
  if (indexDirty) { indexDirty = false; rebuildIndexLocked(); }
  bool changed = false;

  // 1. keep within the card-count and free-space limits
  for (int guard = 0; guard < 64; guard++) {
    uint64_t freeMb = store::freeBytes() >> 20;
    bool over = (int)metas.size() > lim.maxCards || (int)freeMb < lim.minFreeMb;
    if (!over) break;
    const Meta* victim = nullptr;
    for (const Meta& m : metas) {
      if (m.id == curId || m.id == lastUploaded) continue;              // on screen / just uploaded
      if (m.prio == 3 && !acked.count(m.id)) continue;                  // unacknowledged critical
      if (!victim) { victim = &m; continue; }
      if (m.prio != victim->prio) { if (m.prio < victim->prio) victim = &m; continue; }
      int64_t a = std::max(m.mtime, usedAt.count(m.id) ? usedAt[m.id] : 0);
      int64_t b = std::max(victim->mtime, usedAt.count(victim->id) ? usedAt[victim->id] : 0);
      if (a != b) { if (a < b) victim = &m; continue; }
      if (useSeq[m.id] < useSeq[victim->id]) victim = &m;
    }
    if (!victim) break;
    String id = victim->id;
    Serial.printf("maintain: evicting %s (prio %d) cards=%u free=%lluMB\n", id.c_str(), victim->prio, (unsigned)metas.size(), (unsigned long long)freeMb);
    store::removeFile(store::screenPath(id));
    store::removeFile(store::dataPath(id));
    data.erase(id); acked.erase(id); critSeq.erase(id); usedAt.erase(id); useSeq.erase(id);
    evicted++;
    rebuildIndexLocked();
    changed = true;
  }

  // 2. orphans: data snapshots with no card, images no card uses (grace period for upload order)
  std::set<String> ids, used;
  for (const Meta& m : metas) { ids.insert(m.id); for (const String& a : m.assets) used.insert(a); }
  for (const String& name : store::listNames("/data", ".json")) {
    String id = name; id.replace('~', '/');
    if (!ids.count(id)) { store::removeFile("/data/" + name + ".json"); Serial.printf("maintain: removed orphan data %s\n", id.c_str()); }
  }
  if (ui::clockValid()) {
    int64_t now = time(nullptr);
    for (const String& name : store::listNames("/assets", ".bin")) {
      String file = name + ".bin";
      int64_t age = now - store::mtime("/assets/" + file);
      if (!used.count(file) && age > 600 && store::mtime("/assets/" + file) > 0) {
        store::removeFile("/assets/" + file);
        Serial.printf("maintain: removed unused image %s\n", file.c_str());
      }
    }
  }
  if (changed) { indexDirty = true; net::screensChanged(); }
}
