#ifndef WIFISETUP_H
#define WIFISETUP_H

#include <stdint.h>

enum class WifiSetupState : uint8_t {
  Idle = 0,
  Scanning,
  PickingSsid,
  WaitingForClient,
  WaitingForSubmit,
  Saving,
  Done,
  Failed,
};

// Begin the captive-portal flow (Idle -> Scanning).
void wifiSetupBegin();

// Forcibly return to Idle, tearing down any AP/STA. Safe to call from any
// state (including Idle).
void wifiSetupCancel();

// Tick the state machine. Call every loop() iteration.
void wifiSetupTick();

// Route a button press while a setup flow is active. The main loop should
// call this (instead of handleButtonPress) when wifiSetupIsActive() is true.
void wifiSetupHandleButton(int button);

bool wifiSetupIsActive();
WifiSetupState wifiSetupGetState();

// Status text for the OLED (e.g. "wrong password", "scan failed"). Empty
// string when irrelevant.
const char* wifiSetupGetStatusMessage();
// SSID currently being added (visible on Saving/Done screens).
const char* wifiSetupGetCurrentSsid();

// Render the OLED for the current setup state. Caller is main.cpp's
// printPage() when wifiSetupIsActive() is true.
void wifiSetupRender();

// Snapshot used by main.cpp's DisplayState repaint-on-change check.
struct WifiSetupDigest {
  uint8_t  state;
  uint16_t pickerPage;
  int8_t   highlight;
  uint8_t  showQr;
};
WifiSetupDigest wifiSetupGetDigest();

#endif // WIFISETUP_H
