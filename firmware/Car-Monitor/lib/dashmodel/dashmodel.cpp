#include "dashmodel.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

// ---------- samples ----------
void sampleSet(Sample& s, float v, uint32_t nowMs) { s.v = v; s.atMs = nowMs; s.have = true; }

bool sampleFresh(const Sample& s, uint32_t nowMs, uint32_t maxAgeMs) {
  return s.have && (uint32_t)(nowMs - s.atMs) <= maxAgeMs;
}

// ---------- trip ----------
void tripReset(TripStats& t) { t = TripStats(); }

void tripUpdate(TripStats& t, float mph, uint32_t nowMs) {
  if (mph < 0) mph = 0;
  bool justStarted = false;
  if (!t.started && mph >= DASH_TRIP_START_MPH) {
    t.started = true;
    t.startMs = nowMs;
    justStarted = true;                           // the time before this tick was spent below the start speed
  }
  if (t.started) {
    if (t.haveLast && !justStarted) {
      uint32_t dt = nowMs - t.lastMs;
      if (dt <= 2000) t.miles += mph * (float)dt / 3600000.0f;
    }
    if (mph > t.topMph) t.topMph = mph;
  }
  t.lastMs = nowMs;
  t.haveLast = true;
}

// ---------- conversions and levels ----------
float cToF(float c) { return c * 9.0f / 5.0f + 32.0f; }
float mToFt(float m) { return m * 3.28084f; }

Level levelCoolantF(float f) {
  if (f >= DASH_COOLANT_BAD_F) return Level::BAD;
  if (f >= DASH_COOLANT_WARN_F) return Level::WARN;
  return Level::OK;
}

Level levelRpm(float rpm) {
  if (rpm >= DASH_RPM_BAD) return Level::BAD;
  if (rpm >= DASH_RPM_WARN) return Level::WARN;
  return Level::OK;
}

Level levelVoltage(float v, bool running) {
  if (running) {
    if (v < DASH_VOLT_RUN_BAD) return Level::BAD;
    if (v < DASH_VOLT_RUN_WARN || v > DASH_VOLT_RUN_HIGH) return Level::WARN;
    return Level::OK;
  }
  if (v < DASH_VOLT_OFF_BAD) return Level::BAD;
  if (v < DASH_VOLT_OFF_WARN) return Level::WARN;
  return Level::OK;
}

Level levelSats(uint8_t sats) {
  if (sats >= DASH_SATS_OK) return Level::OK;
  if (sats >= DASH_SATS_WARN) return Level::WARN;
  return Level::BAD;
}

const char* compassPoint(float deg) {
  static const char* const names[8] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
  float d = fmodf(deg, 360.0f);
  if (d < 0) d += 360.0f;
  int idx = (int)((d + 22.5f) / 45.0f) % 8;
  return names[idx];
}

// ---------- source selection ----------
bool dashEngineRunning(const DashInput& in, uint32_t nowMs) {
  return sampleFresh(in.rpm, nowMs, DASH_OBD_MAX_AGE_MS) && in.rpm.v > DASH_ENGINE_RUNNING_RPM;
}

bool dashSpeedMph(const DashInput& in, uint32_t nowMs, float& mph) {
  if (sampleFresh(in.obdSpeedKmh, nowMs, DASH_OBD_MAX_AGE_MS)) {
    mph = in.obdSpeedKmh.v * 0.621371f;          // the car's own speed works below 5 mph, where GPS is noisy
    return true;
  }
  if (in.gps == GpsStatus::OK && sampleFresh(in.gpsSpeedMph, nowMs, DASH_GPS_MAX_AGE_MS)) {
    mph = in.gpsSpeedMph.v;
    return true;
  }
  return false;
}

// ---------- building fields ----------
static int rnd(float v) { return (int)(v + (v >= 0 ? 0.5f : -0.5f)); }

static void copyTo(char* dst, size_t cap, const char* src) {
  strncpy(dst, src, cap - 1);
  dst[cap - 1] = '\0';
}

static DashField* add(DashPage& p, const char* label, const char* unit) {
  DashField* f = &p.f[p.count++];
  copyTo(f->label, sizeof f->label, label);
  copyTo(f->unit, sizeof f->unit, unit);
  copyTo(f->value, sizeof f->value, "--");
  f->level = Level::NONE;
  return f;
}

static void setText(DashField* f, const char* text, Level lv) {
  copyTo(f->value, sizeof f->value, text);
  f->level = lv;
}

static void setInt(DashField* f, int v, Level lv) {
  snprintf(f->value, sizeof f->value, "%d", v);
  f->level = lv;
}

static void setDec(DashField* f, float v, int decimals, Level lv) {
  snprintf(f->value, sizeof f->value, "%.*f", decimals, (double)v);
  f->level = lv;
}

static bool gpsFresh(const DashInput& in, const Sample& s, uint32_t now) {
  return in.gps == GpsStatus::OK && sampleFresh(s, now, DASH_GPS_MAX_AGE_MS);
}

