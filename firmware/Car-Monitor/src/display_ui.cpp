#include <Arduino.h>
#include <Wire.h>
#include <TFT_eSPI.h>
#include <string.h>
#include "display_ui.h"

static const uint8_t ROTATION = 0;               // 0 = portrait (320 x 480); use 2 if the picture is upside down
static const int SCREEN_W = 320, SCREEN_H = 480;
static const int HEADER_H = 40;
static const int BODY_TOP = HEADER_H, BODY_H = SCREEN_H - HEADER_H;
static const uint32_t FRAME_MS = 100;

static const uint16_t C_BG = TFT_BLACK, C_LABEL = 0x8410, C_LINE = 0x2104, C_NONE = 0x4208;
static const uint16_t C_OK = 0x07E0, C_INFO = 0x5D9F, C_WARN = 0xFD20, C_BAD = 0xF800, C_TEXT = TFT_WHITE;

static const int TOUCH_SDA = 21, TOUCH_SCL = 22, TOUCH_RST = 4, TOUCH_INT = 35;
static const uint8_t TOUCH_ADDR = 0x38;          // FT6336U

static TFT_eSPI tft;
static bool touchFound = false;
static uint8_t page = 0;
static bool pageDrawn = false;

// what is drawn in each row, so only changed rows are redrawn (no flicker)
struct RowCache { char value[18]; Level level; bool valid; };
static RowCache cache[DASH_MAX_FIELDS];

static bool touchDown() {
  Wire.beginTransmission(TOUCH_ADDR);
  Wire.write(0x02);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((int)TOUCH_ADDR, 1) != 1) return false;
  uint8_t n = Wire.read() & 0x0F;
  return n > 0 && n <= 2;
}

static uint16_t colorFor(Level lv, bool okIsGreen) {
  switch (lv) {
    case Level::OK:   return okIsGreen ? C_OK : C_TEXT;
    case Level::INFO: return C_INFO;
    case Level::WARN: return C_WARN;
    case Level::BAD:  return C_BAD;
    default:          return C_NONE;
  }
}

static bool isNumeric(const char* s) {
  if (!*s) return false;
  for (; *s; s++) if (!((*s >= '0' && *s <= '9') || *s == '.' || *s == '-' || *s == ':')) return false;
  return true;
}

static int rowH(int count) { return BODY_H / count; }

static void drawHeader(const DashPage& p) {
  tft.fillRect(0, 0, SCREEN_W, HEADER_H, C_BG);
  tft.setTextColor(C_TEXT, C_BG);
  tft.setTextDatum(ML_DATUM);
  tft.drawString(p.title, 10, HEADER_H / 2, 4);
  for (int i = 0; i < DASH_PAGES; i++) {                       // page dots
    int cx = SCREEN_W - 20 - (DASH_PAGES - 1 - i) * 22;
    if (i == page) tft.fillCircle(cx, HEADER_H / 2, 6, C_TEXT);
    else           tft.drawCircle(cx, HEADER_H / 2, 6, C_LABEL);
  }
  tft.drawFastHLine(0, HEADER_H - 1, SCREEN_W, C_LINE);
}

static void drawStatic(const DashPage& p) {                    // labels and units, redrawn on page change
  tft.fillScreen(C_BG);
  drawHeader(p);
  int rh = rowH(p.count);
  for (uint8_t i = 0; i < p.count; i++) {
    int top = BODY_TOP + i * rh;
    tft.setTextColor(C_LABEL, C_BG);
    tft.setTextDatum(TL_DATUM);
    tft.drawString(p.f[i].label, 10, top + 4, 2);
    tft.drawFastHLine(0, top + rh - 1, SCREEN_W, C_LINE);
    cache[i].valid = false;
  }
}

static void drawUnit(const DashPage& p, uint8_t i) {           // units can change (the city's state), so they are redrawn with the value
  int rh = rowH(p.count);
  int top = BODY_TOP + i * rh;
  tft.fillRect(SCREEN_W - 80, top + 2, 76, 18, C_BG);
  tft.setTextColor(C_LABEL, C_BG);
  tft.setTextDatum(TR_DATUM);
  tft.drawString(p.f[i].unit, SCREEN_W - 10, top + 4, 2);
}

static void drawValue(const DashPage& p, uint8_t i) {
  const DashField& f = p.f[i];
  int rh = rowH(p.count);
  int top = BODY_TOP + i * rh;
  bool big = (rh >= 70) && isNumeric(f.value);
  int font = big ? 6 : 4;
  tft.fillRect(0, top + 22, SCREEN_W, rh - 23, C_BG);
  tft.setTextColor(colorFor(f.level, page == 2), C_BG);
  tft.setTextDatum(TL_DATUM);
  tft.drawString(f.value, 10, top + 22 + (big ? 0 : 6), font);
  drawUnit(p, i);
}

void displayNextPage() { page = (page + 1) % DASH_PAGES; pageDrawn = false; }

void displayInit() {
  pinMode(TOUCH_RST, OUTPUT);                                  // reset the touch chip
  digitalWrite(TOUCH_RST, LOW);  delay(10);
  digitalWrite(TOUCH_RST, HIGH); delay(100);
  pinMode(TOUCH_INT, INPUT);
  Wire.begin(TOUCH_SDA, TOUCH_SCL, 400000);
  Wire.beginTransmission(TOUCH_ADDR);
  touchFound = (Wire.endTransmission() == 0);
  Serial.printf("display: touch %s\n", touchFound ? "found" : "NOT found (page changes by serial 'n' only)");
  tft.init();
  tft.setRotation(ROTATION);
  tft.fillScreen(C_BG);
}

void displayTick(const DashInput& in, uint32_t now) {
  static uint32_t lastFrame = 0, lastTap = 0;
  static bool wasDown = false;

  bool down = touchFound && (now - lastTap > 300 || wasDown) && touchDown();
  if (down && !wasDown && now - lastTap > 300) { displayNextPage(); lastTap = now; }
  wasDown = down;

  if (now - lastFrame < FRAME_MS) return;
  lastFrame = now;

  DashPage p;
  dashBuild(in, now, page, p);
  if (!pageDrawn) { drawStatic(p); pageDrawn = true; }
  for (uint8_t i = 0; i < p.count; i++) {
    RowCache& c = cache[i];
    if (!c.valid || c.level != p.f[i].level || strcmp(c.value, p.f[i].value) != 0) {
      drawValue(p, i);
      strncpy(c.value, p.f[i].value, sizeof c.value);
      c.level = p.f[i].level;
      c.valid = true;
    }
  }
}
