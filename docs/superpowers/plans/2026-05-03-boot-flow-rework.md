# Boot-to-connection flow rework — Implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the `bool pairingMode` flag and the blocking R/G/B LED cycle with an explicit `ConnState` state machine that drives a non-blocking LED, a full-screen disconnected status OLED, button-dispatch by state, and a 2-minute disconnected idle-sleep timer.

**Architecture:** A single `connState` global of type `enum class ConnState { Booting, Connecting, Discoverable, Connected }` is updated only via `transitionTo(ConnState)`. `loop()` calls `updateConnState()` (event reactions to BLE link + boot bond check), `updateLed(connState)`, and a dispatcher that picks the OLED renderer + button handler based on the state. The existing `pairingMode` boolean and the LED constants for the unwired green/blue channels are removed in the final task.

**Tech Stack:** PlatformIO + Arduino-ESP32, NimBLE-Arduino (h2zero), U8g2 (olikraus), `BLECombo` fork from earthicko. No test framework — verification is `pio run` (compile) plus on-hardware smoke tests where behavior is observable.

**Spec:** `docs/superpowers/specs/2026-05-03-boot-flow-rework-design.md`

---

## File map

| File | Change |
|---|---|
| `src/globals.h` | Add `enum class ConnState`, `extern ConnState connState`, `IDLE_SLEEP_DISCONNECTED_MS`, `transitionTo()` declaration |
| `src/main.cpp` | Define `connState`, add `transitionTo`, `updateConnState`, `updateLed`, `ledPulseFor`, `renderStatusScreen`, `idleTimeoutMs`; rewrite `loop()`, `printPage()`, `enterPairingMode()`, `forgetAllBonds()`. Remove `pairingMode`, dead LED constants, blocking R/G/B cycle |
| `src/Keypad.h` | Declare `handleButtonPressDisconnected()` |
| `src/Keypad.cpp` | Add `handleButtonPressDisconnected()`; route `handleButtonPress()` based on `connState` |

Out of scope (future): OLED power-down at sleep, NimBLE bond persistence verification, host-name display.

---

## Task 1 — Add ConnState enum + global

**Files:**
- Modify: `src/globals.h`
- Modify: `src/main.cpp`

- [ ] **Step 1.1: Add the enum + extern to `globals.h`**

In `src/globals.h`, after the existing `Page` enum and its `++`/`--` overloads, before `extern BLECombo bleCombo;`, insert:

```cpp
enum class ConnState : int {
  Booting,      // first ~2 s of setup(), splash visible
  Connecting,   // has bonds, advertising, waiting for known host
  Discoverable, // no bonds OR user pressed OK on Pairing page
  Connected,    // bleCombo.isConnected() is true
};

extern ConnState connState;

void transitionTo(ConnState newState);
```

- [ ] **Step 1.2: Define `connState` in `main.cpp`**

Find the line `bool pairingMode = false;` in `src/main.cpp`. Immediately above it, add:

```cpp
ConnState connState = ConnState::Booting;
```

Leave `bool pairingMode = false;` in place — Task 7 removes it.

- [ ] **Step 1.3: Add a stub `transitionTo()` that just assigns**

In `src/main.cpp`, immediately below the `connState` definition, add:

```cpp
void transitionTo(ConnState newState) {
  if (connState == newState) return;
  Serial.print("[STATE] ");
  Serial.print(static_cast<int>(connState));
  Serial.print(" -> ");
  Serial.println(static_cast<int>(newState));
  connState = newState;
}
```

- [ ] **Step 1.4: Build**

```bash
export PATH="$HOME/.platformio/penv/bin:$PATH"
pio run 2>&1 | tail -3
```
Expected: `[SUCCESS]` line.

- [ ] **Step 1.5: Commit**

```bash
git add src/globals.h src/main.cpp
git commit -m "Add ConnState enum and transitionTo() scaffolding"
```

---

## Task 2 — Drive `connState` from BLE events and boot

**Files:**
- Modify: `src/main.cpp`

- [ ] **Step 2.1: Add `updateConnState()`**

In `src/main.cpp`, immediately below `transitionTo()`, add:

