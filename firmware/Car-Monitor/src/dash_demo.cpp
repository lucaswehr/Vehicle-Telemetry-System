// Dashboard demo: draws the three real pages (DRIVE / LOCATION / SYSTEM) in portrait using FAKE data,
// so you can judge layout, readability and alert colors before any live data is wired in.
//   pio run -e dash_demo -t upload && pio device monitor
// Tap the screen (or send 'n' over serial) to change page. The fake scenario changes every 12 s:
//   0 normal drive, 1 hot coolant (amber), 2 overheating + low voltage (red), 3 key off (readings go
//   to "--"), 4 GPS no fix, 5 SD card full. The scenario name is shown in the footer.
// If the picture is upside down, change ROTATION from 0 to 2.
#include <Arduino.h>
#include <Wire.h>
#include <TFT_eSPI.h>
#include <math.h>
#include "dashmodel.h"

static const uint8_t ROTATION = 0;               // 0 = portrait (320 x 480), 2 = portrait upside down
static const int SCREEN_W = 320, SCREEN_H = 480;
static const int HEADER_H = 40, FOOTER_H = 22;
static const int BODY_TOP = HEADER_H, BODY_H = SCREEN_H - HEADER_H - FOOTER_H;

// colors (RGB565)
static const uint16_t C_BG = TFT_BLACK, C_LABEL = 0x8410, C_LINE = 0x2104, C_NONE = 0x4208;
static const uint16_t C_OK = 0x07E0, C_INFO = 0x5D9F, C_WARN = 0xFD20, C_BAD = 0xF800, C_TEXT = TFT_WHITE;

static const int TOUCH_SDA = 21, TOUCH_SCL = 22, TOUCH_RST = 4, TOUCH_INT = 35;
static const uint8_t TOUCH_ADDR = 0x38;

TFT_eSPI tft;
static bool touchFound = false;
static DashInput in;
static uint8_t page = 0;
static bool pageDrawn = false;

// what is currently drawn in each row, so only changed rows are redrawn (no flicker)
struct RowCache { char value[18]; Level level; bool valid; };
static RowCache cache[DASH_MAX_FIELDS];

static const char* SCENARIOS[] = {"normal", "hot coolant", "overheat + low volts", "key off", "gps no fix", "sd full"};
static const int SCENARIO_COUNT = 6;
static const uint32_t SCENARIO_MS = 12000;

static bool touchDown() {
  Wire.beginTransmission(TOUCH_ADDR);
  Wire.write(0x02);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((int)TOUCH_ADDR, 1) != 1) return false;
  uint8_t n = Wire.read() & 0x0F;
  return n > 0 && n <= 2;
}

// ---------- fake data ----------
static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

static void simulate(uint32_t now) {
  float t = now / 1000.0f;
  int sc = (now / SCENARIO_MS) % SCENARIO_COUNT;

  float mph = 30.0f + 25.0f * sinf(t / 8.0f);                 // 5..55 mph
  float rpm = 800.0f + mph * 38.0f + 40.0f * sinf(t * 3.0f);
  float coolantC = 90.0f, volts = 14.1f;
  if (sc == 1) coolantC = 111.0f;                              // 232 F
  if (sc == 2) { coolantC = 118.0f; volts = 11.6f; }           // 244 F, charging system failing

  bool obdLive = (sc != 3);
  if (obdLive) {
    sampleSet(in.obdSpeedKmh, mph * 1.609344f, now);
    sampleSet(in.rpm, rpm, now);
    sampleSet(in.throttlePct, clampf(20.0f + 15.0f * sinf(t / 3.0f), 0, 100), now);
    sampleSet(in.loadPct, clampf(30.0f + 20.0f * sinf(t / 5.0f), 0, 100), now);
    sampleSet(in.coolantC, coolantC, now);
    sampleSet(in.volts, volts, now);
  } else {
    static uint32_t lastV = 0;                                 // key off: only the adapter voltage, every 5 s
    if (now - lastV >= 5000) { sampleSet(in.volts, 12.4f, now); lastV = now; }
  }
  in.elm = obdLive ? ElmLink::LIVE : ElmLink::KEY_OFF;

  bool fix = (sc != 4);
  in.gps = fix ? GpsStatus::OK : GpsStatus::NO_FIX;
  in.sats = fix ? 9 : 3;
  if (fix) {
    sampleSet(in.gpsSpeedMph, mph, now);
    sampleSet(in.lat, 46.7298f + 0.00002f * t, now);
    sampleSet(in.lon, -117.1817f + 0.00003f * t, now);
    sampleSet(in.altM, 717.0f + 2.0f * sinf(t / 10.0f), now);
    sampleSet(in.headingDeg, 84.0f + 20.0f * sinf(t / 6.0f), now);
  }
  strcpy(in.city, "Pullman");
  strcpy(in.cityState, "WA");
  in.cityKnown = fix;

  in.sd = (sc == 5) ? SdState::FULL : SdState::LOGGING;
  in.sdFreeKnown = true;
  in.sdFreeMb = (sc == 5) ? 0 : 15000;

  float m;
  if (dashSpeedMph(in, now, m)) tripUpdate(in.trip, m, now);
}

// ---------- drawing ----------
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

static void drawFooter(int sc) {
  tft.fillRect(0, SCREEN_H - FOOTER_H, SCREEN_W, FOOTER_H, C_BG);
  tft.setTextColor(C_LABEL, C_BG);
  tft.setTextDatum(ML_DATUM);
  tft.drawString(String("demo: ") + SCENARIOS[sc], 10, SCREEN_H - FOOTER_H / 2, 2);
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
    tft.setTextDatum(TR_DATUM);
    tft.drawString(p.f[i].unit, SCREEN_W - 10, top + 4, 2);
    tft.drawFastHLine(0, top + rh - 1, SCREEN_W, C_LINE);
    cache[i].valid = false;
  }
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
}

static void render(uint32_t now) {
  DashPage p;
  dashBuild(in, now, page, p);
  if (!pageDrawn) { drawStatic(p); drawFooter((now / SCENARIO_MS) % SCENARIO_COUNT); pageDrawn = true; }
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

static void nextPage() { page = (page + 1) % DASH_PAGES; pageDrawn = false; }

void setup() {
  Serial.begin(115200);
  pinMode(TOUCH_RST, OUTPUT);
  digitalWrite(TOUCH_RST, LOW);  delay(10);
  digitalWrite(TOUCH_RST, HIGH); delay(100);
  pinMode(TOUCH_INT, INPUT);
  Wire.begin(TOUCH_SDA, TOUCH_SCL, 400000);
  Wire.beginTransmission(TOUCH_ADDR);
  touchFound = (Wire.endTransmission() == 0);
  Serial.printf("dash_demo, touch %s\n", touchFound ? "found" : "NOT found");

  tft.init();
  tft.setRotation(ROTATION);
  tft.fillScreen(C_BG);
}

void loop() {
  static uint32_t lastFrame = 0, lastTap = 0, lastScenario = 99;
  static bool wasDown = false;
  uint32_t now = millis();

  bool down = touchFound && touchDown();
  if (down && !wasDown && now - lastTap > 300) { nextPage(); lastTap = now; }
  wasDown = down;
  if (Serial.available() && Serial.read() == 'n') nextPage();

  if (now - lastFrame >= 100) {
    lastFrame = now;
    simulate(now);
    render(now);
    uint32_t sc = (now / SCENARIO_MS) % SCENARIO_COUNT;
    if (sc != lastScenario) { drawFooter(sc); lastScenario = sc; Serial.printf("scenario: %s\n", SCENARIOS[sc]); }
  }
}
