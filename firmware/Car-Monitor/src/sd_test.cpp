#include <Arduino.h>
#include <FS.h>
#include <SD.h>
#include <SPI.h>

constexpr uint8_t SD_CS_PIN = 5;

void setup() {
  Serial.begin(115200);
  delay(2000);

  Serial.println();
  Serial.println("SD card test");
  Serial.println("CS=GPIO 5, SCK=GPIO 18, MISO=GPIO 19, MOSI=GPIO 23");

  if (!SD.begin(SD_CS_PIN, SPI, 4000000)) {
    Serial.println("FAIL: SD card could not be mounted");
    Serial.println("Check 3.3V power, ground, connections, CS, and formatting");
    return;
  }

  uint8_t cardType = SD.cardType();
  if (cardType == CARD_NONE) {
    Serial.println("FAIL: no SD card detected");
    return;
  }

  Serial.print("Card type: ");
  if (cardType == CARD_MMC) Serial.println("MMC");
  else if (cardType == CARD_SD) Serial.println("SDSC");
  else if (cardType == CARD_SDHC) Serial.println("SDHC/SDXC");
  else Serial.println("UNKNOWN");

  uint64_t cardBytes = SD.cardSize();
  Serial.printf("Card size: %llu MB\n", cardBytes / (1024 * 1024));

  const char* path = "/sd_test.txt";
  File file = SD.open(path, FILE_WRITE);
  if (!file) {
    Serial.printf("FAIL: could not open %s for writing\n", path);
    return;
  }

  file.println("ESP32 SD card write test");
  file.printf("Write number: %lu\n", millis());
  file.close();
  Serial.printf("PASS: wrote %s\n", path);

  file = SD.open(path, FILE_READ);
  if (!file) {
    Serial.printf("FAIL: could not reopen %s\n", path);
    return;
  }

  Serial.println("File contents:");
  while (file.available()) {
    Serial.write(file.read());
  }
  file.close();

  Serial.println("PASS: SD card read/write test complete");
}

void loop() {
  delay(1000);
}