```cpp
void updateConnState() {
  bool connected = bleCombo.isConnected();

  switch (connState) {
  case ConnState::Booting:
    transitionTo(NimBLEDevice::getNumBonds() > 0 ? ConnState::Connecting
                                                 : ConnState::Discoverable);
    break;
  case ConnState::Connecting:
  case ConnState::Discoverable:
    if (connected) transitionTo(ConnState::Connected);
    break;
  case ConnState::Connected:
    if (!connected) transitionTo(ConnState::Connecting);
    break;
  }
}
```

- [ ] **Step 2.2: Make `transitionTo()` ensure advertising is running on Connecting/Discoverable entry**

Replace the body of `transitionTo()` with:

```cpp
void transitionTo(ConnState newState) {
  if (connState == newState) return;
  Serial.print("[STATE] ");
  Serial.print(static_cast<int>(connState));
  Serial.print(" -> ");
  Serial.println(static_cast<int>(newState));
  connState = newState;

  if (newState == ConnState::Connecting || newState == ConnState::Discoverable) {
    auto *adv = NimBLEDevice::getAdvertising();
    if (adv && !adv->isAdvertising()) {
      adv->start();
    }
  }
}
```

- [ ] **Step 2.3: Wire `updateConnState()` into `loop()`**

In `src/main.cpp`, locate the start of `void loop(void) {`. Insert immediately after the opening brace, before the existing `mouseEnabled = ...` line:

```cpp
  updateConnState();
```

(`pairingMode` writes elsewhere in the file are still active — Task 7 removes them.)

- [ ] **Step 2.4: Build**

```bash
pio run 2>&1 | tail -3
```
Expected: `[SUCCESS]`.

- [ ] **Step 2.5: Commit**

```bash
git add src/main.cpp
git commit -m "Drive connState from BLE link and boot bond count"
```

---

## Task 3 — Replace LED logic with non-blocking `updateLed()`

**Files:**
- Modify: `src/globals.h`
- Modify: `src/main.cpp`

- [ ] **Step 3.1: Add the new sleep constant to `globals.h`**

After the existing `extern int mouseMoveDelay;` line in `src/globals.h`, before `void enterPairingMode();`, add nothing yet — we'll add the constant in `main.cpp` next to its peers.

- [ ] **Step 3.2: Add `LedPulse`, `ledPulseFor()`, and `updateLed()` in `main.cpp`**

In `src/main.cpp`, find the `setLed()` function. Immediately below it, add:

```cpp
struct LedPulse {
  unsigned long onMs;
  unsigned long offMs;
  uint8_t brightness;
};

LedPulse ledPulseFor(ConnState s) {
  switch (s) {
  case ConnState::Connecting:   return {100, 1900, 30};   // dim heartbeat
  case ConnState::Discoverable: return {150,  150, 80};   // bright fast blink
  default:                      return {0,    0,   0};    // off
  }
}

void updateLed(ConnState s) {
  static unsigned long lastChange = 0;
  static bool isOn = false;
  LedPulse p = ledPulseFor(s);

  if (p.onMs == 0 && p.offMs == 0) {
    if (isOn) {
      analogWrite(R_PIN, 0);
      isOn = false;
    }
    return;
  }

  unsigned long target = isOn ? p.onMs : p.offMs;
  if (millis() - lastChange >= target) {
    isOn = !isOn;
    lastChange = millis();
    analogWrite(R_PIN, isOn ? p.brightness : 0);
  }
}
```

- [ ] **Step 3.3: Replace the disconnected branch's blocking LED cycle**

In `src/main.cpp`, locate the `else { … advertising watchdog … pairingMode blink … 3-second R/G/B cycle … }` block in `loop()` (the entire branch after `if (bleCombo.isConnected()) { … }`).

Replace the entire `else { … }` block with:

```cpp
  }
  // LED is fully driven by connState now.
  updateLed(connState);
```

