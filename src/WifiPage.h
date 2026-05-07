#ifndef WIFIPAGE_H
#define WIFIPAGE_H

#include "ListPicker.h"
#include <stdint.h>

// Item kinds shown on the Wifi page.
enum class WifiItemKind : uint8_t {
  Saved   = 0,  // userId = WifiConfigs slot index
  Add     = 1,  // userId unused — triggers wifiSetupBegin()
  Delete  = 2,  // userId unused — triggers wifiConfigsDeleteActive()
};

// Build/refresh the items array from current WifiConfigs state. Called
// once at boot, and again whenever configs change.
void wifiPageRefresh();

// Button hooks (called from executeAction in Pages.cpp).
void wifiPageOnSlot(uint8_t slot);
void wifiPageOnLeft();
void wifiPageOnRight();
void wifiPageOnConfirm();

// Render the wifi page (called from renderPage in main.cpp).
void wifiPageRender();

// Exposed so main.cpp's DisplayState can include picker state in its
// repaint-on-change check.
struct WifiPageDigest {
  uint16_t pageIdx;
  int8_t   highlightSlot;
  uint16_t activeIdx;
  uint16_t count;
};
WifiPageDigest wifiPageGetDigest();

#endif // WIFIPAGE_H
