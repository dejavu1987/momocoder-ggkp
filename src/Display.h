#ifndef DISPLAY_H
#define DISPLAY_H

#include <U8g2lib.h>
#include <Wire.h>

#define SCREEN_WIDTH 64
#define SCREEN_HEIGHT 48

// EastRising ER-OLED0.66-1: a 64x48 panel driven by a 128x64 SSD1306 with the
// visible window offset 32 columns into RAM. The "_ER_" constructor applies
// that offset; Adafruit_SSD1306 does not, which is why half the screen was
// noise.
U8G2_SSD1306_64X48_ER_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

// sda/scl are passed in because u8g2.begin() calls Wire.begin() internally
// with the ESP32's default pins (21/22), unhooking the MPU6050. We restore
// the configured pins right after.
inline void displaySetup(int sda, int scl) {
  u8g2.setBusClock(400000);
  u8g2.begin();
  Wire.setPins(sda, scl);

  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.drawStr(0, 10, "Momocoder");
  u8g2.drawStr(0, 22, "GGKP");
  u8g2.sendBuffer();
}

#endif // DISPLAY_H
