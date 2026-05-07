#include "WifiSetup.h"
#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <string.h>

static WifiSetupState state = WifiSetupState::Idle;
static unsigned long stateEnteredMs = 0;
static char statusMessage[40] = "";
static char currentSsid[33] = "";

struct ScanEntry {
  char    ssid[33];
  uint8_t bssid[6];
  uint8_t channel;
  int8_t  rssi;
};
constexpr int SCAN_MAX = 32;
static ScanEntry scanResults[SCAN_MAX];
static uint8_t   scanCount = 0;

static uint32_t origCpuMhz = 0;

static void bumpCpu() {
  origCpuMhz = getCpuFrequencyMhz();
  if (origCpuMhz < 240) {
    setCpuFrequencyMhz(240);
    Serial.printf("[WIFISETUP] cpu %u -> 240 MHz\n", (unsigned)origCpuMhz);
  }
}
static void restoreCpu() {
  if (origCpuMhz && origCpuMhz < 240) {
    setCpuFrequencyMhz(origCpuMhz);
    Serial.printf("[WIFISETUP] cpu restored to %u MHz\n", (unsigned)origCpuMhz);
  }
  origCpuMhz = 0;
}

static void startScan() {
  bumpCpu();
  WiFi.mode(WIFI_STA);
  // async=true so we can stay responsive; scanComplete() polls below.
  int rc = WiFi.scanNetworks(/*async*/true, /*show_hidden*/false);
  Serial.printf("[WIFISETUP] scanNetworks(async) rc=%d\n", rc);
}

static int cmpRssiDesc(const void* a, const void* b) {
  const ScanEntry* ea = (const ScanEntry*)a;
  const ScanEntry* eb = (const ScanEntry*)b;
  if (ea->rssi == eb->rssi) return 0;
  return (ea->rssi > eb->rssi) ? -1 : 1;
}

static bool sameNet(const ScanEntry& a, const ScanEntry& b) {
  return strncmp(a.ssid, b.ssid, 33) == 0 &&
         memcmp(a.bssid, b.bssid, 6) == 0;
}

static void harvestScan(int n) {
  scanCount = 0;
  for (int i = 0; i < n && scanCount < SCAN_MAX; ++i) {
    String ssid = WiFi.SSID(i);
    if (ssid.length() == 0) continue;  // hidden
    ScanEntry e{};
    strncpy(e.ssid, ssid.c_str(), sizeof(e.ssid) - 1);
    memcpy(e.bssid, WiFi.BSSID(i), 6);
    e.channel = (uint8_t)WiFi.channel(i);
    e.rssi    = (int8_t)WiFi.RSSI(i);
    // De-dup exact (ssid,bssid) match (some drivers report duplicates).
    bool dup = false;
    for (uint8_t j = 0; j < scanCount; ++j) {
      if (sameNet(scanResults[j], e)) { dup = true; break; }
    }
    if (!dup) scanResults[scanCount++] = e;
  }
  qsort(scanResults, scanCount, sizeof(ScanEntry), cmpRssiDesc);
  Serial.printf("[WIFISETUP] scan harvested %u networks\n",
                (unsigned)scanCount);
  WiFi.scanDelete();
}

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
  WiFi.scanDelete();
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  restoreCpu();
  enterState(WifiSetupState::Idle);
}

void wifiSetupTick() {
  if (state == WifiSetupState::Idle) return;

  switch (state) {
    case WifiSetupState::Scanning: {
      // First tick after entering Scanning: kick off async scan.
      static unsigned long scanStartedMs = 0;
      if (scanStartedMs != stateEnteredMs) {
        startScan();
        scanStartedMs = stateEnteredMs;
      }
      int n = WiFi.scanComplete();
      if (n == WIFI_SCAN_RUNNING) {
        // Hard timeout — don't get stuck.
        if (millis() - stateEnteredMs > 10000) {
          Serial.println("[WIFISETUP] scan timeout");
          WiFi.scanDelete();
          strncpy(statusMessage, "scan timeout",
                  sizeof(statusMessage) - 1);
          enterState(WifiSetupState::Failed);
        }
        return;
      }
      if (n < 0 || n == 0) {
        Serial.printf("[WIFISETUP] scan returned %d\n", n);
        WiFi.scanDelete();
        strncpy(statusMessage,
                (n <= 0) ? "scan failed" : "no networks",
                sizeof(statusMessage) - 1);
        enterState(WifiSetupState::Failed);
        return;
      }
      harvestScan(n);
      restoreCpu();
      // Stay in Scanning until Task 8 wires up PickingSsid; for now,
      // log results and bounce to Idle so the device doesn't hang.
      enterState(WifiSetupState::Idle);
      break;
    }
    case WifiSetupState::Failed: {
      if (millis() - stateEnteredMs > 3000) {
        enterState(WifiSetupState::Idle);
      }
      break;
    }
    default:
      break;
  }
}

void wifiSetupHandleButton(int /*button*/) {
  // Per-state button handling is added in Tasks 8 & 13.
}

bool wifiSetupIsActive() { return state != WifiSetupState::Idle; }
WifiSetupState wifiSetupGetState() { return state; }
const char* wifiSetupGetStatusMessage() { return statusMessage; }
const char* wifiSetupGetCurrentSsid() { return currentSsid; }
