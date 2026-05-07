# Multi-Wi-Fi Configs with Captive-Portal Provisioning

**Status:** Design — pending user review
**Date:** 2026-05-07
**Author:** Anil + Claude (brainstorming)

## Goal

Let the GGKP store and quick-switch between multiple Wi-Fi networks so the device works in different physical locations. New networks are added via an on-device captive portal (no reflash), saved to flash, and selected on-device through a generalizable list-picker UI that can be reused for future selectors (themes, keymaps, etc.).

## Non-goals

- Connecting to the selected network until a Remote-page button is pressed. The wifi page only **selects which config is active**; `WifiRemote.cpp` still associates on-press.
- Hidden-SSID support. Out of scope; scan only finds broadcast SSIDs.
- WPA enterprise (802.1X), WPA3-only networks. PSK only.
- Migration of the existing `wifi_secrets.ini` config. Device boots empty post-flash and the user re-adds their network in ~30 s.

## High-level architecture

### New top-level page: `Page::Wifi`

`NUM_PAGES` 4 → 5. Page enum order: `Mouse=0, Media=1, Remote=2, Wifi=3, Settings=4`. The Wifi page sits between Remote and Settings so config switching is one `DN` away from the buttons that consume the config.

### Generic `ListPickerView` primitive

A struct + free functions (no class hierarchy) usable for any future "pick one of N" UI:

```cpp
struct ListPickerItem {
  const char* label;   // "Home Wifi", "+ Add new...", "Delete current"
  uint8_t kind;        // domain-specific: 0=normal, 1=add, 2=delete, ...
  uint16_t userId;     // domain index (e.g., wifi slot 0..N)
};

struct ListPickerView {
  const ListPickerItem* items;
  uint16_t count;
  uint16_t pageIdx;        // 0..ceil(count/4)-1 — which page of 4 is shown
  int8_t   highlightSlot;  // -1 = nothing pressed yet, 0..3 = inverted row
  uint16_t activeIdx;      // persisted "current" item, drawn with marker
};
```

Free functions: `listPickerOnSlot(view, slot)`, `listPickerOnLeft(view)`, `listPickerOnRight(view)`, `listPickerOnOk(view) -> committedIdx (or -1)`, `listPickerRender(view)`. View is owned by the consumer page; primitive code is stateless.

### Captive portal as a transient sub-state, not a page

A global `WifiSetupState wifiSetupState` (separate from `currentPage`) governs the scan + AP + provisioning flow. While `wifiSetupState != Idle`:

- `UP`/`DN` page nav suppressed
- A custom OLED screen renders for that sub-state
- Long-press `BTN_A` (>1 s) cancels back to Idle

This avoids polluting the `Page` enum with pseudo-pages.

### New module split

| File | Responsibility |
|---|---|
| `src/WifiConfigs.{h,cpp}` | NVS-backed config slots; `WifiConfig` struct; load/save/delete/setActive; `getActiveConfig()` |
| `src/ListPicker.{h,cpp}` | Generic primitive |
| `src/WifiSetup.{h,cpp}` | Scan + AP + captive portal HTTP server + state machine |
| `src/WifiRemote.cpp` | Modified to read `getActiveConfig()` instead of `WIFI_*` macros |
| `src/Pages.{h,cpp}` | `Page::Wifi` bindings + new `ActionKind`s |

## Data model & storage

### `WifiConfig` (in-RAM)

```cpp
struct WifiConfig {
  char     ssid[33];     // 32 + NUL (802.11 max SSID)
  char     password[65]; // 64 + NUL (WPA2 PSK max)
  uint8_t  bssid[6];
  uint8_t  channel;      // 1..13
};
```

~109 bytes. No dynamic allocation. RAM cost for the 16-slot cap = ~1.7 KB.

### In-RAM model

