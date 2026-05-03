# Boot-to-connection flow rework

## Problem

The current behavior between power-on and an established BLE link has four
overlapping issues:

1. **OLED carries no status** while disconnected — the user sees the same icon
   grid as when connected, and has to glance at the LED to know what state the
   device is in.
2. **Button response is laggy** — the disconnected branch of `loop()` does
   `delay(1000)` × 3 (red/green/blue cycle), so a button press takes up to 3
   seconds to be processed.
3. **First-boot is undiscoverable** — a fresh device with no stored bonds
   gives no on-screen hint to pair. Users have to know to navigate to the
   Pairing page and press OK.
4. **No auto-sleep when never connected** — the deep-sleep timer only fires
   in the connected branch. Leave the device on a desk with no host nearby
   and the battery drains until empty.

The hardware also only has a **red LED** — the G_PIN and B_PIN drives are
silent because those channels aren't populated. Several existing states
(connected = green, pairing = blue blink) are therefore invisible.

## Goals

- A clear, on-screen story for every state between boot and connection.
- Non-blocking loop body — buttons get processed within ~50 ms.
- First-boot "just works": no bonds → automatically discoverable, with the
  device name visible on screen so the user can find it in their host's BT
  menu.
- Auto-sleep when idle while disconnected (2 min), in addition to the existing
  60 s connected timeout.
- LED behavior that uses only the red channel and conveys state through
  on/off patterns.

## Non-goals

- HID-over-BLE descriptor changes.
- Connection-interval tuning.
- Device-name editing UI.
- Showing the connected host's name (NimBLE doesn't reliably expose it
  pre-connect for unbonded hosts).
- Removing or reordering the Pairing page — it still exists so a *connected*
  user can switch hosts without disconnecting first.

## Architecture

### Connection state machine

Replace the `bool pairingMode` flag with an explicit enum:

```cpp
// globals.h, alongside Page
enum class ConnState {
  Booting,       // first ~2 s of setup(), splash visible
  Connecting,    // has bonds, advertising, waiting for known host
  Discoverable,  // no bonds OR user pressed OK on Pairing page
  Connected,     // bleCombo.isConnected() is true
};
extern ConnState connState;
```

Deep sleep is OS-level, not a state — `esp_deep_sleep_start()` is the exit.

A single function `updateConnState()` runs every loop iteration and centralises
all transitions. It's the only place `connState` is written.

### Transition table

| From | Trigger | To |
|---|---|---|
| Booting | `setup()` finishes, `NimBLEDevice::getNumBonds() == 0` | Discoverable |
| Booting | `setup()` finishes, `getNumBonds() > 0` | Connecting |
| Connecting | `bleCombo.isConnected()` becomes true | Connected |
| Connecting | OK pressed (status screen) | Discoverable |
| Connecting | A pressed (`forgetAllBonds()`) | Discoverable |
| Connecting / Discoverable | `millis() - lastButtonPressTime >= 120000` | deep sleep |
| Discoverable | `bleCombo.isConnected()` becomes true | Connected |
| Connected | `bleCombo.isConnected()` becomes false | Connecting |
| Connected | OK on Page::Pairing | Discoverable |
| Connected | A on Page::Pairing (`forgetAllBonds()`) | Discoverable |
| Connected | `millis() - lastButtonPressTime >= 60000` | deep sleep |

Entry actions:
- **Connecting**: ensure `NimBLEDevice::getAdvertising()->isAdvertising()`; start it if not.
- **Discoverable**: same — advertising is the same NimBLE call. The two states
  differ only in UI and bond presence; advertising is identical.
- **Connected**: nothing special. The `printPage()` cache repaints the icon
  grid for `currentPage`.

### LED behavior (red channel only, non-blocking)

Driven from `millis()` deltas. No `delay()` calls in the disconnected path.

| State | Pattern |
|---|---|
| Booting | off |
| Connecting | slow heartbeat — **100 ms on, 1900 ms off** |
| Discoverable | fast blink — **150 ms on, 150 ms off** |
| Connected | off |

A small helper carries the state:

```cpp
struct LedPulse { unsigned long onMs; unsigned long offMs; };
// Connecting:   {100, 1900}
// Discoverable: {150,  150}
// Booting/Connected: {0, 0}  // off
```

`loop()` calls `updateLed(connState)` which reads `millis()` and toggles `R_PIN`.

### OLED layouts

The display has three behaviors:

1. **Booting** — existing `displaySetup()` splash, unchanged.
2. **Connecting / Discoverable** — full-screen status takeover (no icon grid).
3. **Connected** — existing icon grid via `printPage()`.

Layouts (64 × 48 monochrome):

```
Connecting
┌──────64×48──────┐
│Connecting...    │ y=0-7,   font u8g2_font_5x7_tr
│                 │
│   [BT 16×16]    │ y=12-28, ICON_BLUETOOTH at x=24, y_baseline=28
│                 │
│   OK = pair new │ y=34-40, font 5x7
│   A  = forget   │ y=41-47, font 5x7
└─────────────────┘

Discoverable
┌──────64×48──────┐
│  Pair me        │ y=0-7,   font 5x7
│                 │
│   [BT 16×16]    │ y=14-30
│                 │
│MomoCoderGGKP    │ y=36-41, font u8g2_font_4x6_tr (52 px wide, fits)
│                 │
└─────────────────┘
```

