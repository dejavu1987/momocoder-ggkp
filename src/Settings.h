#ifndef SETTINGS_H
#define SETTINGS_H

#include <stdint.h>

// Persisted user settings (NVS namespace "settings"):
//   - mouse sensitivity   (int,    default 300)
//   - mouse move delay    (int ms, default 5)
//   - SSD1306 contrast    (uint8_t, default 80 == BRIGHTNESS_MID)
//
// Call settingsBegin() in setup() BEFORE displaySetup() so the display comes
// up at the user's saved brightness. Call settingsSave() after any persisted
// value changes (Pages::executeAction does this for AdjustSens/AdjustDelay,
// Display::displayCycleBrightness for brightness).
void settingsBegin();
void settingsSave();

// Brightness lives in Display.cpp; Settings just owns the persisted byte.
uint8_t settingsGetBrightness();
void    settingsSetBrightness(uint8_t v);

#endif // SETTINGS_H
