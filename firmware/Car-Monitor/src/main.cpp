#include <Arduino.h>

const int LED_PIN = 2;   // onboard blue LED

void setup() {
  pinMode(LED_PIN, OUTPUT);
  Serial.begin(115200);
}

void loop() {
  digitalWrite(LED_PIN, HIGH);
  Serial.println("LED on");
  delay(1000);
  digitalWrite(LED_PIN, LOW);
  Serial.println("LED off");
  delay(1000);
}