
#include <U8g2lib.h>

#ifdef U8X8_HAVE_HW_I2C
#include <Wire.h>
#endif

#define RT_BTN 0

U8G2_SSD1306_64X48_ER_1_SW_I2C
u8g2(U8G2_R0, 1, 2, 42);

int debounce = 0;

void setup(void) {
  Serial.begin(115200);
  u8g2.begin();
}

int  page = 1;
char buff[20];

void printPage(int page) {
  u8g2.drawGlyph(0, 16, (page - 1) * 9 + 64);
  u8g2.drawGlyph(0, 32, (page - 1) * 9 + 64 + 1);
  u8g2.drawGlyph(0, 48, (page - 1) * 9 + 64 + 2);
  u8g2.drawGlyph(21, 16, (page - 1) * 9 + 64 + 3);
  u8g2.drawGlyph(21, 32, (page - 1) * 9 + 64 + 4);
  u8g2.drawGlyph(21, 48, (page - 1) * 9 + 64 + 5);
  u8g2.drawGlyph(42, 16, (page - 1) * 9 + 64 + 6);
  u8g2.drawGlyph(42, 32, (page - 1) * 9 + 64 + 7);
  u8g2.drawGlyph(42, 48, (page - 1) * 9 + 64 + 8);
}

void loop(void) {
  if (digitalRead(RT_BTN) == 0) {
    if (debounce == 0) {
      page++;
      debounce = 1;
    }
  } else {
    debounce = 0;
  }

  u8g2.firstPage();

  do {
    // u8g2.setFont(u8g2_font_tiny_simon_tr);
    // u8g2.drawStr(0, 8, "Res:");
    // u8g2.drawStr(0, 24, "Temp:");
    // u8g2.drawStr(0, 40, "PWM:");

    // u8g2.setFont(u8g2_font_6x10_tr);

    // u8g2.drawStr(0, 32, "Ball");
    // u8g2.drawStr(0, 48, "Cat");

    u8g2.setFont(u8g2_font_open_iconic_all_2x_t);
    printPage(page);

  } while (u8g2.nextPage());
}