```cpp
constexpr uint8_t WIFI_MAX_CONFIGS = 16;     // hard cap; UI is unlimited via pagination

WifiConfig configs[WIFI_MAX_CONFIGS];
uint8_t    configCount;       // 0..WIFI_MAX_CONFIGS
int8_t     activeConfigIdx;   // -1 = none, else 0..configCount-1
```

Loaded from NVS once at boot. After that, NVS writes happen only on add / delete / setActive — read path is RAM only (no flash latency on `wifiRemoteFire`).

### NVS layout (Arduino `Preferences`, namespace `"wifi"`)

| Key      | Type     | Notes                                  |
|----------|----------|----------------------------------------|
| `count`  | `uchar`  | number of stored slots                 |
| `active` | `char`   | -1 if none, else 0..count-1            |
| `cfg.0`  | `bytes`  | binary `WifiConfig` blob for slot 0    |
| `cfg.1`  | `bytes`  | …                                      |
| ...      | ...      | up to `cfg.15`                         |

Each slot is one atomic `putBytes`/`getBytes` blob. Keys deleted on slot removal.

**Why fixed 16:** flash wear, NVS scan latency at boot, and "you'll never realistically need more than 16 known networks on a personal HID." Cap is UI-cosmetic — easy to bump.

### First-boot behavior

