# Multi-Wi-Fi Configs Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add per-location Wi-Fi config slots to the GGKP, swappable on-device via a generalizable list-picker UI, with on-device captive-portal provisioning of new networks.

**Architecture:** Four new modules (`ListPicker`, `WifiConfigs`, `WifiPage`, `WifiSetup`) plus a new `Page::Wifi` slot in the existing page enum. Storage uses Arduino `Preferences` (NVS) under namespace `"wifi"`. Captive portal runs as a transient sub-state — not a page — so the keypad's existing FALLING-edge ISR + `executeAction()` switch dispatch model is preserved unchanged. Long-press cancel uses a poll in the setup tick (no IRQ changes).

**Tech Stack:** PlatformIO / Arduino-ESP32, U8g2, Preferences (NVS), `WiFi.h` + `WiFiClientSecure` + `WiFiServer` (all bundled with the framework — no new lib_deps), NimBLE-Arduino (existing).

**Reference spec:** `docs/superpowers/specs/2026-05-07-multi-wifi-configs-design.md`

**Verification model:** This codebase has no unit-test framework. Each task ends with `pio run` to confirm clean build, optional `pio run -t upload -t monitor` smoke check on hardware where the user has it, and a `git commit`. The user is the one with the hardware and will run flash/monitor steps.

---

## File Structure

**New files:**
- `src/ListPicker.h` / `src/ListPicker.cpp` — generic 4-row list-picker primitive (struct + free functions). No Wi-Fi specifics.
- `src/WifiConfigs.h` / `src/WifiConfigs.cpp` — `WifiConfig` struct + NVS-backed CRUD on slots + active-index tracking. No UI specifics.
- `src/WifiPage.h` / `src/WifiPage.cpp` — glue layer. Owns the wifi-page `ListPickerView` and items array; maps confirm-on-`kind` to `setActive` / enter-scan / delete-active.
- `src/WifiSetup.h` / `src/WifiSetup.cpp` — captive-portal state machine (Scanning → PickingSsid → WaitingForClient → WaitingForSubmit → Saving → Done/Failed → Idle), AP startup, tiny synchronous HTTP server, validation step, long-press-cancel polling.

**Modified files:**
- `src/globals.h` — `NUM_PAGES` 4 → 5; insert `Wifi = 3` ahead of `Settings = 4`.
- `src/Pages.h` — new `ActionKind` values (`ListPickerSlot`, `ListPickerLeft`, `ListPickerRight`, `ListPickerConfirm`); add `uint8_t slot` to the `Action::p` union.
- `src/Pages.cpp` — add `wifiBindings[]`, extend `pageDefs[]`, extend `executeAction()` switch with picker cases.
- `src/main.cpp` — `renderPage()` branches to wifi-page render and to setup-state render; extend `DisplayState` with picker + setup fields; tick `wifiSetupTick()` and route button presses to `wifiSetupHandleButton()` while non-Idle; suppress OLED idle / deep sleep while non-Idle; call `wifiConfigsBegin()` in `setup()`.
- `src/WifiRemote.cpp` — drop the `WIFI_SSID/WIFI_PASSWORD/WIFI_CHANNEL/WIFI_BSSID` hardcoded block; read SSID/password/channel/BSSID from `wifiConfigsGetActive()`.
- `wifi_secrets.ini` — drop `WIFI_SSID`/`WIFI_PASSWORD`/`WIFI_CHANNEL` build_flags; keep only `DEVICE_TOKEN`.
- `CLAUDE.md` — refresh the Wi-Fi remote section, add the Wifi page section, list the new modules.

---

## Task 1: ListPicker primitive

**Files:**
- Create: `src/ListPicker.h`
- Create: `src/ListPicker.cpp`

- [ ] **Step 1: Create `src/ListPicker.h`**

```cpp
#ifndef LISTPICKER_H
#define LISTPICKER_H

#include <stdint.h>

// Reusable 4-row list selector for the 64x48 OLED. The wifi page is the
// first consumer; future selectors (themes, keymaps, etc.) can use the
// same primitive by handing it an items array + on-confirm callback.
//
// Two-step interaction model (matches the user's spec):
//   1) Press A/B/C/D -> highlights that row (inverted) but does not commit.
//   2) Press OK     -> commits the highlighted row; caller dispatches by
//                      the item's `kind` field.
// LT/RT paginate (4 items per page); UP/DN are NOT consumed (caller still
// runs page-nav). Pressing on an empty slot is a silent no-op.

constexpr int LIST_PICKER_ROWS = 4;
constexpr uint16_t LIST_PICKER_NO_ACTIVE = 0xFFFF;

struct ListPickerItem {
  const char* label;   // shown after "X. "  e.g. "Home Wifi", "+ Add new..."
  uint8_t     kind;    // domain-specific tag inspected by caller's onConfirm
  uint16_t    userId;  // domain-specific id (e.g. WifiConfigs slot index)
};

struct ListPickerView {
  const ListPickerItem* items;
  uint16_t count;
  uint16_t pageIdx;        // 0..ceil(count/4)-1
  int8_t   highlightSlot;  // -1 = nothing pressed yet, 0..3 = inverted row
  uint16_t activeIdx;      // global item idx, drawn with marker dot;
                           // LIST_PICKER_NO_ACTIVE if none.
};

void listPickerInit(ListPickerView& v, const ListPickerItem* items,
                    uint16_t count, uint16_t activeIdx);

uint16_t listPickerPageCount(const ListPickerView& v);

// Mutators — call on the corresponding button press.
void listPickerOnSlot(ListPickerView& v, uint8_t slot);  // 0..3
void listPickerOnLeft(ListPickerView& v);
void listPickerOnRight(ListPickerView& v);

// Commit the current highlight. Returns global item index, or -1 if no
// highlight or empty slot. Does NOT update activeIdx — caller decides
// (some kinds, like "Add", don't change the persisted active selection).
int32_t listPickerOnOk(const ListPickerView& v);

// Render the picker into the U8g2 buffer and send. Owns the whole 64x48
// screen — caller should NOT clear/send around this.
void listPickerRender(const ListPickerView& v);

#endif // LISTPICKER_H
```

- [ ] **Step 2: Create `src/ListPicker.cpp`**

```cpp
#include "ListPicker.h"
#include "Display.h"
#include <stdio.h>

static const char ROW_LETTER[LIST_PICKER_ROWS] = {'A', 'B', 'C', 'D'};

void listPickerInit(ListPickerView& v, const ListPickerItem* items,
                    uint16_t count, uint16_t activeIdx) {
  v.items = items;
  v.count = count;
  v.pageIdx = 0;
  v.highlightSlot = -1;
  v.activeIdx = activeIdx;
  // Auto-jump page so the active item is visible on first render.
  if (activeIdx != LIST_PICKER_NO_ACTIVE && activeIdx < count) {
    v.pageIdx = activeIdx / LIST_PICKER_ROWS;
  }
}

uint16_t listPickerPageCount(const ListPickerView& v) {
  if (v.count == 0) return 1;
  return (v.count + LIST_PICKER_ROWS - 1) / LIST_PICKER_ROWS;
}

void listPickerOnSlot(ListPickerView& v, uint8_t slot) {
  if (slot >= LIST_PICKER_ROWS) return;
  uint32_t globalIdx = (uint32_t)v.pageIdx * LIST_PICKER_ROWS + slot;
  if (globalIdx >= v.count) return;  // pressed empty slot — no-op
  v.highlightSlot = (int8_t)slot;
}

void listPickerOnLeft(ListPickerView& v) {
  if (v.pageIdx == 0) return;
  v.pageIdx--;
  v.highlightSlot = -1;
}

void listPickerOnRight(ListPickerView& v) {
  if (v.pageIdx + 1 >= listPickerPageCount(v)) return;
  v.pageIdx++;
  v.highlightSlot = -1;
}

int32_t listPickerOnOk(const ListPickerView& v) {
  if (v.highlightSlot < 0) return -1;
  uint32_t globalIdx = (uint32_t)v.pageIdx * LIST_PICKER_ROWS + v.highlightSlot;
  if (globalIdx >= v.count) return -1;
  return (int32_t)globalIdx;
}

void listPickerRender(const ListPickerView& v) {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tr);

  for (int slot = 0; slot < LIST_PICKER_ROWS; ++slot) {
    uint32_t globalIdx = (uint32_t)v.pageIdx * LIST_PICKER_ROWS + slot;
    if (globalIdx >= v.count) break;

    const int rowY = slot * 12;        // 0, 12, 24, 36 — each row is 12 px tall
    const int baseline = rowY + 9;     // ~bottom of 6x10 ascender row
    const bool highlighted = (v.highlightSlot == slot);
    const bool active = (globalIdx == v.activeIdx);

    if (highlighted) {
      u8g2.setDrawColor(1);
      u8g2.drawBox(0, rowY, SCREEN_WIDTH, 12);
      u8g2.setDrawColor(0);
    }

    char buf[24];
    snprintf(buf, sizeof(buf), "%c. %s", ROW_LETTER[slot],
             v.items[globalIdx].label ? v.items[globalIdx].label : "");
    u8g2.drawStr(0, baseline, buf);

    if (active) {
      u8g2.drawDisc(SCREEN_WIDTH - 3, rowY + 6, 1);
    }

    if (highlighted) u8g2.setDrawColor(1);
  }

  uint16_t pageCount = listPickerPageCount(v);
  if (pageCount > 1) {
    u8g2.setFont(u8g2_font_4x6_tr);
    char pbuf[8];
    snprintf(pbuf, sizeof(pbuf), "%u/%u",
             (unsigned)(v.pageIdx + 1), (unsigned)pageCount);
    int pw = u8g2.getStrWidth(pbuf);
    u8g2.drawStr(SCREEN_WIDTH - pw, SCREEN_HEIGHT - 1, pbuf);
  }

  u8g2.sendBuffer();
}
```

