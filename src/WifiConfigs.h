#ifndef WIFICONFIGS_H
#define WIFICONFIGS_H

#include <stdint.h>

// Per-location Wi-Fi credentials, persisted in NVS namespace "wifi".
// One slot per known network. Hard cap is UI-cosmetic — bumpable.
constexpr uint8_t WIFI_MAX_CONFIGS = 16;

struct WifiConfig {
  char     ssid[33];     // 32 char SSID + NUL
  char     password[65]; // 64 char WPA2 PSK + NUL
  uint8_t  bssid[6];
  uint8_t  channel;      // 1..13
};
static_assert(sizeof(WifiConfig) <= 128,
              "Bump NVS blob assumption if WifiConfig grows");

// Hydrate the in-RAM array from NVS. Call once in setup().
void wifiConfigsBegin();

uint8_t wifiConfigsCount();
const WifiConfig* wifiConfigsGet(uint8_t idx);   // nullptr if out of range
int8_t  wifiConfigsActiveIdx();                  // -1 if none
const WifiConfig* wifiConfigsGetActive();        // nullptr if none

// Add or, if (ssid,bssid) already exists, overwrite the matching slot.
// Returns the resulting slot index, or -1 if storage is full and no match.
// If `setAsActive` is true the slot is marked active and persisted.
int8_t wifiConfigsAddOrUpdate(const WifiConfig& cfg, bool setAsActive);

// Delete the currently-active slot (shifting later slots down). Sets
// activeConfigIdx = -1. Returns false if no active slot.
bool wifiConfigsDeleteActive();

bool wifiConfigsSetActive(int8_t idx);  // 0..count-1; persists.

#endif // WIFICONFIGS_H