(The closing brace shown is the `if (bleCombo.isConnected())`'s closing brace — preserve everything inside that branch unchanged for now.)

- [ ] **Step 3.4: Remove the `setLed()` calls from the connected branch**

Inside `if (bleCombo.isConnected()) { … }`, locate and delete the line:

```cpp
    setLed(LED_OFF, LED_CONNECTED, LED_OFF);
```

`updateLed(connState)` now handles all LED writes (including off when Connected).

- [ ] **Step 3.5: Build**

```bash
pio run 2>&1 | tail -3
```
Expected: `[SUCCESS]`.

- [ ] **Step 3.6: Manual smoke test (on hardware)**

Flash the firmware. Observe:
- Boot: LED off briefly.
- Disconnected with prior bond present: red LED dim heartbeat (~100 ms on every 2 s).
- Disconnected with no bond, or after pressing OK on Pairing page: red LED fast blink (~3 Hz, brighter).
- Connected: LED stays off.
- No 3-second sticky button delay.

If LED behavior matches, commit.

- [ ] **Step 3.7: Commit**

```bash
git add src/main.cpp
git commit -m "Replace blocking LED cycle with non-blocking updateLed(connState)"
```

---

## Task 4 — Full-screen status OLED for disconnected states

**Files:**
- Modify: `src/main.cpp`

- [ ] **Step 4.1: Extend `DisplayState` to track `ConnState`**

In `src/main.cpp`, find:

```cpp
struct DisplayState {
  Page page;
  bool scroll;
  bool drag;
  bool pairing;
  int sensitivity;
  int moveDelay;

  bool operator!=(const DisplayState &o) const {
    return page != o.page || scroll != o.scroll || drag != o.drag ||
           pairing != o.pairing || sensitivity != o.sensitivity ||
           moveDelay != o.moveDelay;
  }
};
```

Replace it with:

```cpp
struct DisplayState {
  ConnState conn;
  Page page;
  bool scroll;
  bool drag;
  int sensitivity;
  int moveDelay;

  bool operator!=(const DisplayState &o) const {
    return conn != o.conn || page != o.page || scroll != o.scroll ||
           drag != o.drag || sensitivity != o.sensitivity ||
           moveDelay != o.moveDelay;
  }
};
```

(`pairing` field is gone — `connState` carries it now.)

- [ ] **Step 4.2: Add `renderStatusScreen()`**

In `src/main.cpp`, immediately above the existing `void renderPage(const DisplayState &s)` function, add:

```cpp
void renderStatusScreen(ConnState s) {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_5x7_tr);

  if (s == ConnState::Connecting) {
    u8g2.drawStr(0, 7, "Connecting...");
    u8g2.setFont(u8g2_font_open_iconic_all_2x_t);
    u8g2.drawGlyph(24, 28, ICON_BLUETOOTH);
    u8g2.setFont(u8g2_font_5x7_tr);
    u8g2.drawStr(0, 40, "OK = pair new");
    u8g2.drawStr(0, 47, "A  = forget");
  } else { // Discoverable
    u8g2.drawStr(14, 7, "Pair me");
    u8g2.setFont(u8g2_font_open_iconic_all_2x_t);
    u8g2.drawGlyph(24, 30, ICON_BLUETOOTH);
    u8g2.setFont(u8g2_font_4x6_tr);
    u8g2.drawStr(0, 41, "MomoCoderGGKP");
  }

  u8g2.sendBuffer();
}
```

- [ ] **Step 4.3: Update `renderPage` to drop the now-stale pairing overlay**

In `src/main.cpp`, find the `renderPage()` function. Locate the block:

```cpp
  } else if (s.page == Page::Pairing && s.pairing) {
    u8g2.setFont(u8g2_font_5x7_tr);
    u8g2.drawStr(20, 46, "PAIR");
  }
```

Delete that entire `else if` clause. (The Pairing-page status while connected isn't meaningful anymore — disconnected pairing has its own screen.)

- [ ] **Step 4.4: Dispatch in `printPage()`**

In `src/main.cpp`, find:

```cpp
void printPage() {
  static DisplayState last = {Page::Mouse, false, false, false, -1, -1};
  DisplayState now = {currentPage,      scrollEnabled,    dragEnabled,
                      pairingMode,      mouseSensitivity, mouseMoveDelay};
  if (now != last) {
    last = now;
    renderPage(now);
  }
}
```

Replace it with:

```cpp
void printPage() {
  static DisplayState last = {ConnState::Booting, Page::Mouse,
                              false, false, -1, -1};
  DisplayState now = {connState,       currentPage,
                      scrollEnabled,   dragEnabled,
                      mouseSensitivity, mouseMoveDelay};
  if (now == last || !(now != last)) {
    // (operator!= is what we have; equality is the negation)
  }
  if (now != last) {
    last = now;
    if (connState == ConnState::Connected) {
      renderPage(now);
    } else if (connState == ConnState::Connecting ||
               connState == ConnState::Discoverable) {
      renderStatusScreen(connState);
    }
    // Booting: leave the splash from displaySetup() in place.
  }
}
```

- [ ] **Step 4.5: Build**

```bash
pio run 2>&1 | tail -3
```
Expected: `[SUCCESS]`.

- [ ] **Step 4.6: Manual smoke test**

Flash. Observe:
- First boot with no prior pairing: screen shows "Pair me" + Bluetooth glyph + "MomoCoderGGKP".
- After pairing once and rebooting: screen shows "Connecting..." + Bluetooth glyph + button hints.
- Once paired and connected: screen shows the icon grid for the active page (was Mouse on cold boot).
- Tweaking sensitivity on Page::Settings while connected still updates the live S/D values.

- [ ] **Step 4.7: Commit**

```bash
git add src/main.cpp
git commit -m "Full-screen status OLED for Connecting and Discoverable states"
```

---

## Task 5 — Disconnected button dispatch

**Files:**
- Modify: `src/Keypad.h`
- Modify: `src/Keypad.cpp`

- [ ] **Step 5.1: Declare the new handler**

In `src/Keypad.h`, find:

```cpp
void IRAM_ATTR buttonInterrupt();
void handleButtonPress(Page page, int pressedButton);
```

Replace with:

```cpp
void IRAM_ATTR buttonInterrupt();
void handleButtonPress(Page page, int pressedButton);
void handleButtonPressDisconnected(int pressedButton);
```

- [ ] **Step 5.2: Implement `handleButtonPressDisconnected()` in `Keypad.cpp`**

In `src/Keypad.cpp`, immediately above the existing `void handleButtonPress(Page page, int pressedButton)` function, add:

```cpp
void handleButtonPressDisconnected(int pressedButton) {
  if (connState == ConnState::Connecting) {
    switch (pressedButton) {
    case BTN_OK:
      enterPairingMode();
      break;
    case BTN_A:
      forgetAllBonds();
      break;
    }
  }
  // ConnState::Discoverable: every button is a no-op.
}
```

- [ ] **Step 5.3: Route disconnected button presses in `main.cpp`**

In `src/main.cpp`, find the block:

```cpp
  if (pressedButton != -1) {
    handleButtonPress(currentPage, pressedButton);
    delay(DEBOUNCE_MS);
```

Replace `handleButtonPress(currentPage, pressedButton);` with:

```cpp
    if (connState == ConnState::Connected) {
      handleButtonPress(currentPage, pressedButton);
    } else {
      handleButtonPressDisconnected(pressedButton);
    }
```

- [ ] **Step 5.4: Build**

```bash
pio run 2>&1 | tail -3
```
Expected: `[SUCCESS]`.

- [ ] **Step 5.5: Manual smoke test**

While disconnected:
- Press UP/DN/B/C/D/LT/RT — nothing happens visibly (no page change, nothing on serial besides the press log).
- Press OK while in Connecting — switches to Discoverable (LED becomes fast blink, screen says "Pair me").
- Press A while in Connecting — bonds wipe, switches to Discoverable.

While connected:
- All page handlers work as before.

- [ ] **Step 5.6: Commit**

```bash
git add src/Keypad.h src/Keypad.cpp src/main.cpp
git commit -m "Dispatch buttons through handleButtonPressDisconnected when not connected"
```

---

## Task 6 — Idle-sleep timer for disconnected states

**Files:**
- Modify: `src/main.cpp`

- [ ] **Step 6.1: Add the disconnected timeout constant**

In `src/main.cpp`, locate the existing `constexpr unsigned long IDLE_SLEEP_MS = 60000UL;` line. Replace it with:

```cpp
constexpr unsigned long IDLE_SLEEP_CONNECTED_MS    = 60'000;
constexpr unsigned long IDLE_SLEEP_DISCONNECTED_MS = 120'000;
```

- [ ] **Step 6.2: Add `idleTimeoutMs()` helper**

Immediately below the constants you just added, insert:

```cpp
unsigned long idleTimeoutMs(ConnState s) {
  return (s == ConnState::Connected) ? IDLE_SLEEP_CONNECTED_MS
                                     : IDLE_SLEEP_DISCONNECTED_MS;
}
```

- [ ] **Step 6.3: Move the sleep check out of the connected branch**

In `loop()`, find the block inside `if (bleCombo.isConnected()) { ... }`:

```cpp
    if (millis() - lastButtonPressTime >= IDLE_SLEEP_MS) {
      Serial.println("Going to sleep...");
      delay(DEEP_SLEEP_HOLD_MS);
      esp_deep_sleep_start();
    }
```

Delete those lines from the connected branch.

Then, after the entire `if/else` block in `loop()`, immediately before the `printPage();` call at the end of `loop()`, insert:

```cpp
  if (connState != ConnState::Booting &&
      millis() - lastButtonPressTime >= idleTimeoutMs(connState)) {
    Serial.println("Going to sleep...");
    delay(DEEP_SLEEP_HOLD_MS);
    esp_deep_sleep_start();
  }
```

- [ ] **Step 6.4: Build**

```bash
pio run 2>&1 | tail -3
```
Expected: `[SUCCESS]`.

- [ ] **Step 6.5: Manual smoke test**

- Power on, do not pair. Wait 2 minutes. Device should print `Going to sleep...` and deep-sleep (LED off, OLED freezes).
- Wake by pressing BTN_A (GPIO 6). Device boots back up.
- While connected, idle 60 s without buttons → still sleeps as before.

- [ ] **Step 6.6: Commit**

```bash
git add src/main.cpp
git commit -m "Auto-sleep after 2 min of disconnected idle in addition to connected 60 s"
```

---

## Task 7 — Cleanup: remove `pairingMode`, dead constants, and the old advertising watchdog

**Files:**
- Modify: `src/globals.h`
- Modify: `src/main.cpp`
- Modify: `src/Keypad.cpp`

- [ ] **Step 7.1: Delete `pairingMode` from `globals.h`**

In `src/globals.h`, delete the line:

```cpp
extern bool pairingMode;
```

- [ ] **Step 7.2: Delete `pairingMode` definition and writes in `main.cpp`**

Delete the line:

```cpp
bool pairingMode = false;
```

In `enterPairingMode()`, delete the line:

```cpp
  pairingMode = true;
```

In `forgetAllBonds()`, delete the line:

```cpp
  pairingMode = true;
```

- [ ] **Step 7.3: Drop the old advertising watchdog from the disconnected branch**

In `loop()`, find the `else { … }` branch that currently looks roughly like:

```cpp
  } else {
    auto *adv = NimBLEDevice::getAdvertising();
    if (adv && !adv->isAdvertising()) {
      adv->start();
    }

    if (pairingMode) {
      // ... old blink ...
    } else {
      // ... old serial print + (already-deleted) cycle ...
    }
  }
```

Replace the entire `else` block (including its braces) with empty space — `updateLed(connState)` after the if/else is the only remaining LED logic, and `transitionTo()` already restarts advertising on entry to Connecting/Discoverable. The disconnected branch should now be gone entirely; the `if (bleCombo.isConnected()) { … }` block stays but has no `else`.

- [ ] **Step 7.4: End the rewrite of `enterPairingMode()` and `forgetAllBonds()` with `transitionTo`**

In `enterPairingMode()`, immediately before the closing `}`, replace any remaining direct call to `NimBLEDevice::startAdvertising();` with:

```cpp
  transitionTo(ConnState::Discoverable);
```

(Advertising restart is handled by `transitionTo`.)

In `forgetAllBonds()`, replace `NimBLEDevice::startAdvertising();` immediately before its closing `}` with:

```cpp
  transitionTo(ConnState::Discoverable);
```

- [ ] **Step 7.5: Delete the dead LED brightness constants**

In `src/main.cpp`, delete the four lines:

```cpp
constexpr uint8_t LED_CONNECTED = 55;
constexpr uint8_t LED_PAIRING_BLINK = 80;
constexpr uint8_t LED_WAIT_R = 40;
constexpr uint8_t LED_WAIT_G = 30;
constexpr uint8_t LED_WAIT_B = 20;
```

(`LED_OFF = 0` may stay if still referenced; if grep shows zero references, delete that too.)

- [ ] **Step 7.6: Verify `setLed()` is unreferenced and delete it**

```bash
grep -n "setLed" src/*.h src/*.cpp
```
Expected: no matches. Then delete the `setLed()` function definition from `src/main.cpp` if present.

- [ ] **Step 7.7: Build**

```bash
pio run 2>&1 | tail -3
```
Expected: `[SUCCESS]`. Watch flash/RAM footprint — should drop slightly versus pre-rework.

- [ ] **Step 7.8: Final hardware smoke test**

Walk through the full state machine:
1. Cold boot, no bonds → "Pair me" screen, fast blink.
2. Pair from macOS — Just Works, no passkey prompt. Screen flips to Mouse icon grid, LED off.
3. Disconnect (turn off Mac BT) — screen back to "Connecting...", LED dim heartbeat.
4. Reconnect (turn Mac BT back on) — auto-reconnects, back to icon grid.
5. Press OK on Page::Pairing while connected — disconnects, "Pair me" screen, LED fast blink. macOS bond stays valid.
6. Press A on Page::Pairing while connected — disconnects + wipes bonds. macOS will need to "Forget Device" before re-pairing.
7. Disconnected for 2 min → deep sleep. Press A to wake.

- [ ] **Step 7.9: Commit**

```bash
git add src/globals.h src/main.cpp src/Keypad.cpp
git commit -m "Remove pairingMode, dead LED constants, and the old advertising watchdog"
```

---

## Self-review

**Spec coverage:**
- ConnState enum (Booting/Connecting/Discoverable/Connected) — Task 1 ✓
- Single `transitionTo` writer with advertising entry action — Tasks 1, 2 ✓
- Boot picks state via `getNumBonds()` — Task 2 ✓
- BLE link drives Connecting↔Connected — Task 2 ✓
- Non-blocking LED, dim heartbeat for Connecting, fast blink for Discoverable, off otherwise — Task 3 ✓
- Full-screen status OLED with two layouts — Task 4 ✓
- Device name shown on Discoverable only — Task 4 ✓
- DisplayState cache extended to `ConnState` — Task 4 ✓
- UP/DN/all-others no-op while disconnected — Task 5 ✓
- OK = enter Discoverable from Connecting — Task 5 ✓
- A = forgetAllBonds + Discoverable from Connecting — Task 5 ✓
- 2-min disconnected sleep, 60-s connected sleep — Task 6 ✓
- Removal of `pairingMode`, dead LED constants, advertising watchdog block — Task 7 ✓
- Connected→Discoverable via Page::Pairing's OK and A — preserved by existing handlers calling rewritten `enterPairingMode`/`forgetAllBonds` ✓

**Type/symbol consistency:**
- `connState` lowercase global, `ConnState` uppercase type — used consistently across tasks.
- `transitionTo(ConnState)` declared in Task 1, used in Tasks 2, 7.
- `idleTimeoutMs(ConnState)` defined in Task 6, called once.
- `handleButtonPressDisconnected(int)` declared in Task 5.2, defined in Task 5.2, called in Task 5.3.

**Open risks (called out in spec, verified manually post-implementation):**
- NimBLE bonds persist across deep sleep — verified by Step 7.8 item 4 (reconnect after Mac BT toggle simulates the scenario).
- OLED retains image after deep sleep — out of scope; if it matters, follow-up task to power down OLED at sleep.