static void pageDrive(const DashInput& in, uint32_t now, DashPage& p) {
  p.title = "DRIVE";
  float mph;
  DashField* f = add(p, "SPEED", "mph");
  if (dashSpeedMph(in, now, mph)) setInt(f, rnd(mph), Level::OK);

  f = add(p, "RPM", "");
  if (sampleFresh(in.rpm, now, DASH_OBD_MAX_AGE_MS)) setInt(f, rnd(in.rpm.v), levelRpm(in.rpm.v));

  f = add(p, "THROTTLE", "%");
  if (sampleFresh(in.throttlePct, now, DASH_OBD_MAX_AGE_MS)) setInt(f, rnd(in.throttlePct.v), Level::OK);

  f = add(p, "ENGINE LOAD", "%");
  if (sampleFresh(in.loadPct, now, DASH_OBD_MAX_AGE_MS)) setInt(f, rnd(in.loadPct.v), Level::OK);

  f = add(p, "COOLANT", "F");
  if (sampleFresh(in.coolantC, now, DASH_OBD_MAX_AGE_MS * 5)) {   // slow group: refreshed about once a second
    float fh = cToF(in.coolantC.v);
    setInt(f, rnd(fh), levelCoolantF(fh));
  }

  f = add(p, "VOLTAGE", "V");
  if (sampleFresh(in.volts, now, DASH_VOLT_MAX_AGE_MS))
    setDec(f, in.volts.v, 1, levelVoltage(in.volts.v, dashEngineRunning(in, now)));
}

static void formatDeg(char* out, size_t cap, float deg, char pos, char neg) {
  char h = deg >= 0 ? pos : neg;
  snprintf(out, cap, "%.5f %c", (double)fabsf(deg), h);
}

static void pageLocation(const DashInput& in, uint32_t now, DashPage& p) {
  p.title = "LOCATION";
  DashField* f = add(p, "ALTITUDE", "ft");
  if (gpsFresh(in, in.altM, now)) setInt(f, rnd(mToFt(in.altM.v)), Level::OK);

  f = add(p, "LATITUDE", "");
  if (gpsFresh(in, in.lat, now)) { formatDeg(f->value, sizeof f->value, in.lat.v, 'N', 'S'); f->level = Level::OK; }

  f = add(p, "LONGITUDE", "");
  if (gpsFresh(in, in.lon, now)) { formatDeg(f->value, sizeof f->value, in.lon.v, 'E', 'W'); f->level = Level::OK; }

  f = add(p, "CITY", "");
  if (in.gps == GpsStatus::OK && in.cityKnown && in.city[0]) {
    setText(f, in.city, Level::OK);
    copyTo(f->unit, sizeof f->unit, in.cityState);          // "WA" appears small on the right of the row
  }

  f = add(p, "HEADING", "");
  if (gpsFresh(in, in.headingDeg, now)) {
    snprintf(f->value, sizeof f->value, "%d %s", rnd(in.headingDeg.v) % 360, compassPoint(in.headingDeg.v));
    f->level = Level::OK;
  }

  f = add(p, "TRIP", "mi");
  if (in.trip.started) setDec(f, in.trip.miles, 1, Level::OK);

  f = add(p, "TOP SPEED", "mph");
  if (in.trip.started) setInt(f, rnd(in.trip.topMph), Level::OK);

  f = add(p, "TRIP TIME", "");
  if (in.trip.started) {
    uint32_t s = (now - in.trip.startMs) / 1000;
    snprintf(f->value, sizeof f->value, "%u:%02u:%02u", (unsigned)(s / 3600), (unsigned)((s / 60) % 60), (unsigned)(s % 60));
    f->level = Level::OK;
  }
}

static void pageSystem(const DashInput& in, uint32_t now, DashPage& p) {
  (void)now;
  p.title = "SYSTEM";
  DashField* f = add(p, "SD CARD", "");
  switch (in.sd) {
    case SdState::UNMOUNTED: setText(f, "DOWN", Level::BAD); break;
    case SdState::ARMED:     setText(f, "READY", Level::INFO); break;
    case SdState::LOGGING:   setText(f, "LOGGING", Level::OK); break;
    case SdState::FULL:      setText(f, "FULL", Level::BAD); break;
  }

  f = add(p, "SD FREE", "");
  if (in.sd != SdState::UNMOUNTED && in.sdFreeKnown) {
    Level lv = in.sdFreeMb < DASH_SD_FREE_BAD_MB ? Level::BAD : in.sdFreeMb < DASH_SD_FREE_WARN_MB ? Level::WARN : Level::OK;
    if (in.sdFreeMb >= 1000) snprintf(f->value, sizeof f->value, "%.1f GB", (double)in.sdFreeMb / 1024.0);
    else snprintf(f->value, sizeof f->value, "%u MB", (unsigned)in.sdFreeMb);
    f->level = lv;
  }

  f = add(p, "ELM327", "");
  switch (in.elm) {
    case ElmLink::OFF:        setText(f, "OFF", Level::WARN); break;
    case ElmLink::CONNECTING: setText(f, "CONNECTING", Level::INFO); break;
    case ElmLink::LIVE:       setText(f, "LIVE", Level::OK); break;
    case ElmLink::KEY_OFF:    setText(f, "KEY OFF", Level::INFO); break;
    case ElmLink::DOWN:       setText(f, "LOST", Level::BAD); break;
  }

  f = add(p, "GPS", "");
  switch (in.gps) {
    case GpsStatus::OK:        setText(f, "FIX", Level::OK); break;
    case GpsStatus::NO_FIX:    setText(f, "NO FIX", Level::WARN); break;
    case GpsStatus::LINK_LOST: setText(f, "LOST", Level::BAD); break;
  }

  f = add(p, "SATELLITES", "");
  if (in.gps != GpsStatus::LINK_LOST) setInt(f, in.sats, levelSats(in.sats));
}

void dashBuild(const DashInput& in, uint32_t nowMs, uint8_t page, DashPage& out) {
  out.count = 0;
  out.title = "";
  switch (page) {
    case 0:  pageDrive(in, nowMs, out); break;
    case 1:  pageLocation(in, nowMs, out); break;
    default: pageSystem(in, nowMs, out); break;
  }
}
