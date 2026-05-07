#include "Display.h"
#include <Arduino.h>
#include <Wire.h>

U8G2_SSD1306_64X48_ER_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

static const uint8_t BRIGHTNESS_LOW  = 20;
static const uint8_t BRIGHTNESS_MID  = 80;
static const uint8_t BRIGHTNESS_HIGH = 200;
static uint8_t currentBrightness = BRIGHTNESS_MID;

void displaySetup(int sda, int scl) {
  u8g2.setBusClock(400000);
  u8g2.begin();
  Wire.setPins(sda, scl);
  u8g2.setContrast(currentBrightness);

  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.drawStr(0, 10, "Momocoder");
  u8g2.drawStr(0, 22, "GGKP");
  u8g2.sendBuffer();
}

void displaySetPowerSave(bool save) {
  u8g2.setPowerSave(save ? 1 : 0);
}

void displaySetBrightness(uint8_t v) {
  currentBrightness = v;
  u8g2.setContrast(v);
}

void displayCycleBrightness() {
  uint8_t next;
  if (currentBrightness <= BRIGHTNESS_LOW + 5)        next = BRIGHTNESS_MID;
  else if (currentBrightness <= BRIGHTNESS_MID + 5)   next = BRIGHTNESS_HIGH;
  else                                                next = BRIGHTNESS_LOW;
  displaySetBrightness(next);
  Serial.printf("[OLED] brightness -> %u\n", (unsigned)next);
}