NVS namespace empty → `configCount=0`, `activeConfigIdx=-1`. Wifi page shows only `+ Add new...`. Build-flag `WIFI_SSID/WIFI_PASSWORD/WIFI_CHANNEL` are dropped from `WifiRemote.cpp`. `wifi_secrets.ini` keeps only `DEVICE_TOKEN` (the bearer token for the vercel app — that's a build-time secret, not per-network).

### Migration

None. First flash boots with zero configs; user adds home network via captive portal in ~30 s.

## List-picker rendering & input

### OLED layout (64×48, 4 rows of 12 px)

```
┌────────────────────────────────┐
│ A. Home Wifi             ●     │   ← row 0, 12 px tall
│ B. Work Wifi                   │   ← row 1
│ C. Cafe                        │   ← row 2
│ D. + Add new...                │   ← row 3
└────────────────────────────────┘
```

- Font: `u8g2_font_6x10_tr` (~10 chars wide). With `"X. "` prefix, labels get ~7 chars on-screen. SSIDs longer than 7 chars truncate with no ellipsis. Fallback to `u8g2_font_5x7_tr` (~12 char-wide labels) if 6×10 feels too tight after prototyping.
- **Highlight** (transient — pressed but not OK'd): full-row inverted rectangle (`drawBox` + XOR text).
- **Active** (persisted — currently-saved selection): small filled dot at right edge of row.
- Row can be both highlighted AND active (user pressed the letter that's already saved). Renders as inverted-with-dot.
- Page indicator: `"1/3"` style in bottom-right with `u8g2_font_4x6_tr`, only shown when `count > 4`.

### Input handling

```cpp
bool listPickerHandleButton(ListPickerView& v, int button);
// Returns true if consumed by the picker; false if page should fall through.
```

Mapping:
- `BTN_A`/`B`/`C`/`D` → `highlightSlot = 0/1/2/3` after bounds check. Pressing on an empty slot is a no-op.
- `BTN_LT` → `pageIdx--` if `> 0`, else no-op. Resets `highlightSlot = -1`.
- `BTN_RT` → `pageIdx++` if more pages, else no-op. Resets `highlightSlot = -1`.
- `BTN_OK` → if `highlightSlot >= 0` and slot has an item, `listPickerOnOk(v)` returns the global item index. Caller dispatches based on `item.kind` (Normal/Add/Delete). If `highlightSlot == -1`, no-op.
- `BTN_UP`/`DN` → returns false (not consumed); existing nav-page action runs.

### Dispatch chain

New `ActionKind`s for the picker (Section "Action kinds & bindings"). The `Page::Wifi` binding table is 9 entries that all reference picker actions. `executeAction()` cases for these inspect `currentPage` to know which view to operate on (right now only `Page::Wifi`'s `wifiPickerView`; future pages add their own branch). Single-switch dispatcher preserved.

### Render-time integration

`renderPage()` adds:
```cpp
if (s.page == Page::Wifi) {
  listPickerRender(wifiPickerView);
  return;
}
```

The wifi page does not draw the keypad-icon grid. Picker's `pageIdx`, `highlightSlot`, `activeIdx`, `count` (and the `WifiSetupState` for setup screens) are added to `DisplayState` so `printPage()` only repaints on real change — preserves the I²C-don't-stream-unless-changed invariant.

## Captive portal / scan flow

### State machine

```
Idle
  → user confirms "+ Add new..."  ⇒  Scanning
Scanning              [BLE stays connected; brief STA-mode scan, ~2-3 s]
  → scan completes               ⇒  PickingSsid
  → scan fails / 0 nets          ⇒  Failed("scan failed")
PickingSsid           [4-row list of SSIDs sorted by RSSI desc; LT/RT paginate]
  → A/B/C/D + OK                 ⇒  WaitingForClient
  → BTN_A long-press (>1 s)      ⇒  Idle (cancel)
WaitingForClient      [AP up; OLED shows "Join GGKP-Setup\n192.168.4.1"]
  → phone associates             ⇒  WaitingForSubmit
  → 90 s timeout                 ⇒  Failed("setup timeout")
WaitingForSubmit      [captive portal page served; waits for POST /save]
  → POST /save received          ⇒  Saving
  → 90 s no submit               ⇒  Failed("setup timeout")
Saving                [tear AP, STA assoc with new creds, validate]
  → assoc success                ⇒  Done (commit to NVS, set active, → Idle)
  → assoc fail                   ⇒  Failed("wrong password")
Failed                [OLED shows error 3 s]
  → timer                        ⇒  Idle
Done                  [OLED shows "Saved: <SSID>" 1.5 s]
  → timer                        ⇒  Idle
```

State machine ticked from `loop()` whenever `wifiSetupState != Idle`. Button presses while non-Idle bypass `handleButtonPress` and route to a `wifiSetupHandleButton(btn)`. UP/DN page-nav is suppressed throughout.

### Scan

`WiFi.mode(WIFI_STA)` → `WiFi.scanNetworks(false /*async=false*/, false /*hidden*/)`. Read each via `WiFi.SSID(i)`, `WiFi.RSSI(i)`, `WiFi.BSSID(i)`, `WiFi.channel(i)`. Sort indices by RSSI descending; cap at 32. De-dup by `(ssid, bssid)`; same-SSID different-BSSID stay distinct, exact duplicates dropped. Hand to picker as `ListPickerItem[]` with `kind = Normal`, `userId = scan-result-index`.

CPU bumped to 240 MHz for scan (faster), restored before `WaitingForClient`.

### AP + captive portal

- `WiFi.mode(WIFI_AP)` (drops scan-mode STA — fine, scan is done).
- `WiFi.softAP("GGKP-Setup", nullptr)` — **open** AP, no password. Trade-off: anyone in radio range during the ~90 s window could connect. Acceptable for personal device; avoids displaying AP password on 64×48 OLED. Easy to flip to `"momoggkp"` later for a guard.
- `WiFi.softAPIP()` returns `192.168.4.1` (default).
- Synchronous HTTP server on port 80 using `WiFiServer` (no new lib_dep). Two routes, hand-rolled HTTP/1.1 ~50 lines, matches bare-metal style of `WifiRemote.cpp`'s raw GET.

Routes:
- `GET /` → static HTML page (~1 KB, embedded `const char[]`). Form: hidden `ssid` (pre-filled from picked scan result), visible `password` input, `Save` button. Title shows the picked SSID.
- `POST /save` → parse `application/x-www-form-urlencoded` body (two fields), validate password is 8–63 chars, respond with "Saving, you can disconnect" page, transition to `Saving`.
- `GET /generate_204`, `GET /hotspot-detect.html`, `GET /connecttest.txt` → 302 to `/`. Covers Android/iOS/Windows captive-portal probes so the phone auto-pops the form. No DNS hijack — users may have to type `192.168.4.1` if their OS doesn't probe (most do).

### Validation step in `Saving`

After AP teardown, `WiFi.mode(WIFI_STA)` + `WiFi.begin(ssid, password, channel, bssid)` (using picked scan result's channel/BSSID — same fast-associate trick `WifiRemote` uses). Wait up to 8 s for `WL_CONNECTED`:

- Success → write to next free NVS slot, set `activeConfigIdx`, persist, transition `Done`.
- Failure → discard, transition `Failed("wrong password")`.

After save (success or failure), full teardown: `WiFi.disconnect(true); WiFi.mode(WIFI_OFF)`.

### OLED screens for setup states

```
Scanning:                    WaitingForClient:
┌──────────────┐             ┌──────────────┐
│  Scanning... │             │ Join Wi-Fi:  │
│              │             │ GGKP-Setup   │
│   .  .  .    │             │              │
│              │             │ 192.168.4.1  │
└──────────────┘             └──────────────┘

WaitingForSubmit:            Saving:
┌──────────────┐             ┌──────────────┐
│ Open browser │             │   Saving...  │
│ on phone:    │             │              │
│              │             │  Connecting  │
│ 192.168.4.1  │             │  to <SSID>   │
└──────────────┘             └──────────────┘

Done:                        Failed:
┌──────────────┐             ┌──────────────┐
│    Saved     │             │   Failed:    │
│              │             │              │
│  <SSID>      │             │ wrong pass   │
│              │             │ scan failed  │
└──────────────┘             └──────────────┘
```

Render via existing `printPage()` cache. `DisplayState` extended with `WifiSetupState setup` field + an SSID buffer pointer.

## Action kinds & bindings

### New `ActionKind`s in `Pages.h`

```
ListPickerSlot       { uint8_t slot; }    // 0..3
ListPickerLeft       {}                   // BTN_LT
ListPickerRight      {}                   // BTN_RT
ListPickerConfirm    {}                   // BTN_OK
```

`executeAction()` cases inspect `currentPage` to route to the right view (just `wifiPickerView` for now). Single-switch dispatcher preserved.

### `Page::Wifi` binding table

```cpp
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

Icons all `0` (page draws the list, not the icon grid).

### `pageDefs[]`

```cpp
const PageDef pageDefs[NUM_PAGES] = {
  {Page::Mouse,    mouseBindings,    9},
  {Page::Media,    mediaBindings,    9},
  {Page::Remote,   remoteBindings,   9},
  {Page::Wifi,     wifiBindings,     9},   // NEW
  {Page::Settings, settingsBindings, 9},
};
```

`enum class Page` order: `Mouse=0, Media=1, Remote=2, Wifi=3, Settings=4`. `NUM_PAGES = 5`.

### Synthetic items the wifi page injects

The wifi page hands the picker an items array of:
- One item per saved config (`kind = Normal`, `userId = slot index`)
- Trailing `+ Add new...` (`kind = Add`)
- Trailing `Delete current` (`kind = Delete`) — only if `activeConfigIdx >= 0`

Picker doesn't know these are special; the wifi-specific `onConfirm` callback inspects `item.kind` and dispatches:
- `Normal` → `setActiveConfig(item.userId)`, persist
- `Add` → set `wifiSetupState = Scanning`
- `Delete` → delete `configs[activeConfigIdx]`, set `activeConfigIdx = -1`, persist, refresh items

## `WifiRemote.cpp` plumbing

- Drop `#define WIFI_SSID/WIFI_PASSWORD/WIFI_CHANNEL/WIFI_BSSID` block.
- Drop `static uint8_t WIFI_BSSID[6]` constant.
- `wifiRemoteFire(name)` first calls `getActiveConfig()` (returns `const WifiConfig*` or `nullptr`).
  - `nullptr`: log `[WIFI] no active config`, OLED shows transient "No Wi-Fi config" overlay (same 3 s mechanism as `Failed`), return without associating.
  - Otherwise pass `cfg->ssid, cfg->password, cfg->channel, cfg->bssid` to `WiFi.begin()`.

## Edge cases & error handling

- **MPU6050 during setup:** wifi setup state machine doesn't touch the IMU. While on `Page::Wifi`, `mouseEnabled` is false (Wifi is not in `Mouse | Settings` set in `loop()`), so MPU is asleep — power-friendly during 90 s setup.
- **BLE during setup:** stays connected throughout scan + AP + STA-validate. ESP32-S3 coexistence handles it (proven by current `WifiRemote`).
- **OLED idle timeout:** while `wifiSetupState != Idle`, suppress `displaySetPowerSave(true)` so user always sees setup instructions.
- **Deep sleep:** suppress while `wifiSetupState != Idle`.
- **Same SSID added twice:** on save, if `(ssid, bssid)` already exists, **overwrite** that slot's password instead of appending.
- **All slots full (16) on Save:** `Failed("storage full — delete one first")`. Front-load the check in `WaitingForSubmit` so we don't AP-up a flow that can't succeed.
- **Cancel during setup:** long-press `BTN_A` (≥1 s) at any non-Idle state → tear AP/STA, return to `Idle` and the wifi page list. Visible affordance: small `"hold A: cancel"` line at bottom of every setup screen if it fits. Long-press detection is new infrastructure — current keypad is ISR-with-instant-FALLING-dispatch; implementation needs press-time tracking (only used while `wifiSetupState != Idle`, so it doesn't perturb existing pages).
- **Active config deleted:** `activeConfigIdx = -1`. User must explicitly select a new active. Only path to "no active config" after first save — predictable; user opted in.
- **Deleting a non-active config:** not directly supported (the synthetic row is `Delete current`, which only deletes the active slot). To remove a non-active config, user activates it first (highlight + OK), then triggers Delete current. Trade-off: keeps the synthetic-row vocabulary tiny — no per-row delete affordance to fit on each list item.

## Testing approach

On-device verification (no unit-test framework on the project):

1. Build & flash, observe boot: NVS empty → wifi page shows only `+ Add new...`.
2. Add home Wi-Fi via captive portal end-to-end → verify saved + active, Remote page works.
3. Add a second config (swap router or use phone hotspot) → verify list shows both, switching changes which network Remote uses.
4. Power-cycle → configs persist, active slot persists.
5. Wrong password during setup → `Failed` screen, no slot saved.
6. Cancel via long-press `A` mid-setup → returns cleanly, no AP left running.
7. Pagination: add 5+ configs → verify LT/RT cycles pages.
8. Stress: hold AP for >90 s without submit → `Failed("setup timeout")`, AP torn down, returns to Idle.

Smoke checks beyond the feature:
- Settings page sensitivity/delay still works.
- Mouse page air-mouse still works (MPU sleep/wake on page transitions).
- BLE pairing flow on `Page::Settings` still works.
- Remote page still fires (with active config) — round-trip should still be ~1.2-1.6 s warm.

## Open questions / deferred

- **Captive portal AP password:** currently designed open. Flip to `"momoggkp"` if neighbors-on-the-same-floor concern arises.
- **DNS hijack in AP mode:** skipped for simplicity. Adds `DNSServer` for true any-domain-redirects-to-portal behavior. Re-evaluate if phones don't auto-detect on most networks.
- **Display label vs. SSID:** currently using the SSID directly (truncated). If "Home" / "Work" labels become useful, add a `label[16]` field to `WifiConfig` and a label-edit step in the captive portal form.
