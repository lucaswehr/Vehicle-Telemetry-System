// Display bring-up: checks the wiring before any real screen code exists.
//   1. cycles solid red / green / blue / white / black (wrong colors = see notes below)
//   2. draws an orientation test: corner labels and a border, in landscape (480 x 320)
//   3. reads the FT6336U touch chip over I2C and shows where you touch
// Build/upload/monitor:  pio run -e display_test -t upload && pio device monitor
//
// Pins (display on HSPI):  CS 15, RESET 26, DC 27, MOSI 13, SCK 14, backlight 25
//                          touch SDA 21, SCL 22, RST 4, INT 35
// If the screen is white or dark but the serial output looks fine, the backlight pin is wired
// differently than assumed: try the LED pin on 3.3V or 5V instead of GPIO 25.
// If red and blue are swapped add  -DTFT_RGB_ORDER=TFT_BGR  to the build flags;
// if colors look inverted add  -DTFT_INVERSION_ON=1.
#include <Arduino.h>
#include <Wire.h>
#include <TFT_eSPI.h>

static const int TOUCH_SDA = 21;
static const int TOUCH_SCL = 22;
static const int TOUCH_RST = 4;
static const int TOUCH_INT = 35;
static const uint8_t TOUCH_ADDR = 0x38;   // FT6336U

TFT_eSPI tft;
static bool touchFound = false;

static bool touchRead(uint16_t& x, uint16_t& y) {
  Wire.beginTransmission(TOUCH_ADDR);
  Wire.write(0x02);                                   // TD_STATUS: number of touch points
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((int)TOUCH_ADDR, 5) != 5) return false;
  uint8_t n  = Wire.read() & 0x0F;
  uint8_t xh = Wire.read(), xl = Wire.read();
  uint8_t yh = Wire.read(), yl = Wire.read();
  if (n == 0 || n > 2) return false;
  x = (uint16_t)(((xh & 0x0F) << 8) | xl);            // raw panel coordinates (portrait, 320 x 480)
  y = (uint16_t)(((yh & 0x0F) << 8) | yl);
  return true;
}

static void colorCycle() {
  const uint16_t colors[] = {TFT_RED, TFT_GREEN, TFT_BLUE, TFT_WHITE, TFT_BLACK};
  const char* names[]     = {"red", "green", "blue", "white", "black"};
  for (int i = 0; i < 5; i++) {
    tft.fillScreen(colors[i]);
    Serial.printf("screen: %s\n", names[i]);
    delay(700);
  }
}

static void orientationScreen() {
  tft.setRotation(1);                                 // landscape, 480 x 320
  tft.fillScreen(TFT_BLACK);
  tft.drawRect(0, 0, tft.width(), tft.height(), TFT_WHITE);
  tft.drawRect(2, 2, tft.width() - 4, tft.height() - 4, TFT_DARKGREY);
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.setTextDatum(TL_DATUM);  tft.drawString("TOP LEFT", 8, 8, 2);
  tft.setTextDatum(TR_DATUM);  tft.drawString("TOP RIGHT", tft.width() - 8, 8, 2);
  tft.setTextDatum(BL_DATUM);  tft.drawString("BOTTOM LEFT", 8, tft.height() - 8, 2);
  tft.setTextDatum(BR_DATUM);  tft.drawString("BOTTOM RIGHT", tft.width() - 8, tft.height() - 8, 2);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("Vehicle Telemetry", tft.width() / 2, tft.height() / 2 - 30, 4);
  tft.drawString(String(tft.width()) + " x " + String(tft.height()), tft.width() / 2, tft.height() / 2, 4);
  tft.setTextColor(touchFound ? TFT_GREEN : TFT_RED, TFT_BLACK);
  tft.drawString(touchFound ? "touch chip found" : "touch chip NOT found", tft.width() / 2, tft.height() / 2 + 40, 2);
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\ndisplay_test");

  pinMode(TOUCH_RST, OUTPUT);                          // reset the touch chip (low pulse)
  digitalWrite(TOUCH_RST, LOW);  delay(10);
  digitalWrite(TOUCH_RST, HIGH); delay(100);
  pinMode(TOUCH_INT, INPUT);
  Wire.begin(TOUCH_SDA, TOUCH_SCL, 400000);
  Wire.beginTransmission(TOUCH_ADDR);
  touchFound = (Wire.endTransmission() == 0);
  Serial.printf("touch chip at 0x38: %s\n", touchFound ? "found" : "NOT found (check SDA/SCL/RST wiring)");

  tft.init();
  tft.setRotation(1);
  colorCycle();
  orientationScreen();
}

void loop() {
  static uint32_t lastPrint = 0;
  uint16_t x, y;
  if (touchFound && touchRead(x, y)) {
    tft.fillRect(8, tft.height() / 2 + 60, 300, 24, TFT_BLACK);
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.setTextDatum(TL_DATUM);
    tft.drawString("touch raw x=" + String(x) + " y=" + String(y), 8, tft.height() / 2 + 60, 2);
    if (millis() - lastPrint > 200) {
      Serial.printf("touch raw x=%u y=%u\n", x, y);
      lastPrint = millis();
    }
  }
  delay(20);
}
