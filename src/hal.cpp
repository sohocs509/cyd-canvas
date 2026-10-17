// SPDX-License-Identifier: Apache-2.0
#include "hal.h"
#include "config.h"
#include <Preferences.h>

static TFT_eSPI tft;

struct Cal {
  uint8_t valid = 0, swap = 0;
  float xs = 0, xo = 0, ys = 0, yo = 0;  // screen = off + raw * scale
};
static Cal cal;

TFT_eSPI& hal::display() { return tft; }

void hal::backlight(uint8_t pct) {
  uint32_t v = (uint32_t)constrain((int)pct, 0, 100) * 255 / 100;
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWrite(TFT_BL, v);
#else
  ledcWrite(0, v);
#endif
}

void hal::led(bool r, bool g, bool b) {
  digitalWrite(PIN_LED_R, !r);
  digitalWrite(PIN_LED_G, !g);
  digitalWrite(PIN_LED_B, !b);
}

uint16_t hal::ldrRaw() { return analogRead(PIN_LDR); }
bool hal::bootPressed() { return digitalRead(PIN_BOOT) == LOW; }

static uint16_t xptRead(uint8_t cmd) {
  digitalWrite(PIN_T_CS, LOW);
  for (int i = 7; i >= 0; i--) {
    digitalWrite(PIN_T_MOSI, (cmd >> i) & 1);
    digitalWrite(PIN_T_CLK, HIGH);
    delayMicroseconds(1);
    digitalWrite(PIN_T_CLK, LOW);
    delayMicroseconds(1);
  }
  uint16_t v = 0;
  for (int i = 0; i < 16; i++) {
    digitalWrite(PIN_T_CLK, HIGH);
    delayMicroseconds(1);
    v = (v << 1) | digitalRead(PIN_T_MISO);
    digitalWrite(PIN_T_CLK, LOW);
    delayMicroseconds(1);
  }
  digitalWrite(PIN_T_CS, HIGH);
  return (v >> 3) & 0xFFF;
}

static bool rawTouch(int& rx, int& ry) {
  if (digitalRead(PIN_T_IRQ) != LOW) return false;
  xptRead(0xD0);  // discard first conversion
  long sx = 0, sy = 0;
  const int N = 4;
  for (int i = 0; i < N; i++) {
    sx += xptRead(0xD0);
    sy += xptRead(0x90);
  }
  if (digitalRead(PIN_T_IRQ) != LOW) return false;
  rx = sx / N;
  ry = sy / N;
  return true;
}

bool hal::touch(int16_t& x, int16_t& y) {
  int rx, ry;
  if (!cal.valid || !rawTouch(rx, ry)) return false;
  float ax = cal.swap ? ry : rx;
  float ay = cal.swap ? rx : ry;
  x = constrain((int)(cal.xo + ax * cal.xs), 0, SCREEN_W - 1);
  y = constrain((int)(cal.yo + ay * cal.ys), 0, SCREEN_H - 1);
  return true;
}

bool hal::hasCalibration() { return cal.valid; }

static void waitRelease() {
  int a, b;
  uint32_t quiet = 0;
  while (quiet < 150) {
    if (rawTouch(a, b)) quiet = 0; else { delay(10); quiet += 10; }
  }
}

static void tapAt(int sx, int sy, int& rx, int& ry) {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawCentreString("Touch calibration", SCREEN_W / 2, 100, 4);
  tft.drawCentreString("Tap the crosshair", SCREEN_W / 2, 135, 2);
  tft.drawFastHLine(sx - 10, sy, 21, TFT_RED);
  tft.drawFastVLine(sx, sy - 10, 21, TFT_RED);
  waitRelease();
  while (true) {
    delay(20);
    if (!rawTouch(rx, ry)) continue;
    // settle: take the average of a few consecutive presses
    long ax = 0, ay = 0; int n = 0, a, b;
    for (int i = 0; i < 8; i++) { delay(15); if (rawTouch(a, b)) { ax += a; ay += b; n++; } }
    if (n >= 4) { rx = ax / n; ry = ay / n; return; }
  }
}

void hal::calibrate() {
  const int X1 = 20, X2 = SCREEN_W - 20, Y1 = 20, Y2 = SCREEN_H - 20;
  int r1x, r1y, r2x, r2y, r3x, r3y;
  tapAt(X1, Y1, r1x, r1y);  // top-left
  tapAt(X2, Y1, r2x, r2y);  // top-right
  tapAt(X2, Y2, r3x, r3y);  // bottom-right
  waitRelease();
  Cal c;
  c.swap = abs(r2y - r1y) > abs(r2x - r1x);
  float ax1 = c.swap ? r1y : r1x, ax2 = c.swap ? r2y : r2x;
  float ay1 = c.swap ? r1x : r1y, ay3 = c.swap ? r3x : r3y;
  if (fabsf(ax2 - ax1) < 200 || fabsf(ay3 - ay1) < 200) {
    tft.fillScreen(TFT_BLACK);
    tft.drawCentreString("Calibration failed, retrying", SCREEN_W / 2, 110, 2);
    delay(1500);
    return calibrate();
  }
  c.xs = (float)(X2 - X1) / (ax2 - ax1);
  c.xo = X1 - ax1 * c.xs;
  c.ys = (float)(Y2 - Y1) / (ay3 - ay1);
  c.yo = Y1 - ay1 * c.ys;
  c.valid = 1;
  cal = c;
  Preferences p;
  p.begin("cyd", false);
  p.putBytes("cal", &cal, sizeof(cal));
  p.end();
  tft.fillScreen(TFT_BLACK);
}

void hal::begin() {
  pinMode(PIN_BOOT, INPUT_PULLUP);
  pinMode(PIN_LED_R, OUTPUT); pinMode(PIN_LED_G, OUTPUT); pinMode(PIN_LED_B, OUTPUT);
  led(false, false, false);

  pinMode(PIN_T_CLK, OUTPUT); pinMode(PIN_T_MOSI, OUTPUT); pinMode(PIN_T_CS, OUTPUT);
  pinMode(PIN_T_MISO, INPUT); pinMode(PIN_T_IRQ, INPUT);
  digitalWrite(PIN_T_CS, HIGH); digitalWrite(PIN_T_CLK, LOW);

  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcAttach(TFT_BL, 5000, 8);
#else
  ledcSetup(0, 5000, 8);
  ledcAttachPin(TFT_BL, 0);
#endif
  backlight(100);

  Preferences p;
  p.begin("cyd", true);
  if (p.getBytesLength("cal") == sizeof(cal)) p.getBytes("cal", &cal, sizeof(cal));
  p.end();
}