- [ ] **Step 3: Build to verify it compiles standalone**

Run: `pio run`
Expected: build succeeds. `ListPicker.{h,cpp}` is included in the source tree but not yet referenced by anything else, so this only validates that the new file compiles cleanly against U8g2 + Display.h.

- [ ] **Step 4: Commit**

```bash
git add src/ListPicker.h src/ListPicker.cpp
git commit -m "Add generic 4-row list-picker primitive"
```

---

## Task 2: WifiConfigs storage

**Files:**
- Create: `src/WifiConfigs.h`
- Create: `src/WifiConfigs.cpp`

- [ ] **Step 1: Create `src/WifiConfigs.h`**

```cpp
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
```

- [ ] **Step 2: Create `src/WifiConfigs.cpp`**

```cpp
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
```

- [ ] **Step 3: Wire `wifiConfigsBegin()` into `setup()` so the load runs at boot**

In `src/main.cpp`, add the include alongside the existing module includes:
```cpp
#include "WifiConfigs.h"
```
and add the call inside `setup()` immediately before `displaySetup(I2C_SDA, I2C_SCL);`:
```cpp
  wifiConfigsBegin();
```

- [ ] **Step 4: Build to verify**

Run: `pio run`
Expected: build succeeds.

- [ ] **Step 5: Optional hardware smoke check**

Run: `pio run -t upload -t monitor`
Expected serial line on boot: `[WIFICFG] loaded 0 configs (active=-1)` (NVS namespace empty).

- [ ] **Step 6: Commit**

```bash
git add src/WifiConfigs.h src/WifiConfigs.cpp src/main.cpp
git commit -m "Add WifiConfigs NVS-backed slot storage"
```

---

## Task 3: Page::Wifi enum slot + new ActionKinds + dispatch hooks

**Files:**
- Modify: `src/globals.h`
- Modify: `src/Pages.h`
- Modify: `src/Pages.cpp`

- [ ] **Step 1: Bump `NUM_PAGES` and insert `Wifi` ahead of `Settings` in `src/globals.h`**

Replace:
```cpp
constexpr int NUM_PAGES = 4;

enum class Page : int {
  Mouse = 0,    // air mouse active
  Media = 1,    // keyboard / media keys
  Remote = 2,   // wifi HTTP remote — connect-on-press to momoggkp.vercel.app
  Settings = 3, // air mouse + sensitivity/delay tuning + BLE re-pairing
};
```
With:
```cpp
constexpr int NUM_PAGES = 5;

enum class Page : int {
  Mouse = 0,    // air mouse active
  Media = 1,    // keyboard / media keys
  Remote = 2,   // wifi HTTP remote — connect-on-press to momoggkp.vercel.app
  Wifi = 3,     // saved Wi-Fi configs: pick which one Remote uses
  Settings = 4, // air mouse + sensitivity/delay tuning + BLE re-pairing
};
```

- [ ] **Step 2: Add new ActionKinds and a `slot` field to the `Action::p` union in `src/Pages.h`**

In the `enum class ActionKind`, add immediately after `CycleBrightness,`:
```cpp
  ListPickerSlot,    // p.slot = 0..3 — A/B/C/D row select on a list page
  ListPickerLeft,    // LT — page back in current page's list-picker
  ListPickerRight,   // RT — page forward
  ListPickerConfirm, // OK — commit highlighted row to current page's onConfirm
```

In the `Action::p` union, add after `const char* urlPart;`:
```cpp
    uint8_t               slot;     // ListPickerSlot row 0..3
```

- [ ] **Step 3: Add `wifiBindings[]`, extend `pageDefs[]`, and add picker dispatch cases to `src/Pages.cpp`**

At the top of `Pages.cpp`, add the include:
```cpp
#include "WifiPage.h"   // forward declarations created in Task 4
```

Insert this binding table after the `settingsBindings[]` array and before the `pageDefs[]` definition:
```cpp
// Page::Wifi — saved Wi-Fi configs as a list-picker. The page itself
// renders the list (see renderPage in main.cpp); these bindings only
// route button presses through the picker's three-action vocabulary
// (Slot, Left/Right paginate, Confirm) plus the universal UP/DN nav.
//   A : row 0 highlight   UP: nav-prev   B : row 1 highlight
//   LT: page back         OK: confirm    RT: page forward
//   C : row 2 highlight   DN: nav-next   D : row 3 highlight
static const Binding wifiBindings[] = {
  {BTN_A,  0, {ActionKind::ListPickerSlot,    {.slot = 0}}},
  {BTN_UP, 0, {ActionKind::NavPrev,           {}}},
  {BTN_B,  0, {ActionKind::ListPickerSlot,    {.slot = 1}}},
  {BTN_LT, 0, {ActionKind::ListPickerLeft,    {}}},
  {BTN_OK, 0, {ActionKind::ListPickerConfirm, {}}},
  {BTN_RT, 0, {ActionKind::ListPickerRight,   {}}},
  {BTN_C,  0, {ActionKind::ListPickerSlot,    {.slot = 2}}},
  {BTN_DN, 0, {ActionKind::NavNext,           {}}},
  {BTN_D,  0, {ActionKind::ListPickerSlot,    {.slot = 3}}},
};
```

Replace the existing `pageDefs[]` definition with:
```cpp
const PageDef pageDefs[NUM_PAGES] = {
  {Page::Mouse,    mouseBindings,    9},
  {Page::Media,    mediaBindings,    9},
  {Page::Remote,   remoteBindings,   9},
  {Page::Wifi,     wifiBindings,     9},
  {Page::Settings, settingsBindings, 9},
};
```

In `executeAction()`, add four new cases at the end of the switch, immediately before the closing `}`:
```cpp
  case ActionKind::ListPickerSlot:
    switch (currentPage) {
      case Page::Wifi: wifiPageOnSlot(a.p.slot); break;
      default: break;
    }
    break;
  case ActionKind::ListPickerLeft:
    switch (currentPage) {
      case Page::Wifi: wifiPageOnLeft(); break;
      default: break;
    }
    break;
  case ActionKind::ListPickerRight:
    switch (currentPage) {
      case Page::Wifi: wifiPageOnRight(); break;
      default: break;
    }
    break;
  case ActionKind::ListPickerConfirm:
    switch (currentPage) {
      case Page::Wifi: wifiPageOnConfirm(); break;
      default: break;
    }
    break;
```

- [ ] **Step 4: Build (will fail until Task 4 lands `WifiPage.h`)**

Run: `pio run`
Expected: build fails with "WifiPage.h: No such file or directory" — this is intentional. Move directly to Task 4.

- [ ] **Step 5: Do NOT commit yet**

This task's changes only compile after Task 4 is in place. Stage them but defer the commit until after Task 4 builds clean.

---

## Task 4: WifiPage glue layer

