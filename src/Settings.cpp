#include "Settings.h"
#include "globals.h"
#include <Arduino.h>
#include <Preferences.h>

static const char *NS = "settings";
static Preferences prefs;
static uint8_t savedBrightness = 80;  // matches Display.cpp BRIGHTNESS_MID

// Defined here (declared extern in globals.h) so a single module owns the
// in-RAM copy of every persisted setting.
int mouseSensitivity = 300;
int mouseMoveDelay   = 5;

void settingsBegin() {
  prefs.begin(NS, true);
  mouseSensitivity = prefs.getInt("sens",   300);
  mouseMoveDelay   = prefs.getInt("delay",  5);
  savedBrightness  = prefs.getUChar("bright", 80);
  prefs.end();
  Serial.printf("[SETTINGS] loaded sens=%d delay=%d bright=%u\n",
                mouseSensitivity, mouseMoveDelay, (unsigned)savedBrightness);
}

void settingsSave() {
  prefs.begin(NS, false);
  prefs.putInt("sens",   mouseSensitivity);
  prefs.putInt("delay",  mouseMoveDelay);
  prefs.putUChar("bright", savedBrightness);
  prefs.end();
}

uint8_t settingsGetBrightness()        { return savedBrightness; }
void    settingsSetBrightness(uint8_t v) { savedBrightness = v; }
