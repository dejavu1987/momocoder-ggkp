#include "Display.h"
#include <Wire.h>

U8G2_SSD1306_64X48_ER_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

void displaySetup(int sda, int scl) {
  u8g2.setBusClock(400000);
  u8g2.begin();
  Wire.setPins(sda, scl);

  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.drawStr(0, 10, "Momocoder");
  u8g2.drawStr(0, 22, "GGKP");
  u8g2.sendBuffer();
}

void displaySetPowerSave(bool save) {
  u8g2.setPowerSave(save ? 1 : 0);
}
