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

#endif // WIFISETUP_H