**Files:**
- Create: `src/WifiPage.h`
- Create: `src/WifiPage.cpp`
- Modify: `src/main.cpp` (renderPage branch + DisplayState extension)

- [ ] **Step 1: Create `src/WifiPage.h`**

```cpp
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
```

- [ ] **Step 2: Create `src/WifiPage.cpp`**

```cpp
#include "WifiPage.h"
#include "WifiConfigs.h"
#include "WifiSetup.h"     // forward — full impl in Task 6
#include <Arduino.h>
#include <stdio.h>
#include <string.h>

// Item layout (when configs exist):
//   slot 0..N-1  -> Saved       (label = SSID truncated)
//   slot N       -> Add         (label = "+ Add new...")
//   slot N+1     -> Delete      (label = "Delete current") -- only if active
//
// When configs is empty: just one Add row.
static ListPickerItem items[WIFI_MAX_CONFIGS + 2];
static char           labels[WIFI_MAX_CONFIGS][16];  // SSID truncations
static uint16_t       itemCount = 0;
static ListPickerView view;

static void buildItems() {
  itemCount = 0;
  uint8_t n = wifiConfigsCount();
  int8_t  active = wifiConfigsActiveIdx();
  for (uint8_t i = 0; i < n; ++i) {
    const WifiConfig* c = wifiConfigsGet(i);
    if (!c) continue;
    // Truncate SSID to fit list-picker label (~12 chars; the row already
    // costs 3 for "X. ").
    strncpy(labels[i], c->ssid, sizeof(labels[i]) - 1);
    labels[i][sizeof(labels[i]) - 1] = 0;
    items[itemCount++] = {labels[i], (uint8_t)WifiItemKind::Saved, i};
  }
  items[itemCount++] = {"+ Add new...", (uint8_t)WifiItemKind::Add, 0};
  if (active >= 0) {
    items[itemCount++] = {"Delete current", (uint8_t)WifiItemKind::Delete, 0};
  }
}

void wifiPageRefresh() {
  buildItems();
  uint16_t activeIdx = LIST_PICKER_NO_ACTIVE;
  int8_t a = wifiConfigsActiveIdx();
  if (a >= 0) activeIdx = (uint16_t)a;  // saved slots come first, so idx = a
  listPickerInit(view, items, itemCount, activeIdx);
}

void wifiPageOnSlot(uint8_t slot)  { listPickerOnSlot(view, slot); }
void wifiPageOnLeft()              { listPickerOnLeft(view); }
void wifiPageOnRight()             { listPickerOnRight(view); }

void wifiPageOnConfirm() {
  int32_t idx = listPickerOnOk(view);
  if (idx < 0) return;
  const ListPickerItem& it = items[idx];
  switch ((WifiItemKind)it.kind) {
    case WifiItemKind::Saved: {
      wifiConfigsSetActive((int8_t)it.userId);
      wifiPageRefresh();
      break;
    }
    case WifiItemKind::Add: {
      wifiSetupBegin();
      // Page will re-render once setup state machine completes.
      break;
    }
    case WifiItemKind::Delete: {
      wifiConfigsDeleteActive();
      wifiPageRefresh();
      break;
    }
  }
}

void wifiPageRender() {
  listPickerRender(view);
}

WifiPageDigest wifiPageGetDigest() {
  return {view.pageIdx, view.highlightSlot, view.activeIdx, view.count};
}
```

- [ ] **Step 3: Add forward decl + render branch in `src/main.cpp`**

Add includes near the existing module includes at the top of `main.cpp`:
```cpp
#include "WifiPage.h"
#include "WifiSetup.h"
```

Inside `setup()`, after the existing `wifiConfigsBegin();` line (added in Task 2), append:
```cpp
  wifiPageRefresh();
```

In `renderPage(const DisplayState &s)`, add a branch at the very top of the function (immediately after the local-vars but before the existing `def` lookup), so the wifi page bypasses the icon-grid path entirely:
```cpp
void renderPage(const DisplayState &s) {
  if (s.page == Page::Wifi) {
    wifiPageRender();
    return;
  }
  const PageDef &def = pageDefs[static_cast<int>(s.page)];
  // ... existing body unchanged ...
```

In the `DisplayState` struct, add three fields plus extend `operator!=`:
```cpp
struct DisplayState {
  ConnState conn;
  Page page;
  bool scroll;
  bool drag;
  int sensitivity;
  int moveDelay;
  // wifi-page picker state — repaint when any of these change
  uint16_t wifiPickerPage;
  int8_t   wifiHighlight;
  uint16_t wifiActiveIdx;
  uint16_t wifiCount;

  bool operator!=(const DisplayState &o) const {
    return conn != o.conn || page != o.page || scroll != o.scroll ||
           drag != o.drag || sensitivity != o.sensitivity ||
           moveDelay != o.moveDelay ||
           wifiPickerPage != o.wifiPickerPage ||
           wifiHighlight  != o.wifiHighlight  ||
           wifiActiveIdx  != o.wifiActiveIdx  ||
           wifiCount      != o.wifiCount;
  }
};
```

In `printPage()`, replace the `DisplayState now = {...};` initializer to populate the new fields:
```cpp
  WifiPageDigest wd = wifiPageGetDigest();
  DisplayState now = {connState, currentPage,
                      scrollEnabled, dragEnabled,
                      mouseSensitivity, mouseMoveDelay,
                      wd.pageIdx, wd.highlightSlot,
                      wd.activeIdx, wd.count};
```

Update the `static DisplayState last = {...}` initializer the same way:
```cpp
  static DisplayState last = {ConnState::Booting, Page::Mouse,
                              false, false, -1, -1,
                              0xFFFF, -2, 0xFFFF, 0xFFFF};
```

- [ ] **Step 4: Build to verify**

