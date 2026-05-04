#ifndef DISPLAY_H
#define DISPLAY_H

#include <U8g2lib.h>

constexpr int SCREEN_WIDTH = 64;
constexpr int SCREEN_HEIGHT = 48;

// EastRising ER-OLED0.66-1: a 64x48 panel driven by a 128x64 SSD1306 with the
// visible window offset 32 columns into RAM. The "_ER_" constructor applies
// that offset; Adafruit_SSD1306 does not, which is why half the screen was
// noise.
extern U8G2_SSD1306_64X48_ER_F_HW_I2C u8g2;

// sda/scl are passed in because u8g2.begin() calls Wire.begin() internally
// with the ESP32's default pins (21/22), unhooking the MPU6050. We restore
// the configured pins right after.
void displaySetup(int sda, int scl);

// Toggle SSD1306 display-off (power save) mode. RAM is preserved, so the
// previous frame is shown again when waking — no repaint required.
void displaySetPowerSave(bool save);

#endif // DISPLAY_H
