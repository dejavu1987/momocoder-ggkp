#include "WifiSetup.h"
#include <Arduino.h>

static WifiSetupState state = WifiSetupState::Idle;
static unsigned long stateEnteredMs = 0;
static char statusMessage[40] = "";
static char currentSsid[33] = "";

static void enterState(WifiSetupState s) {
  Serial.printf("[WIFISETUP] %u -> %u\n",
                (unsigned)state, (unsigned)s);
  state = s;
  stateEnteredMs = millis();
}

void wifiSetupBegin() {
  if (state != WifiSetupState::Idle) {
    Serial.println("[WIFISETUP] begin ignored — already active");
    return;
  }
  statusMessage[0] = 0;
  currentSsid[0] = 0;
  enterState(WifiSetupState::Scanning);
}

void wifiSetupCancel() {
  if (state == WifiSetupState::Idle) return;
  Serial.println("[WIFISETUP] cancel");
  // Per-state teardown will be filled in by later tasks (AP/STA off).
  enterState(WifiSetupState::Idle);
}

void wifiSetupTick() {
  if (state == WifiSetupState::Idle) return;
  // Per-state behavior is added in Tasks 7–12.
}

void wifiSetupHandleButton(int /*button*/) {
  // Per-state button handling is added in Tasks 8 & 13.
}

bool wifiSetupIsActive() { return state != WifiSetupState::Idle; }
WifiSetupState wifiSetupGetState() { return state; }
const char* wifiSetupGetStatusMessage() { return statusMessage; }
const char* wifiSetupGetCurrentSsid() { return currentSsid; }