Run: `pio run`
Expected: build succeeds. (This commit unblocks Task 3's pending changes.)

- [ ] **Step 5: Hardware smoke check (optional but useful)**

Run: `pio run -t upload -t monitor`
Expected:
- Boot serial: `[WIFICFG] loaded 0 configs (active=-1)`.
- Navigate down past Remote — you land on the Wifi page (one DN press past Remote, one before Settings).
- Page shows a single row: `A. + Add new...`. No page indicator (only 1 page).
- Pressing `A` inverts that row. Pressing `OK` calls `wifiSetupBegin()` — which is a stub at this point and will likely log nothing or no-op (full implementation in Task 6+).
- Pressing `LT`/`RT` does nothing visible (only one page).
- `UP`/`DN` cycles pages as before.

- [ ] **Step 6: Commit**

```bash
git add src/WifiPage.h src/WifiPage.cpp src/globals.h src/Pages.h src/Pages.cpp src/main.cpp
git commit -m "Add Page::Wifi with list-picker UI for saved configs"
```

---

## Task 5: Route WifiRemote through the active config

**Files:**
- Modify: `src/WifiRemote.cpp`

- [ ] **Step 1: Drop the hardcoded creds block and read from `wifiConfigsGetActive()`**

In `src/WifiRemote.cpp`, delete the entire block (lines starting with the `// SSID/password come from wifi_secrets.ini` comment through the `static uint8_t WIFI_BSSID[6] = ...;` line — i.e. the `#ifndef WIFI_SSID` block + the `WIFI_BSSID` constant) and replace with:
```cpp
#include "WifiConfigs.h"

// SSID/password/BSSID/channel are read per-press from WifiConfigs (NVS).
// DEVICE_TOKEN remains a build-time secret from wifi_secrets.ini — it's
// the bearer auth for the vercel app, not per-network.
#ifndef DEVICE_TOKEN
#define DEVICE_TOKEN  "YOUR_DEVICE_TOKEN"
#endif
```

Replace the `associate()` function so it accepts a `const WifiConfig*`:
```cpp
static bool associate(const WifiConfig& cfg) {
  unsigned long t0 = millis();
  Serial.printf("[WIFI] associate begin ssid=\"%s\" ch=%u "
                "bssid=%02X:%02X:%02X:%02X:%02X:%02X timeout=%lums\n",
                cfg.ssid, (unsigned)cfg.channel,
                cfg.bssid[0], cfg.bssid[1], cfg.bssid[2],
                cfg.bssid[3], cfg.bssid[4], cfg.bssid[5],
                ASSOCIATE_TIMEOUT_MS);
  WiFi.mode(WIFI_STA);
  // WiFi.begin signature: ssid, pass, channel, bssid, connect=true.
  // bssid is non-const uint8_t* in the API even though we don't mutate it.
  WiFi.begin(cfg.ssid, cfg.password, (int32_t)cfg.channel,
             const_cast<uint8_t*>(cfg.bssid));
  unsigned long deadline = millis() + ASSOCIATE_TIMEOUT_MS;
  wl_status_t lastStatus = (wl_status_t)-1;
  while (WiFi.status() != WL_CONNECTED && millis() < deadline) {
    wl_status_t s = WiFi.status();
    if (s != lastStatus) {
      Serial.printf("[WIFI]   status %s (t+%lums)\n",
                    wifiStatusName(s), millis() - t0);
      lastStatus = s;
    }
    delay(50);
  }
  if (WiFi.status() != WL_CONNECTED) {
    Serial.printf("[WIFI] associate timeout after %lums (status=%s)\n",
                  millis() - t0, wifiStatusName(WiFi.status()));
    return false;
  }
  Serial.printf("[WIFI] associated in %lums IP=%s RSSI=%d ch=%d\n",
                millis() - t0,
                WiFi.localIP().toString().c_str(),
                WiFi.RSSI(), WiFi.channel());
  return true;
}
```

In `wifiRemoteFire(const char* buttonName)`, replace the existing placeholder check + `if (!associate())` block (the `if (strcmp(WIFI_SSID, "YOUR_SSID")...)` warning down through `if (!associate()) { ... }`) with:
```cpp
  const WifiConfig* cfg = wifiConfigsGetActive();
  if (!cfg) {
    Serial.println("[WIFI] no active Wi-Fi config — go to Wifi page to add one");
    if (origMhz < 240) setCpuFrequencyMhz(origMhz);
    Serial.printf("[WIFI] === fire end (%lums, no request) ===\n",
                  millis() - t0);
    return;
  }

  if (!associate(*cfg)) {
    Serial.println("[WIFI] aborting: associate failed");
    teardown();
    if (origMhz < 240) setCpuFrequencyMhz(origMhz);
    Serial.printf("[WIFI] === fire end (%lums, no request) ===\n",
                  millis() - t0);
    return;
  }
```

- [ ] **Step 2: Build to verify**

Run: `pio run`
Expected: build succeeds.

- [ ] **Step 3: Hardware smoke check (optional)**

Run: `pio run -t upload -t monitor`
Expected: pressing a Remote-page button now logs `[WIFI] no active Wi-Fi config` and does not associate (since NVS is empty). This is correct — Remote will work again as soon as the captive portal lands a saved config (Tasks 6–11).

- [ ] **Step 4: Commit**

```bash
git add src/WifiRemote.cpp
git commit -m "Route WifiRemote through the active WifiConfigs slot"
```

---

## Task 6: WifiSetup module skeleton + state types

**Files:**
- Create: `src/WifiSetup.h`
- Create: `src/WifiSetup.cpp`
- Modify: `src/main.cpp`

- [ ] **Step 1: Create `src/WifiSetup.h`**

```cpp
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
```

- [ ] **Step 2: Create `src/WifiSetup.cpp` with the state-machine skeleton (no real WiFi work yet)**

```cpp
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
```

- [ ] **Step 3: Wire `wifiSetupTick()` and the button-routing branch into `loop()` in `src/main.cpp`**

In `loop()`, immediately after `updateConnState();`, add:
```cpp
  wifiSetupTick();
```

Replace the existing button-dispatch block:
```cpp
  } else if (pressedButton != -1) {
    if (connState == ConnState::Connected) {
      handleButtonPress(currentPage, pressedButton);
    } else {
      handleButtonPressDisconnected(pressedButton);
    }
    delay(DEBOUNCE_MS);
    // ... existing serial logs ...
    pressedButton = -1;
  }
```
with:
```cpp
  } else if (pressedButton != -1) {
    if (wifiSetupIsActive()) {
      wifiSetupHandleButton(pressedButton);
    } else if (connState == ConnState::Connected) {
      handleButtonPress(currentPage, pressedButton);
    } else {
      handleButtonPressDisconnected(pressedButton);
    }
    delay(DEBOUNCE_MS);

    Serial.print("Page: ");
    Serial.println(static_cast<int>(currentPage));
    Serial.print("mouseEnabled: ");
    Serial.println(mouseEnabled);
    Serial.print("MRefreshDelay: ");
    Serial.println(mouseMoveDelay);
    Serial.print("MSensitivity: ");
    Serial.println(mouseSensitivity);

    pressedButton = -1;
  }
```

- [ ] **Step 4: Build to verify**

Run: `pio run`
Expected: build succeeds.

- [ ] **Step 5: Hardware smoke check (optional)**

Run: `pio run -t upload -t monitor`
Expected: navigating to the Wifi page and pressing `A` then `OK` (to trigger Add) logs `[WIFISETUP] 0 -> 1` (Idle → Scanning), then nothing else (no per-state work yet). The device stays stuck at Scanning until Task 7 lands.

- [ ] **Step 6: Commit**

```bash
git add src/WifiSetup.h src/WifiSetup.cpp src/main.cpp
git commit -m "Add WifiSetup state machine skeleton"
```

---

## Task 7: Scanning state — async Wi-Fi scan + sorted results

**Files:**
- Modify: `src/WifiSetup.cpp`

- [ ] **Step 1: Add scan globals + scan code at the top of `WifiSetup.cpp`**

Replace the includes block at the top of `src/WifiSetup.cpp` with:
```cpp
#include "WifiSetup.h"
#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <string.h>
```

Just below the existing static globals (`state`, `stateEnteredMs`, `statusMessage`, `currentSsid`), add scan-result storage:
```cpp
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
```

- [ ] **Step 2: Implement scan entry, polling, and result harvesting**

Add these helpers above `enterState()`:
```cpp
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

static int8_t cmpRssiDesc(const void* a, const void* b) {
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
```

- [ ] **Step 3: Drive Scanning from `wifiSetupTick()`**

Replace the empty `wifiSetupTick()` body with:
```cpp
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
```

- [ ] **Step 4: Update `wifiSetupCancel()` to tear scan down cleanly**

Replace the existing minimal `wifiSetupCancel()` body with:
```cpp
void wifiSetupCancel() {
  if (state == WifiSetupState::Idle) return;
  Serial.println("[WIFISETUP] cancel");
  WiFi.scanDelete();
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  restoreCpu();
  enterState(WifiSetupState::Idle);
}
```

- [ ] **Step 5: Build to verify**

Run: `pio run`
Expected: build succeeds.

- [ ] **Step 6: Hardware smoke check**

Run: `pio run -t upload -t monitor`
Expected: navigate to Wifi page → highlight Add → press OK. Serial shows:
```
[WIFISETUP] 0 -> 1
[WIFISETUP] cpu 80 -> 240 MHz
[WIFISETUP] scanNetworks(async) rc=...
[WIFISETUP] scan harvested N networks
[WIFISETUP] cpu restored to 80 MHz
[WIFISETUP] 1 -> 0
```
The device should return to Idle after ~2-3 seconds. BLE stays connected throughout.

- [ ] **Step 7: Commit**

```bash
git add src/WifiSetup.cpp
git commit -m "Add Scanning state to WifiSetup with sorted async results"
```

---

## Task 8: PickingSsid state — repurpose ListPicker for scan results

**Files:**
- Modify: `src/WifiSetup.cpp`

- [ ] **Step 1: Add a setup-owned ListPickerView and items array at the top of `WifiSetup.cpp`**

Add these includes at the top (after the existing ones):
```cpp
#include "ListPicker.h"
```

Below the existing scan-result globals, add:
```cpp
// The scan-pick view reuses the same ListPicker primitive used by the
// Wifi page itself. Items are built directly from scanResults[].
static ListPickerItem scanItems[SCAN_MAX + 1];   // +1 for "Cancel" row
static char           scanLabels[SCAN_MAX][16];
static ListPickerView scanView;
static int8_t         pickedScanIdx = -1;        // index into scanResults

enum class ScanItemKind : uint8_t { Net = 0, Cancel = 1 };
```

- [ ] **Step 2: Build the scan-picker items after harvest**

Add this helper after `harvestScan()`:
```cpp
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
```

- [ ] **Step 3: Transition Scanning → PickingSsid instead of Scanning → Idle on success**

In `wifiSetupTick()`'s `case WifiSetupState::Scanning:` block, replace:
```cpp
      harvestScan(n);
      restoreCpu();
      // Stay in Scanning until Task 8 wires up PickingSsid; for now,
      // log results and bounce to Idle so the device doesn't hang.
      enterState(WifiSetupState::Idle);
      break;
```
with:
```cpp
      harvestScan(n);
      restoreCpu();
      buildScanItems();
      enterState(WifiSetupState::PickingSsid);
      break;
```

- [ ] **Step 4: Route button presses to the scan picker while in PickingSsid**

Replace the empty `wifiSetupHandleButton()` body with:
```cpp
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
      // Stub: bounce to Idle. Task 9 will go to WaitingForClient.
      enterState(WifiSetupState::Idle);
      break;
    }
    default: break;
  }
}
```

To use `BTN_*` constants, add this include at the top of `WifiSetup.cpp`:
```cpp
#include "Keypad.h"
```

- [ ] **Step 5: Add a render hook so the scan-picker shows on the OLED while in PickingSsid**

Append a public render helper to `WifiSetup.h`:
```cpp
// Render the OLED for the current setup state. Caller is main.cpp's
// renderPage when wifiSetupIsActive() is true.
void wifiSetupRender();
```

In `WifiSetup.cpp`, add:
```cpp
#include "Display.h"

void wifiSetupRender() {
  if (state == WifiSetupState::PickingSsid) {
    listPickerRender(scanView);
    return;
  }
  // Per-state OLED screens (Scanning/WaitingForClient/etc.) are added in
  // Task 12. Until then, leave the previous frame on the screen.
}
```

In `src/main.cpp`'s `printPage()`, route to the setup renderer when active. Replace the existing `if (now != last)` block body with:
```cpp
  if (now != last) {
    last = now;
    if (wifiSetupIsActive()) {
      wifiSetupRender();
    } else if (connState == ConnState::Connected) {
      renderPage(now);
    } else if (connState == ConnState::Connecting ||
               connState == ConnState::Discoverable) {
      renderStatusScreen(connState);
    }
    // ConnState::Booting: leave the splash from displaySetup() in place.
  }
```

Also extend `DisplayState` to include a setup snapshot so the screen repaints when setup state advances. Add these fields after `wifiCount`:
```cpp
  uint8_t  setupState;       // (uint8_t)WifiSetupState
  uint16_t setupPickerPage;  // valid only in PickingSsid
  int8_t   setupHighlight;   // valid only in PickingSsid
```
extend `operator!=`:
```cpp
           setupState     != o.setupState     ||
           setupPickerPage!= o.setupPickerPage||
           setupHighlight != o.setupHighlight;
```
and update both initializers (`now = {...}` and `static last = {...}`):
```cpp
  // now:
  WifiPageDigest wd = wifiPageGetDigest();
  WifiSetupDigest sd = wifiSetupGetDigest();
  DisplayState now = {connState, currentPage,
                      scrollEnabled, dragEnabled,
                      mouseSensitivity, mouseMoveDelay,
                      wd.pageIdx, wd.highlightSlot,
                      wd.activeIdx, wd.count,
                      sd.state, sd.pickerPage, sd.highlight};

  // last:
  static DisplayState last = {ConnState::Booting, Page::Mouse,
                              false, false, -1, -1,
                              0xFFFF, -2, 0xFFFF, 0xFFFF,
                              0xFF, 0xFFFF, -2};
```

Add a digest accessor in `WifiSetup.h`:
```cpp
struct WifiSetupDigest {
  uint8_t  state;
  uint16_t pickerPage;
  int8_t   highlight;
};
WifiSetupDigest wifiSetupGetDigest();
```
and in `WifiSetup.cpp`:
```cpp
WifiSetupDigest wifiSetupGetDigest() {
  WifiSetupDigest d{};
  d.state = (uint8_t)state;
  d.pickerPage = scanView.pageIdx;
  d.highlight  = scanView.highlightSlot;
  return d;
}
```

- [ ] **Step 6: Build to verify**

Run: `pio run`
Expected: build succeeds.

- [ ] **Step 7: Hardware smoke check**

Run: `pio run -t upload -t monitor`
Expected: enter Add flow → after scan, OLED switches to a list of nearby SSIDs sorted by signal. `LT`/`RT` paginate, `A/B/C/D` highlight, `OK` on a network logs `[WIFISETUP] picked SSID "..."` then returns to Idle (full handoff to AP comes in Task 9). `OK` on the trailing `Cancel` row returns to Idle without picking.

- [ ] **Step 8: Commit**

```bash
git add src/WifiSetup.h src/WifiSetup.cpp src/main.cpp
git commit -m "Add PickingSsid state with scan-result list-picker"
```

---

## Task 9: WaitingForClient state — bring up the setup AP

**Files:**
- Modify: `src/WifiSetup.cpp`

- [ ] **Step 1: Add AP startup helper near the scan helpers**

Add the include at the top:
```cpp
#include <WiFiClient.h>
#include <WiFiServer.h>
```

Add these statics below the scan globals:
```cpp
static const char* AP_SSID = "GGKP-Setup";
static const unsigned long SETUP_AP_TIMEOUT_MS = 90UL * 1000UL;
static WiFiServer httpServer(80);
static bool       httpStarted = false;
```

Add helpers above `enterState()`:
```cpp
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
```

- [ ] **Step 2: Transition PickingSsid → WaitingForClient instead of bouncing to Idle**

In `wifiSetupHandleButton()`'s `case BTN_OK:` branch, replace the stub:
```cpp
      // Stub: bounce to Idle. Task 9 will go to WaitingForClient.
      enterState(WifiSetupState::Idle);
```
with:
```cpp
      startAp();
      enterState(WifiSetupState::WaitingForClient);
```

- [ ] **Step 3: Tick the new state**

Add a case to `wifiSetupTick()`'s switch (between Scanning and Failed):
```cpp
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
```

- [ ] **Step 4: Tear AP in `wifiSetupCancel()`**

Replace the body of `wifiSetupCancel()`:
```cpp
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
```

- [ ] **Step 5: Build to verify**

Run: `pio run`
Expected: build succeeds.

- [ ] **Step 6: Hardware smoke check**

Run: `pio run -t upload -t monitor`
Expected: pick a network from the scan list → device prints `softAP "GGKP-Setup" up, IP=192.168.4.1`. Open phone Wi-Fi settings — `GGKP-Setup` shows up as an open network. Joining it advances state to `WaitingForSubmit` (logged); but no HTTP page is served yet (Task 10).

- [ ] **Step 7: Commit**

```bash
git add src/WifiSetup.cpp
git commit -m "Add WaitingForClient state; bring up setup AP after SSID pick"
```

---

## Task 10: WaitingForSubmit state — synchronous HTTP server

**Files:**
- Modify: `src/WifiSetup.cpp`

- [ ] **Step 1: Add the HTML page templates and form-parsing helpers**

Add to `WifiSetup.cpp` near the top (after the includes):
```cpp
static const char FORM_HTML_PRE[] PROGMEM =
  "<!DOCTYPE html><html><head>"
  "<meta name=viewport content=\"width=device-width,initial-scale=1\">"
  "<title>GGKP Wi-Fi Setup</title>"
  "<style>body{font-family:sans-serif;max-width:320px;margin:2em auto;"
  "padding:0 1em}input[type=password]{width:100%;padding:.5em;"
  "font-size:1em;box-sizing:border-box}button{width:100%;padding:.7em;"
  "font-size:1em;margin-top:1em;background:#007aff;color:#fff;"
  "border:0;border-radius:.5em}.s{font-weight:bold;margin-bottom:1em}"
  "</style></head><body><h2>Add Wi-Fi to GGKP</h2>"
  "<div class=s>SSID: ";
static const char FORM_HTML_MID[] PROGMEM =
  "</div><form method=POST action=\"/save\">"
  "<input type=hidden name=ssid value=\"";
static const char FORM_HTML_POST[] PROGMEM =
  "\"><label>Password<br><input type=password name=password autofocus "
  "minlength=8 maxlength=63></label>"
  "<button type=submit>Save</button></form></body></html>";

static const char SAVED_HTML[] PROGMEM =
  "<!DOCTYPE html><html><head><title>GGKP</title></head><body>"
  "<h2>Saved</h2><p>Disconnect from GGKP-Setup. The device is connecting "
  "to your network now.</p></body></html>";

static int hexNibble(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return 10 + c - 'a';
  if (c >= 'A' && c <= 'F') return 10 + c - 'A';
  return -1;
}

// In-place URL-decode of a x-www-form-urlencoded value. Returns final length.
static size_t urldecodeInPlace(char* s) {
  char* w = s;
  for (char* r = s; *r; ++r) {
    if (*r == '+') { *w++ = ' '; }
    else if (*r == '%' && r[1] && r[2]) {
      int hi = hexNibble(r[1]), lo = hexNibble(r[2]);
      if (hi >= 0 && lo >= 0) { *w++ = (char)((hi << 4) | lo); r += 2; }
      else *w++ = *r;
    } else *w++ = *r;
  }
  *w = 0;
  return (size_t)(w - s);
}

// Find a `name=` field in a form-encoded body. Writes value (decoded) into
// out (max outSz incl NUL). Returns true if found.
static bool formField(const char* body, const char* name,
                      char* out, size_t outSz) {
  size_t nameLen = strlen(name);
  const char* p = body;
  while (*p) {
    if (strncmp(p, name, nameLen) == 0 && p[nameLen] == '=') {
      const char* v = p + nameLen + 1;
      const char* end = strchr(v, '&');
      size_t n = end ? (size_t)(end - v) : strlen(v);
      if (n >= outSz) n = outSz - 1;
      memcpy(out, v, n);
      out[n] = 0;
      urldecodeInPlace(out);
      return true;
    }
    while (*p && *p != '&') ++p;
    if (*p == '&') ++p;
  }
  return false;
}
```

Add submitted-creds storage with the other statics:
```cpp
static char submittedPassword[65] = "";
```

- [ ] **Step 2: Add request-handling helpers**

```cpp
static void sendForm(WiFiClient& cli, const char* ssid) {
  cli.print(F("HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n"
              "Connection: close\r\n\r\n"));
  cli.print((__FlashStringHelper*)FORM_HTML_PRE);
  cli.print(ssid);
  cli.print((__FlashStringHelper*)FORM_HTML_MID);
  cli.print(ssid);
  cli.print((__FlashStringHelper*)FORM_HTML_POST);
}

static void sendSaved(WiFiClient& cli) {
  cli.print(F("HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n"
              "Connection: close\r\n\r\n"));
  cli.print((__FlashStringHelper*)SAVED_HTML);
}

static void sendRedirect(WiFiClient& cli) {
  cli.print(F("HTTP/1.1 302 Found\r\nLocation: http://192.168.4.1/\r\n"
              "Connection: close\r\nContent-Length: 0\r\n\r\n"));
}

// Read one HTTP request from cli into buf (header + up to bodyMax bytes
// of body). Returns true if a full request line + headers were read.
// Body length is capped at bodyMax — fine because our only POST body is
// "ssid=...&password=..." which is well under 256 bytes.
static bool readRequest(WiFiClient& cli, char* buf, size_t bufSz,
                        const char** method, const char** path,
                        const char** body) {
  size_t n = 0;
  unsigned long deadline = millis() + 2000;
  // Headers
  while (n < bufSz - 1 && millis() < deadline) {
    while (cli.available() && n < bufSz - 1) {
      buf[n++] = (char)cli.read();
      if (n >= 4 &&
          buf[n - 4] == '\r' && buf[n - 3] == '\n' &&
          buf[n - 2] == '\r' && buf[n - 1] == '\n') {
        goto headersDone;
      }
    }
    delay(2);
  }
headersDone:
  buf[n] = 0;
  if (n == 0) return false;

  *method = buf;
  char* sp = strchr(buf, ' ');
  if (!sp) return false;
  *sp = 0;
  *path = sp + 1;
  char* sp2 = strchr((char*)*path, ' ');
  if (sp2) *sp2 = 0;

  // Find Content-Length and read body if present.
  *body = "";
  const char* clHdr = strstr(sp2 ? sp2 + 1 : buf + n, "Content-Length:");
  size_t contentLen = 0;
  if (clHdr) contentLen = (size_t)atoi(clHdr + 15);
  if (contentLen > 0 && n < bufSz - 1) {
    size_t off = n;
    size_t want = contentLen;
    if (want > bufSz - 1 - off) want = bufSz - 1 - off;
    deadline = millis() + 2000;
    while (want > 0 && millis() < deadline) {
      while (cli.available() && want > 0) {
        buf[off++] = (char)cli.read();
        want--;
      }
      delay(2);
    }
    buf[off] = 0;
    *body = buf + n;
  }
  return true;
}
```

- [ ] **Step 3: Tick the new state**

Add a case to `wifiSetupTick()`'s switch:
```cpp
    case WifiSetupState::WaitingForSubmit: {
      if (millis() - stateEnteredMs > SETUP_AP_TIMEOUT_MS) {
        strncpy(statusMessage, "setup timeout", sizeof(statusMessage) - 1);
        stopAp();
        enterState(WifiSetupState::Failed);
        break;
      }
      WiFiClient cli = httpServer.available();
      if (!cli) break;
      static char reqBuf[1024];
      const char *method = "", *path = "", *body = "";
      bool ok = readRequest(cli, reqBuf, sizeof(reqBuf),
                            &method, &path, &body);
      Serial.printf("[WIFISETUP] http %s %s ok=%d\n", method, path, ok);
      if (!ok) { cli.stop(); break; }

      // Captive-portal probes from various OSes — redirect to /.
      if (strcmp(path, "/") != 0 && strcmp(path, "/save") != 0) {
        sendRedirect(cli);
        cli.flush();
        cli.stop();
        break;
      }

      if (strcmp(method, "GET") == 0 && strcmp(path, "/") == 0) {
        sendForm(cli, currentSsid);
        cli.flush();
        cli.stop();
        break;
      }

      if (strcmp(method, "POST") == 0 && strcmp(path, "/save") == 0) {
        char pwd[65] = "";
        bool havePwd = formField(body, "password", pwd, sizeof(pwd));
        size_t pwdLen = strlen(pwd);
        if (!havePwd || pwdLen < 8 || pwdLen > 63) {
          // Re-serve the form. Phone validation should also catch this.
          sendForm(cli, currentSsid);
          cli.flush();
          cli.stop();
          break;
        }
        strncpy(submittedPassword, pwd, sizeof(submittedPassword) - 1);
        submittedPassword[sizeof(submittedPassword) - 1] = 0;
        sendSaved(cli);
        cli.flush();
        cli.stop();
        // Tear AP and validate in Saving (Task 11).
        stopAp();
        enterState(WifiSetupState::Saving);
        break;
      }

      // Unknown method/path — redirect.
      sendRedirect(cli);
      cli.flush();
      cli.stop();
      break;
    }
```

- [ ] **Step 4: Build to verify**

Run: `pio run`
Expected: build succeeds.

- [ ] **Step 5: Hardware smoke check**

Run: `pio run -t upload -t monitor`
Expected: pick a network → join `GGKP-Setup` from phone → most phones auto-pop a captive portal page; otherwise visit `http://192.168.4.1/`. Form shows the SSID, accepts a password. Submit → serial logs `[WIFISETUP] http POST /save ok=1` then enters Saving state. (Saving currently does nothing — Task 11 fills it in. The device may stall here.)

- [ ] **Step 6: Commit**

```bash
git add src/WifiSetup.cpp
git commit -m "Add WaitingForSubmit state with synchronous HTTP form server"
```

---

## Task 11: Saving state — validate password by associating, then persist

**Files:**
- Modify: `src/WifiSetup.cpp`

- [ ] **Step 1: Include WifiConfigs and WifiPage so we can persist**

Add includes at the top:
```cpp
#include "WifiConfigs.h"
#include "WifiPage.h"
```

- [ ] **Step 2: Implement Saving in `wifiSetupTick()`**

Add a case to the switch:
```cpp
    case WifiSetupState::Saving: {
      // Single-shot: kick off association on first tick; poll thereafter.
      static unsigned long savingStartedMs = 0;
      if (savingStartedMs != stateEnteredMs) {
        savingStartedMs = stateEnteredMs;
        if (pickedScanIdx < 0) {
          strncpy(statusMessage, "internal error",
                  sizeof(statusMessage) - 1);
          enterState(WifiSetupState::Failed);
          break;
        }
        const ScanEntry& e = scanResults[pickedScanIdx];
        bumpCpu();
        WiFi.mode(WIFI_STA);
        WiFi.begin(e.ssid, submittedPassword, (int32_t)e.channel,
                   const_cast<uint8_t*>(e.bssid));
        Serial.printf("[WIFISETUP] validating creds for \"%s\"\n", e.ssid);
        break;
      }
      wl_status_t s = WiFi.status();
      if (s == WL_CONNECTED) {
        const ScanEntry& e = scanResults[pickedScanIdx];
        WifiConfig cfg{};
        strncpy(cfg.ssid, e.ssid, sizeof(cfg.ssid) - 1);
        strncpy(cfg.password, submittedPassword, sizeof(cfg.password) - 1);
        memcpy(cfg.bssid, e.bssid, 6);
        cfg.channel = e.channel;
        int8_t idx = wifiConfigsAddOrUpdate(cfg, /*setAsActive*/true);
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
        restoreCpu();
        // Wipe submitted password from RAM after persist.
        memset(submittedPassword, 0, sizeof(submittedPassword));
        if (idx < 0) {
          strncpy(statusMessage, "storage full",
                  sizeof(statusMessage) - 1);
          enterState(WifiSetupState::Failed);
        } else {
          wifiPageRefresh();
          enterState(WifiSetupState::Done);
        }
        break;
      }
      if (millis() - stateEnteredMs > 8000) {
        Serial.printf("[WIFISETUP] validation timeout (status=%d)\n", (int)s);
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
        restoreCpu();
        memset(submittedPassword, 0, sizeof(submittedPassword));
        strncpy(statusMessage, "wrong password", sizeof(statusMessage) - 1);
        enterState(WifiSetupState::Failed);
      }
      break;
    }
    case WifiSetupState::Done: {
      if (millis() - stateEnteredMs > 1500) {
        enterState(WifiSetupState::Idle);
      }
      break;
    }
```

- [ ] **Step 3: Build to verify**

Run: `pio run`
Expected: build succeeds.

- [ ] **Step 4: Hardware smoke check (end-to-end)**

Run: `pio run -t upload -t monitor`
Expected: pick a network → join AP → submit correct password → device validates, persists, and `[WIFICFG] added slot 0 (active=0)` logs. State → Done → Idle. Wifi page now shows `A. <SSID>` + `+ Add new...` + `Delete current`. Active config exists and Remote-page presses now associate using the new config.

- Wrong-password test: submit a bad password → after ~8 s, `[WIFISETUP] validation timeout` and Failed state. No slot saved.

- [ ] **Step 5: Commit**

```bash
git add src/WifiSetup.cpp
git commit -m "Add Saving state: validate password, persist on success"
```

---

## Task 12: Per-state OLED screens

**Files:**
- Modify: `src/WifiSetup.cpp`

- [ ] **Step 1: Flesh out `wifiSetupRender()` for every state**

Replace the existing `wifiSetupRender()` body in `WifiSetup.cpp` with:
```cpp
void wifiSetupRender() {
  if (state == WifiSetupState::PickingSsid) {
    listPickerRender(scanView);
    return;
  }
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_5x7_tr);

  switch (state) {
    case WifiSetupState::Scanning:
      u8g2.drawStr(8, 16, "Scanning");
      u8g2.drawStr(8, 28, "for Wi-Fi...");
      break;
    case WifiSetupState::WaitingForClient:
      u8g2.drawStr(0, 8,  "Join Wi-Fi:");
      u8g2.drawStr(0, 22, "GGKP-Setup");
      u8g2.drawStr(0, 40, "192.168.4.1");
      break;
    case WifiSetupState::WaitingForSubmit:
      u8g2.drawStr(0, 8,  "Open browser:");
      u8g2.drawStr(0, 22, "192.168.4.1");
      u8g2.drawStr(0, 40, "Enter password");
      break;
    case WifiSetupState::Saving:
      u8g2.drawStr(0, 8,  "Saving...");
      u8g2.drawStr(0, 22, "Connecting to");
      u8g2.setFont(u8g2_font_4x6_tr);
      u8g2.drawStr(0, 38, currentSsid);
      break;
    case WifiSetupState::Done:
      u8g2.drawStr(20, 14, "Saved");
      u8g2.setFont(u8g2_font_4x6_tr);
      u8g2.drawStr(0, 32, currentSsid);
      break;
    case WifiSetupState::Failed:
      u8g2.drawStr(8, 14, "Failed:");
      u8g2.setFont(u8g2_font_4x6_tr);
      u8g2.drawStr(0, 32, statusMessage);
      break;
    default:
      break;
  }

  // Cancel hint on every non-Picking, non-Done screen.
  if (state != WifiSetupState::PickingSsid &&
      state != WifiSetupState::Done) {
    u8g2.setFont(u8g2_font_4x6_tr);
    u8g2.drawStr(0, SCREEN_HEIGHT - 1, "hold A: cancel");
  }
  u8g2.sendBuffer();
}
```

- [ ] **Step 2: Build to verify**

Run: `pio run`
Expected: build succeeds.

- [ ] **Step 3: Hardware smoke check**

Run: `pio run -t upload -t monitor`
Expected: each setup state shows its dedicated OLED screen. Cancel hint shows on Scanning / Waiting* / Saving / Failed. Page indicator shows on PickingSsid if more than 4 networks found.

- [ ] **Step 4: Commit**

```bash
git add src/WifiSetup.cpp
git commit -m "Add per-state OLED screens for the WiFi setup flow"
```

---

## Task 13: Long-press cancel + suppress idle/sleep during setup

**Files:**
- Modify: `src/WifiSetup.cpp`
- Modify: `src/main.cpp`

- [ ] **Step 1: Add long-press-A polling at the top of `wifiSetupTick()`**

In `WifiSetup.cpp`, add these statics with the other globals:
```cpp
static unsigned long btnAPressStartMs = 0;
static bool          btnAWasDown = false;
```

In `wifiSetupTick()`, immediately after the `if (state == WifiSetupState::Idle) return;` line, add:
```cpp
  // Long-press BTN_A (>= 1 s while held) cancels back to Idle from any
  // non-Idle state. Cheaper than re-IRQ-wiring; only polled during setup.
  bool down = (digitalRead(BTN_A) == LOW);
  if (down && !btnAWasDown) {
    btnAWasDown = true;
    btnAPressStartMs = millis();
  } else if (!down && btnAWasDown) {
    btnAWasDown = false;
  } else if (down && btnAWasDown &&
             (millis() - btnAPressStartMs) >= 1000) {
    Serial.println("[WIFISETUP] long-press A: cancel");
    btnAWasDown = false;
    wifiSetupCancel();
    return;
  }
```

- [ ] **Step 2: Suppress OLED idle and deep-sleep while setup is active**

In `src/main.cpp`'s `loop()`, change the OLED-idle block:
```cpp
  if (!oledAsleep && connState != ConnState::Booting &&
      millis() - lastButtonPressTime >= OLED_IDLE_MS) {
```
to:
```cpp
  if (!oledAsleep && connState != ConnState::Booting &&
      !wifiSetupIsActive() &&
      millis() - lastButtonPressTime >= OLED_IDLE_MS) {
```

And the deep-sleep block:
```cpp
  if (connState != ConnState::Booting &&
      millis() - lastButtonPressTime >= idleTimeoutMs(connState)) {
```
to:
```cpp
  if (connState != ConnState::Booting && !wifiSetupIsActive() &&
      millis() - lastButtonPressTime >= idleTimeoutMs(connState)) {
```

- [ ] **Step 3: Build to verify**

Run: `pio run`
Expected: build succeeds.

- [ ] **Step 4: Hardware smoke check**

Run: `pio run -t upload -t monitor`
Expected: holding `A` for >=1 s during any setup state logs `[WIFISETUP] long-press A: cancel`, tears any active AP/STA, returns to Idle and the wifi page list. OLED stays awake throughout setup (no power-save kick at 30 s).

- [ ] **Step 5: Commit**

```bash
git add src/WifiSetup.cpp src/main.cpp
git commit -m "Add long-press A cancel and suppress idle/sleep during WiFi setup"
```

---

## Task 14: Drop wifi_secrets.ini Wi-Fi creds; refresh CLAUDE.md

**Files:**
- Modify: `wifi_secrets.ini`
- Modify: `CLAUDE.md`

- [ ] **Step 1: Trim `wifi_secrets.ini` to DEVICE_TOKEN only**

The current file (gitignored) has three Wi-Fi build flags plus the device token. Remove the three Wi-Fi flags so only the token remains. Concretely, the file should contain only:
```ini
[wifi_secrets]
build_flags =
    -D DEVICE_TOKEN=\"<your-real-token>\"
```
(Replace `<your-real-token>` with whatever was already in your local file. Do not commit this file — `.gitignore` handles it.)

- [ ] **Step 2: Refresh `CLAUDE.md` Wi-Fi remote section**

Open `CLAUDE.md` and replace the current `### Wi-Fi Remote page` section with:
```markdown
### Wi-Fi remote + multi-config

The Remote page (`Page::Remote`) binds 7 buttons (`A`/`B`/`left`/`ok`/`right`/`C`/`D`) to HTTPS GETs at `https://momoggkp.vercel.app/buttonPress/{name}`. UP/DN remain page nav.

Credentials are **per-saved-config**, not per-build. `WifiConfig` slots are stored in NVS namespace `"wifi"` (one blob per slot via Arduino `Preferences::putBytes`); `wifiConfigsGetActive()` returns the current `(ssid, password, bssid, channel)` tuple, and `wifiRemoteFire()` reads it on every press. If no active config exists (first boot, or active deleted), the press logs `[WIFI] no active Wi-Fi config` and returns without associating.

Adding a new config happens entirely on-device:

- The Wifi page (between Remote and Settings) is a list-picker of saved configs plus synthetic `+ Add new...` and `Delete current` rows. `A/B/C/D` highlight a row, `OK` confirms; `LT/RT` paginate; `UP/DN` page-nav.
- Confirming `+ Add new...` enters the captive-portal flow (`WifiSetup` state machine: `Scanning → PickingSsid → WaitingForClient → WaitingForSubmit → Saving → Done`). The device runs an open AP `GGKP-Setup` at `192.168.4.1` and serves a one-page form. Submitting validates the password by attempting a real STA association (8 s timeout) before persisting.
- Long-press `A` (>= 1 s) cancels any non-Idle setup state. OLED idle and deep-sleep are suppressed for the duration of the flow.

`wifi_secrets.ini` keeps only `DEVICE_TOKEN` (the bearer auth for the vercel app). All other Wi-Fi build_flags were removed when multi-config landed — first flash boots with zero saved configs and the user adds their first network through the captive portal.

The CPU bump to 240 MHz inside `wifiRemoteFire()` is unchanged. `WifiSetup` does the same trick during scan and validation. Teardown via `WiFi.disconnect(true) + WiFi.mode(WIFI_OFF)` still logs `E (NNNN) wifi:timeout when WiFi un-init, type=4` cosmetically.
```

Update the architecture file list near the top to include the new modules. In the bullet list under "Layout:", add after `src/WifiRemote.{h,cpp}` two new bullets:
```markdown
- `src/WifiConfigs.{h,cpp}` — per-location Wi-Fi credentials. NVS-backed slot storage (`namespace "wifi"`, blob per slot). Public API: `wifiConfigsBegin/Count/Get/GetActive/AddOrUpdate/DeleteActive/SetActive`. Hard cap `WIFI_MAX_CONFIGS = 16`.
- `src/ListPicker.{h,cpp}` — generic 4-row list picker primitive used by the Wifi page (and reusable for any future "pick one of N" UI). Two-step `highlight → OK` model; `LT/RT` paginate; full-row inversion + active-dot marker.
- `src/WifiPage.{h,cpp}` — glue between `WifiConfigs` and `ListPicker`. Owns the picker view and items array; injects synthetic `+ Add new...` and `Delete current` rows; maps `OK` to `setActive` / `wifiSetupBegin()` / `wifiConfigsDeleteActive()` based on the confirmed item's `kind`.
- `src/WifiSetup.{h,cpp}` — captive-portal state machine (`Scanning/PickingSsid/WaitingForClient/WaitingForSubmit/Saving/Done/Failed`). Hand-rolled HTTP/1.1 server on `WiFiServer` (no `WebServer` dep). Long-press-A cancel polled in `wifiSetupTick()`. Wakes Wi-Fi only for the duration of the flow.
```

In the `Page` enum bullet, update the description:
```markdown
- `src/globals.h` — `enum class Page` (`Mouse`/`Media`/`Remote`/`Wifi`/`Settings`, `NUM_PAGES = 5`) with wrap-around `++`/`--` overloads, …
```

In the runtime-model section, update the page cycle reference:
```markdown
- `currentPage` cycles `Mouse → Media → Remote → Wifi → Settings → Mouse` via the UP/DN buttons; `++` and `--` on the enum class wrap modulo `NUM_PAGES`. `Mouse` and `Settings` enable the air mouse and keep MPU6050 awake; `Media`/`Remote`/`Wifi` sleep it via `mpuSleep()`. ...
```

Add a short "Editing tips" bullet near the end:
```markdown
- A new "pick one of N" page should reuse `ListPicker` rather than rolling its own UI. Add an `enum *ItemKind` for synthetic rows (Add/Delete/etc.), build `ListPickerItem[]` with kind-tagged userIds, and dispatch from a single `onConfirm()` switch.
```

- [ ] **Step 3: Build & smoke check**

Run: `pio run`
Expected: build succeeds (the `wifi_secrets.ini` change is a no-op for `WifiRemote.cpp` after Task 5; `DEVICE_TOKEN` still flows through the existing build_flag interpolation).

Run: `pio run -t upload -t monitor`
Expected: full flow still works end-to-end. NVS persists across reboot. Multiple configs paginate correctly. Switching active changes which network Remote uses.

- [ ] **Step 4: Commit**

```bash
git add CLAUDE.md
git commit -m "Refresh CLAUDE.md for multi-WiFi configs and captive-portal setup"
```

(`wifi_secrets.ini` is gitignored — the local trim does not get committed.)

---

## Self-Review Checklist (run mentally after writing)

- **Spec coverage**
  - Page::Wifi (NUM_PAGES=5 between Remote/Settings) ✅ Task 3
  - ListPickerView primitive (struct + free fns) ✅ Task 1
  - Captive portal as transient sub-state ✅ Tasks 6–13
  - WifiConfig struct + 16-slot cap ✅ Task 2
  - NVS layout (count, active, cfg.N blobs) ✅ Task 2
  - First-boot empty / no migration ✅ Task 2 + Task 14
  - 4-row OLED layout w/ inverted highlight + active dot + page indicator ✅ Task 1
  - LT/RT paginate, UP/DN nav unchanged ✅ Task 3 (bindings) + Task 1 (input handling)
  - Synthetic items (Add / Delete current) ✅ Task 4
  - State machine (8 states) ✅ Tasks 6–11
  - Async scan, RSSI sort, dedupe ✅ Task 7
  - Open AP `GGKP-Setup` at 192.168.4.1 ✅ Task 9
  - Hand-rolled HTTP server, GET / + POST /save + redirect probes ✅ Task 10
  - Validate password by associating before persist ✅ Task 11
  - Done/Failed/timeout OLED screens ✅ Task 12
  - Long-press A cancel ✅ Task 13
  - Suppress OLED idle + deep sleep during setup ✅ Task 13
  - WifiRemote uses getActiveConfig ✅ Task 5
  - Same-(ssid,bssid) overwrite ✅ Task 2
  - Storage-full check on save ✅ Task 11 (returns -1) + Task 2 (storage full path)
  - Active deleted leaves activeConfigIdx = -1 ✅ Task 2
  - Drop wifi_secrets.ini wifi creds ✅ Task 14
  - CLAUDE.md refresh ✅ Task 14
- **Placeholder scan:** No "TBD" / "TODO" / "implement later" / "similar to Task N" / "add appropriate error handling" remain.
- **Type consistency:** `WifiConfig`, `ListPickerView`, `WifiSetupState`, `wifiPageRefresh`, `wifiSetupBegin/Cancel/Tick/HandleButton/IsActive/Render/GetDigest/GetState/GetStatusMessage/GetCurrentSsid` are spelled identically across all tasks where they appear.
