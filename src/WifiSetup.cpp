#include "WifiSetup.h"
#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <string.h>
#include "ListPicker.h"
#include "Keypad.h"
#include "Display.h"
#include <WiFiClient.h>
#include <WiFiServer.h>

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

// The scan-pick view reuses the same ListPicker primitive used by the
// Wifi page itself. Items are built directly from scanResults[].
static ListPickerItem scanItems[SCAN_MAX + 1];   // +1 for "Cancel" row
static char           scanLabels[SCAN_MAX][16];
static ListPickerView scanView;
static int8_t         pickedScanIdx = -1;        // index into scanResults

enum class ScanItemKind : uint8_t { Net = 0, Cancel = 1 };

static const char* AP_SSID = "GGKP-Setup";
static const unsigned long SETUP_AP_TIMEOUT_MS = 90UL * 1000UL;
static WiFiServer httpServer(80);
static bool       httpStarted = false;

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

static void buildScanItems() {
  uint16_t n = 0;
  for (uint8_t i = 0; i < scanCount; ++i) {
    strncpy(scanLabels[i], scanResults[i].ssid, sizeof(scanLabels[i]) - 1);
    scanLabels[i][sizeof(scanLabels[i]) - 1] = 0;
    scanItems[n++] = {scanLabels[i], (uint8_t)ScanItemKind::Net, i};
  }
  scanItems[n++] = {"Cancel", (uint8_t)ScanItemKind::Cancel, 0};
  listPickerInit(scanView, scanItems, n, LIST_PICKER_NO_ACTIVE);
}

static void startAp() {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_AP);
  bool ok = WiFi.softAP(AP_SSID, /*password*/nullptr);
  Serial.printf("[WIFISETUP] softAP \"%s\" %s, IP=%s\n", AP_SSID,
                ok ? "up" : "FAILED",
                WiFi.softAPIP().toString().c_str());
  httpServer.begin();
  httpStarted = true;
}

static void stopAp() {
  if (httpStarted) {
    httpServer.stop();
    httpStarted = false;
  }
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  Serial.println("[WIFISETUP] AP stopped");
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
  if (httpStarted) {
    httpServer.stop();
    httpStarted = false;
  }
  WiFi.softAPdisconnect(true);
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
      buildScanItems();
      enterState(WifiSetupState::PickingSsid);
      break;
    }
    case WifiSetupState::WaitingForClient: {
      if (WiFi.softAPgetStationNum() > 0) {
        Serial.println("[WIFISETUP] client associated to setup AP");
        enterState(WifiSetupState::WaitingForSubmit);
      } else if (millis() - stateEnteredMs > SETUP_AP_TIMEOUT_MS) {
        strncpy(statusMessage, "setup timeout", sizeof(statusMessage) - 1);
        stopAp();
        enterState(WifiSetupState::Failed);
      }
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

void wifiSetupHandleButton(int button) {
  if (state != WifiSetupState::PickingSsid) {
    // BTN_A long-press is handled by polling in tick() (Task 13);
    // taps in non-Idle non-Picking states are ignored.
    return;
  }
  switch (button) {
    case BTN_A:  listPickerOnSlot(scanView, 0); break;
    case BTN_B:  listPickerOnSlot(scanView, 1); break;
    case BTN_C:  listPickerOnSlot(scanView, 2); break;
    case BTN_D:  listPickerOnSlot(scanView, 3); break;
    case BTN_LT: listPickerOnLeft(scanView);   break;
    case BTN_RT: listPickerOnRight(scanView);  break;
    case BTN_OK: {
      int32_t idx = listPickerOnOk(scanView);
      if (idx < 0) return;
      const ListPickerItem& it = scanItems[idx];
      if ((ScanItemKind)it.kind == ScanItemKind::Cancel) {
        wifiSetupCancel();
        return;
      }
      pickedScanIdx = (int8_t)it.userId;
      strncpy(currentSsid, scanResults[pickedScanIdx].ssid,
              sizeof(currentSsid) - 1);
      Serial.printf("[WIFISETUP] picked SSID \"%s\"\n", currentSsid);
      startAp();
      enterState(WifiSetupState::WaitingForClient);
      break;
    }
    default: break;
  }
}

bool wifiSetupIsActive() { return state != WifiSetupState::Idle; }
WifiSetupState wifiSetupGetState() { return state; }
const char* wifiSetupGetStatusMessage() { return statusMessage; }
const char* wifiSetupGetCurrentSsid() { return currentSsid; }

void wifiSetupRender() {
  if (state == WifiSetupState::PickingSsid) {
    listPickerRender(scanView);
    return;
  }
  // Per-state OLED screens (Scanning/WaitingForClient/etc.) are added in
  // Task 12. Until then, leave the previous frame on the screen.
}

WifiSetupDigest wifiSetupGetDigest() {
  WifiSetupDigest d{};
  d.state = (uint8_t)state;
  d.pickerPage = scanView.pageIdx;
  d.highlight  = scanView.highlightSlot;
  return d;
}