The device name is only shown on Discoverable — that's when the user needs
to find it in their host's BT scan list.

A new helper `renderStatusScreen(connState)` lives in `main.cpp` next to
`renderPage()`. The `DisplayState` cache key gains a `ConnState` field so
the screen redraws when the state changes.

When transitioning **Discoverable / Connecting → Connected**, the screen
switches to the icon grid for `currentPage` (whatever page the user was on
before disconnect, or `Page::Mouse` on cold boot — that's the existing
default).

### Button behavior while disconnected

In `Connecting` or `Discoverable`, the per-page handlers are bypassed.
Only three buttons do anything:

| Button | Connecting | Discoverable |
|---|---|---|
| OK | enter Discoverable | no-op |
| A | `forgetAllBonds()` → Discoverable | no-op |
| UP / DN | no-op | no-op |
| All others | no-op | no-op |

While in `Discoverable` no buttons are wired up — the only useful action is
to pair from the host. Already-bonded hosts can still auto-reconnect from
this state because advertising is identical to `Connecting`.

Implementation: `handleButtonPress()` in `Keypad.cpp` checks `connState`. If
not `Connected`, dispatches to a new `handleButtonPressDisconnected(btn)`
function. Otherwise dispatches to the existing per-page switch.

`currentPage` is unchanged while disconnected — the user's page selection is
preserved across the disconnected interval and restored on reconnect.

### Auto-sleep

```cpp
constexpr unsigned long IDLE_SLEEP_CONNECTED_MS    = 60'000;
constexpr unsigned long IDLE_SLEEP_DISCONNECTED_MS = 120'000;

unsigned long idleTimeoutMs(ConnState s) {
  return (s == ConnState::Connected) ? IDLE_SLEEP_CONNECTED_MS
                                     : IDLE_SLEEP_DISCONNECTED_MS;
}
```

Sleep check runs in `loop()` regardless of state (except `Booting`):
```cpp
if (connState != ConnState::Booting &&
    millis() - lastButtonPressTime >= idleTimeoutMs(connState)) {
  esp_deep_sleep_start();
}
```

Wake source unchanged: `GPIO_NUM_6` (BTN_A) ext0 LOW.

## Refactor scope

### Removed

- `bool pairingMode` global (replaced by `ConnState`)
- The 3 × `delay(1000)` blocking R/G/B cycle in `loop()`
- The advertising watchdog block (folded into state entry actions)
- `LED_CONNECTED`, `LED_WAIT_G`, `LED_WAIT_B`, `LED_PAIRING_BLINK` constants
  (subsumed by `LedPulse` patterns)
- `enterPairingMode()` and `forgetAllBonds()` get rewritten as state-transition
  helpers; their public signatures may change

### Preserved

- All four page handlers (`handleButtonPressPage0/1/2/3`) — still used in
  `Connected`
- `Icons.h`, the icon grid, the `pages[][]` table
- `Display.cpp`, U8g2 setup, `displaySetup()` splash
- `setLed(r, g, b)` signature — passing 0 to the dead g/b channels stays
  harmless; keeps the API generic if hardware ever changes
- The `DisplayState` cache (gains a `ConnState` field)

### New

- `enum class ConnState` and `extern ConnState connState` in `globals.h`
- `updateConnState()` — central transition function in `main.cpp`
- `updateLed(ConnState)` — non-blocking LED driver in `main.cpp`
- `renderStatusScreen(ConnState)` — OLED status screen in `main.cpp`
- `handleButtonPressDisconnected(int)` — global button dispatch in
  `Keypad.cpp`

## Edge cases

- **Wake from deep sleep**: `setup()` runs again. `getNumBonds()` decides
  initial state (Connecting if bonds, Discoverable if not). Same as cold boot.
- **`forgetAllBonds()` while Connected**: must disconnect first. Drops to
  Discoverable.
- **Host comes back into range during the 2 min disconnected window**: NimBLE
  auto-connect handles it; `bleCombo.isConnected()` flips, transition fires.
- **User presses A on Pairing page while Connected**: `forgetAllBonds()`
  disconnects, Connected → Connecting (briefly) → Discoverable.
- **Device name longer than 13 chars at 4x6**: any name longer than ~16 chars
  gets clipped. `MomoCoderGGKP` (13 chars) fits. Renaming to a longer string
  needs the layout reconsidered.

## Open questions / risks

- **NimBLE bond persistence across `esp_deep_sleep_start()`**: needs to be
  verified on first build. If bonds don't survive deep sleep, every wake is
  effectively a fresh boot, defeating the Connecting state. NimBLE-Arduino
  defaults to NVS persistence; should be fine, but flag it for the
  implementation plan.
- **OLED retains image after deep sleep**: SSD1306 keeps its frame buffer
  while powered. Powering down the OLED rail at sleep is not in scope here
  — if the screen stays lit at low brightness during sleep, treat as a
  follow-up.
