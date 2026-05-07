#include "WifiConfigs.h"
#include <Arduino.h>
#include <Preferences.h>
#include <string.h>

static const char* NS = "wifi";
static Preferences prefs;

static WifiConfig configs[WIFI_MAX_CONFIGS];
static uint8_t configCount = 0;
static int8_t activeConfigIdx = -1;

static void slotKey(uint8_t idx, char* out, size_t n) {
  // NVS key length cap is 15 chars; "cfg.NN" fits comfortably.
  snprintf(out, n, "cfg.%u", idx);
}

void wifiConfigsBegin() {
  prefs.begin(NS, true);  // read-only first pass
  configCount = prefs.getUChar("count", 0);
  activeConfigIdx = prefs.getChar("active", -1);
  if (configCount > WIFI_MAX_CONFIGS) {
    Serial.printf("[WIFICFG] count %u > cap; resetting\n", configCount);
    configCount = 0;
    activeConfigIdx = -1;
  }
  for (uint8_t i = 0; i < configCount; ++i) {
    char key[12];
    slotKey(i, key, sizeof(key));
    size_t n = prefs.getBytes(key, &configs[i], sizeof(WifiConfig));
    if (n != sizeof(WifiConfig)) {
      Serial.printf("[WIFICFG] slot %u corrupt (read %u); resetting all\n",
                    (unsigned)i, (unsigned)n);
      configCount = 0;
      activeConfigIdx = -1;
      break;
    }
    configs[i].ssid[32] = 0;       // defense-in-depth NUL terminators
    configs[i].password[64] = 0;
  }
  if (activeConfigIdx >= (int8_t)configCount) activeConfigIdx = -1;
  prefs.end();
  Serial.printf("[WIFICFG] loaded %u configs (active=%d)\n",
                (unsigned)configCount, (int)activeConfigIdx);
}

uint8_t wifiConfigsCount() { return configCount; }

const WifiConfig* wifiConfigsGet(uint8_t idx) {
  if (idx >= configCount) return nullptr;
  return &configs[idx];
}

int8_t wifiConfigsActiveIdx() { return activeConfigIdx; }

const WifiConfig* wifiConfigsGetActive() {
  if (activeConfigIdx < 0) return nullptr;
  return &configs[activeConfigIdx];
}

int8_t wifiConfigsAddOrUpdate(const WifiConfig& cfg, bool setAsActive) {
  // Match by (ssid, bssid).
  int8_t targetIdx = -1;
  for (uint8_t i = 0; i < configCount; ++i) {
    if (strncmp(configs[i].ssid, cfg.ssid, 33) == 0 &&
        memcmp(configs[i].bssid, cfg.bssid, 6) == 0) {
      targetIdx = (int8_t)i;
      break;
    }
  }
  if (targetIdx < 0) {
    if (configCount >= WIFI_MAX_CONFIGS) {
      Serial.println("[WIFICFG] storage full");
      return -1;
    }
    targetIdx = (int8_t)configCount;
  }

  configs[targetIdx] = cfg;
  configs[targetIdx].ssid[32] = 0;
  configs[targetIdx].password[64] = 0;
  if (targetIdx == (int8_t)configCount) configCount++;
  if (setAsActive) activeConfigIdx = targetIdx;

  prefs.begin(NS, false);
  char key[12];
  slotKey((uint8_t)targetIdx, key, sizeof(key));
  prefs.putBytes(key, &configs[targetIdx], sizeof(WifiConfig));
  prefs.putUChar("count", configCount);
  prefs.putChar("active", activeConfigIdx);
  prefs.end();

  Serial.printf("[WIFICFG] %s slot %d (active=%d)\n",
                (targetIdx == (int8_t)configCount - 1) ? "added" : "updated",
                (int)targetIdx, (int)activeConfigIdx);
  return targetIdx;
}

bool wifiConfigsDeleteActive() {
  if (activeConfigIdx < 0) return false;
  uint8_t deleted = (uint8_t)activeConfigIdx;
  // Shift later slots down.
  for (uint8_t i = deleted; i + 1 < configCount; ++i) {
    configs[i] = configs[i + 1];
  }
  configCount--;
  activeConfigIdx = -1;

  prefs.begin(NS, false);
  // Re-persist the shifted slots.
  for (uint8_t i = deleted; i < configCount; ++i) {
    char key[12];
    slotKey(i, key, sizeof(key));
    prefs.putBytes(key, &configs[i], sizeof(WifiConfig));
  }
  // Remove the now-vacant tail key.
  char tailKey[12];
  slotKey(configCount, tailKey, sizeof(tailKey));
  prefs.remove(tailKey);
  prefs.putUChar("count", configCount);
  prefs.putChar("active", activeConfigIdx);
  prefs.end();

  Serial.printf("[WIFICFG] deleted slot %u (count=%u)\n",
                (unsigned)deleted, (unsigned)configCount);
  return true;
}

bool wifiConfigsSetActive(int8_t idx) {
  if (idx < 0 || idx >= (int8_t)configCount) return false;
  activeConfigIdx = idx;
  prefs.begin(NS, false);
  prefs.putChar("active", activeConfigIdx);
  prefs.end();
  Serial.printf("[WIFICFG] active=%d\n", (int)activeConfigIdx);
  return true;
}
