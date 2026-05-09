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

// Picker view backing the Wifi page. Exposed so executeAction() can drive
// it through the generic listPickerOn{Slot,Left,Right} primitives without
// per-page wrapper functions.
ListPickerView* wifiPageGetView();

// Confirm dispatches by item kind (Saved/Add/Delete) so it stays page-
// specific — listPickerOnOk only returns the picked index.
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
