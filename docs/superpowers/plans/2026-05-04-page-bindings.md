# Page bindings refactor — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the three fragmented sources of "what page N button X does" (`pages[][]` icon array in `main.cpp`, four `handleButtonPressPageN` functions in `Keypad.cpp`, and the `Page` enum dispatch) with a single declarative table per page in `src/Pages.{h,cpp}`. Behavior-preserving; no new pages, no new icons, no new actions.

**Architecture:** Tagged-union `Action` (one of 11 kinds: Key, MouseClick, toggles, navigation, adjustments, BLE control). Per-page `constexpr Binding[]` tables keyed by `BTN_*`, listed in `pageDefs[NUM_PAGES]`. `findBinding(page, btn)` is a linear scan over the page's bindings; `executeAction(a)` is one switch over `ActionKind`. Renderer iterates the same tables via a fixed `slotForButton()` map.

**Tech Stack:** PlatformIO + Arduino on `esp32-s3-devkitc-1`; U8g2 OLED driver; ESP32-BLE-Combo (NimBLE-backed). No unit-test framework — verification is `pio run` (build) + hardware smoke tests (user has the device).

**Reference:** [`docs/superpowers/specs/2026-05-04-page-bindings-design.md`](../specs/2026-05-04-page-bindings-design.md)

---

## File map

- **Create:** `src/Pages.h` — `ActionKind`, `Action`, `Binding`, `PageDef`, `slotForButton()`, `findBinding()`, `executeAction()`, `extern pageDefs`
- **Create:** `src/Pages.cpp` — per-page binding tables, `pageDefs[]`, function bodies
- **Modify:** `src/Keypad.cpp` — rewrite `handleButtonPress`; delete `handleButtonPressPage0..3`
- **Modify:** `src/main.cpp` — rewrite `renderPage()`; delete `pages[NUM_PAGES][ICONS_PER_PAGE]`
- **Unchanged:** `src/globals.h` (Page enum, NUM_PAGES); `Keypad.h` (BTN_* constants); `handleButtonPressDisconnected`

---

## Task 1 — Add `src/Pages.h` and skeleton `src/Pages.cpp`

Lock in the data types and infrastructure (slot map, lookup, stub executor) before any tables or wiring. Nothing else consumes `Pages.h` yet, so behavior is unchanged. Empty `pageDefs[]` keeps the link clean.

**Files:**
- Create: `src/Pages.h`
- Create: `src/Pages.cpp`

- [ ] **Step 1.1: Create `src/Pages.h`**

```cpp
#ifndef PAGES_H
#define PAGES_H

#include <stdint.h>
#include "globals.h"

enum class ActionKind : uint8_t {
  None,
  Key,           // bleCombo.write(p.key)
  MouseClick,    // bleCombo.mouseClick(p.mouseBtn)
  ToggleScroll,  // scrollEnabled = !scrollEnabled
  ToggleDrag,    // dragEnabled = !dragEnabled; press/release MOUSE_LEFT
  NavPrev,       // --currentPage
  NavNext,       // ++currentPage
  AdjustSens,    // mouseSensitivity += p.delta (clamped >= 10)
  AdjustDelay,   // mouseMoveDelay   += p.delta (clamped >= 5)
  EnterPairing,  // enterPairingMode()
  ForgetBonds,   // forgetAllBonds()
};

struct Action {
  ActionKind kind;
  union { uint16_t key; uint8_t mouseBtn; int8_t delta; } p;
};

struct Binding {
  int      button;   // BTN_* pin number
  uint16_t icon;     // ICON_* glyph index, 0 = none
  Action   action;
};

struct PageDef {
  Page             id;
  const Binding*   bindings;
  uint8_t          count;
};

extern const PageDef pageDefs[NUM_PAGES];

// Slot index 0..8 in row-major order, matching the current 3x3 layout:
//   A (0)  | UP(1) | B (2)
//   LT(3)  | OK(4) | RT(5)
//   C (6)  | DN(7) | D (8)
// Returns -1 for unknown buttons.
int slotForButton(int button);

const Binding* findBinding(Page page, int button);
void executeAction(const Action& a);

#endif // PAGES_H
```

- [ ] **Step 1.2: Create `src/Pages.cpp` with empty tables and stub `executeAction`**

```cpp
#include "Pages.h"
#include "Keypad.h"

const PageDef pageDefs[NUM_PAGES] = {
  {Page::Mouse,    nullptr, 0},
  {Page::Media,    nullptr, 0},
  {Page::Settings, nullptr, 0},
  {Page::Pairing,  nullptr, 0},
};

int slotForButton(int button) {
  switch (button) {
    case BTN_A:  return 0;
    case BTN_UP: return 1;
    case BTN_B:  return 2;
    case BTN_LT: return 3;
    case BTN_OK: return 4;
    case BTN_RT: return 5;
    case BTN_C:  return 6;
    case BTN_DN: return 7;
    case BTN_D:  return 8;
  }
  return -1;
}

const Binding* findBinding(Page page, int button) {
  const PageDef& def = pageDefs[static_cast<int>(page)];
  for (uint8_t i = 0; i < def.count; ++i) {
    if (def.bindings[i].button == button) return &def.bindings[i];
  }
  return nullptr;
}

void executeAction(const Action& a) {
  // Implemented in Task 3.
  (void)a;
}
```

- [ ] **Step 1.3: Build**

Run: `pio run`
Expected: build succeeds. No new symbols are referenced from anywhere yet, so the firmware behavior is unchanged.

- [ ] **Step 1.4: Commit**

```bash
git add src/Pages.h src/Pages.cpp
git commit -m "Add Pages.{h,cpp} skeleton — types, slot map, stub dispatcher"
```

---

## Task 2 — Populate the four binding tables

Translate today's `pages[][]` icons + `handleButtonPressPageN` actions into `constexpr Binding[]` tables, one per page. Same icons in the same slots, same actions per button. `Page::Pairing` keeps its `ICON_BAN` placeholders (declared with `ActionKind::None`) so the visual is identical.

**Files:**
- Modify: `src/Pages.cpp`

- [ ] **Step 2.1: Replace `pageDefs[]` with the four populated tables**

Replace the block in `src/Pages.cpp` that currently reads:

```cpp
const PageDef pageDefs[NUM_PAGES] = {
  {Page::Mouse,    nullptr, 0},
  {Page::Media,    nullptr, 0},
  {Page::Settings, nullptr, 0},
  {Page::Pairing,  nullptr, 0},
};
```

with:

```cpp
#include <BLECombo.h>
#include "Icons.h"

// Page::Mouse — air mouse with click bindings.
//   A : ESC                UP: nav-prev          B : scroll toggle
//   LT: left click         OK: right click       RT: browser back
//   C : drag toggle        DN: nav-next          D : browser forward
static constexpr Binding mouseBindings[] = {
  {BTN_A,  ICON_CIRCLE_X,        {ActionKind::Key,          {.key=(uint16_t)KEY_ESC}}},
  {BTN_UP, ICON_CHEVRON_TOP,     {ActionKind::NavPrev,      {}}},
  {BTN_B,  ICON_LOOP,            {ActionKind::ToggleScroll, {}}},
  {BTN_LT, ICON_TARGET,          {ActionKind::MouseClick,   {.mouseBtn=MOUSE_LEFT}}},
  {BTN_OK, ICON_MENU,            {ActionKind::MouseClick,   {.mouseBtn=MOUSE_RIGHT}}},
  {BTN_RT, ICON_ACTION_UNDO,     {ActionKind::MouseClick,   {.mouseBtn=MOUSE_BACK}}},
  {BTN_C,  ICON_MOVE,            {ActionKind::ToggleDrag,   {}}},
  {BTN_DN, ICON_CHEVRON_BOTTOM,  {ActionKind::NavNext,      {}}},
  {BTN_D,  ICON_ACTION_REDO,     {ActionKind::MouseClick,   {.mouseBtn=MOUSE_FORWARD}}},
};

// Page::Media — keyboard / media keys.
//   A : ESC          UP: nav-prev    B : 'f' (fullscreen)
//   LT: left arrow   OK: play/pause  RT: right arrow
//   C : vol up       DN: nav-next    D : vol down
static constexpr Binding mediaBindings[] = {
  {BTN_A,  ICON_CIRCLE_X,            {ActionKind::Key, {.key=(uint16_t)KEY_ESC}}},
  {BTN_UP, ICON_CHEVRON_TOP,         {ActionKind::NavPrev, {}}},
  {BTN_B,  ICON_FULLSCREEN_ENTER,    {ActionKind::Key, {.key=(uint16_t)'f'}}},
  {BTN_LT, ICON_MEDIA_SKIP_BACKWARD, {ActionKind::Key, {.key=(uint16_t)KEY_LEFT_ARROW}}},
  {BTN_OK, ICON_MEDIA_PLAY,          {ActionKind::Key, {.key=(uint16_t)KEY_MEDIA_PLAY_PAUSE}}},
  {BTN_RT, ICON_MEDIA_SKIP_FORWARD,  {ActionKind::Key, {.key=(uint16_t)KEY_RIGHT_ARROW}}},
  {BTN_C,  ICON_VOLUME_HIGH,         {ActionKind::Key, {.key=(uint16_t)KEY_MEDIA_VOLUME_UP}}},
  {BTN_DN, ICON_CHEVRON_BOTTOM,      {ActionKind::NavNext, {}}},
  {BTN_D,  ICON_VOLUME_LOW,          {ActionKind::Key, {.key=(uint16_t)KEY_MEDIA_VOLUME_DOWN}}},
};

// Page::Settings — air-mouse tuning. Bottom row icons exist but are not drawn:
// renderPage() replaces the bottom row with a "S:NNN D:NN" overlay. Bindings
// still execute when those buttons are pressed.
//   A : ESC               UP: nav-prev          B : 'f' (fullscreen)
//   LT: sens -10          OK: play/pause        RT: sens +10
//   C : delay -5          DN: nav-next          D : delay +5
static constexpr Binding settingsBindings[] = {
  {BTN_A,  ICON_CIRCLE_X,         {ActionKind::Key, {.key=(uint16_t)KEY_ESC}}},
  {BTN_UP, ICON_CHEVRON_TOP,      {ActionKind::NavPrev, {}}},
  {BTN_B,  ICON_FULLSCREEN_ENTER, {ActionKind::Key, {.key=(uint16_t)'f'}}},
  {BTN_LT, ICON_MINUS,            {ActionKind::AdjustSens,  {.delta=-10}}},
  {BTN_OK, ICON_MEDIA_PLAY,       {ActionKind::Key, {.key=(uint16_t)KEY_MEDIA_PLAY_PAUSE}}},
  {BTN_RT, ICON_PLUS,             {ActionKind::AdjustSens,  {.delta=+10}}},
  {BTN_C,  ICON_BOLT,             {ActionKind::AdjustDelay, {.delta=-5}}},
  {BTN_DN, ICON_CHEVRON_BOTTOM,   {ActionKind::NavNext, {}}},
  {BTN_D,  ICON_TIMER,            {ActionKind::AdjustDelay, {.delta=+5}}},
};

// Page::Pairing — A wipes all bonds (destructive), OK starts pairing,
// UP/DN navigate. Inactive slots show ICON_BAN with no action (preserves the
// current visual exactly).
static constexpr Binding pairingBindings[] = {
  {BTN_A,  ICON_TRASH,           {ActionKind::ForgetBonds,  {}}},
  {BTN_UP, ICON_CHEVRON_TOP,     {ActionKind::NavPrev,      {}}},
  {BTN_B,  ICON_BAN,             {ActionKind::None,         {}}},
  {BTN_LT, ICON_BAN,             {ActionKind::None,         {}}},
  {BTN_OK, ICON_BLUETOOTH,       {ActionKind::EnterPairing, {}}},
  {BTN_RT, ICON_BAN,             {ActionKind::None,         {}}},
  {BTN_C,  ICON_BAN,             {ActionKind::None,         {}}},
  {BTN_DN, ICON_CHEVRON_BOTTOM,  {ActionKind::NavNext,      {}}},
  {BTN_D,  ICON_BAN,             {ActionKind::None,         {}}},
};

const PageDef pageDefs[NUM_PAGES] = {
  {Page::Mouse,    mouseBindings,    9},
  {Page::Media,    mediaBindings,    9},
  {Page::Settings, settingsBindings, 9},
  {Page::Pairing,  pairingBindings,  9},
};
```

- [ ] **Step 2.2: Build**

Run: `pio run`
Expected: build succeeds. The tables exist as `constexpr` data. Nothing reads them yet, so behavior is still unchanged.

If the build fails on a `KEY_*` or `MOUSE_*` symbol, the include order is wrong: `BLECombo.h` must come before any reference to those constants. The `#include <BLECombo.h>` at the top of the new block is what makes them available.

- [ ] **Step 2.3: Commit**

```bash
git add src/Pages.cpp
git commit -m "Populate page binding tables for all four pages"
```

---

## Task 3 — Implement `executeAction()`

One switch covering every `ActionKind`. Matches today's behavior exactly, including the existing clamps on `mouseSensitivity > 10` and `mouseMoveDelay > 5` before subtraction, and the press/release pattern for the drag toggle.

**Files:**
- Modify: `src/Pages.cpp`

- [ ] **Step 3.1: Replace the stub `executeAction()` with the real implementation**

Replace the body of `executeAction()` in `src/Pages.cpp` (currently `(void)a;`) with:

```cpp
void executeAction(const Action& a) {
  switch (a.kind) {
  case ActionKind::None:
    break;
  case ActionKind::Key:
    bleCombo.write(a.p.key);
    break;
  case ActionKind::MouseClick:
    bleCombo.mouseClick(a.p.mouseBtn);
    break;
  case ActionKind::ToggleScroll:
    scrollEnabled = !scrollEnabled;
    break;
  case ActionKind::ToggleDrag:
    dragEnabled = !dragEnabled;
    if (dragEnabled) {
      bleCombo.mousePress(MOUSE_LEFT);
    } else {
      bleCombo.mouseRelease(MOUSE_LEFT);
    }
    break;
  case ActionKind::NavPrev:
    --currentPage;
    break;
  case ActionKind::NavNext:
    ++currentPage;
    break;
  case ActionKind::AdjustSens:
    if (a.p.delta < 0) {
      if (mouseSensitivity > 10) mouseSensitivity += a.p.delta;
    } else {
      mouseSensitivity += a.p.delta;
    }
    break;
  case ActionKind::AdjustDelay:
    if (a.p.delta < 0) {
      if (mouseMoveDelay > 5) mouseMoveDelay += a.p.delta;
    } else {
      mouseMoveDelay += a.p.delta;
    }
    break;
  case ActionKind::EnterPairing:
    enterPairingMode();
    break;
  case ActionKind::ForgetBonds:
    forgetAllBonds();
    break;
  }
}
```

The `AdjustSens`/`AdjustDelay` branches preserve the current asymmetric guard: today `LT` checks `mouseSensitivity > 10` before subtracting 10, while `RT` adds without bound. Same for delay. The `delta < 0` branch mirrors that — only clamp on negative deltas.

- [ ] **Step 3.2: Build**

Run: `pio run`
Expected: build succeeds. `executeAction` references `bleCombo`, `scrollEnabled`, `dragEnabled`, `currentPage`, `mouseSensitivity`, `mouseMoveDelay`, `enterPairingMode`, `forgetAllBonds` — all visible via `globals.h` (already included by `Pages.h`) and `Keypad.h` (already included by `Pages.cpp` for `currentPage`).

If the build fails with `MOUSE_LEFT` undefined, the `<BLECombo.h>` include added in Task 2 covers it (it's at the top of `Pages.cpp` now).

- [ ] **Step 3.3: Commit**

```bash
git add src/Pages.cpp
git commit -m "Implement executeAction() covering all 11 ActionKinds"
```

---

## Task 4 — Wire renderer to `pageDefs[]`; delete `pages[][]`

Switch `renderPage()` from iterating the positional `pages[NUM_PAGES][ICONS_PER_PAGE]` array to iterating `pageDefs[page].bindings` and placing each glyph at `slotForButton(binding.button)`. Delete the now-orphaned `pages[][]` array. The Settings "draw only top 2 rows" rule and the live overlay are preserved.

**Files:**
- Modify: `src/main.cpp`

- [ ] **Step 4.1: Add `#include "Pages.h"` to `src/main.cpp`**

Find the existing include block in `src/main.cpp:24-26`:

```cpp
#include "Display.h"
#include "Icons.h"
#include "Keypad.h"
```

and add `Pages.h` after `Keypad.h`:

```cpp
#include "Display.h"
#include "Icons.h"
#include "Keypad.h"
#include "Pages.h"
```

- [ ] **Step 4.2: Delete the `pages[NUM_PAGES][ICONS_PER_PAGE]` array**

Delete lines `src/main.cpp:96-133` — the entire `// 3x3 icon layout per page...` comment block plus the `const uint16_t pages[NUM_PAGES][ICONS_PER_PAGE] = { ... };` definition.

- [ ] **Step 4.3: Rewrite `renderPage()` to iterate `pageDefs[]`**

Replace `renderPage()` (currently at `src/main.cpp:290-322`) with:

```cpp
void renderPage(const DisplayState &s) {
  const PageDef &def = pageDefs[static_cast<int>(s.page)];
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_open_iconic_all_2x_t);

  // Page::Settings replaces the bottom row with a "S:NNN D:NN" overlay,
  // so suppress glyphs in slots 6..8.
  const int maxSlot = (s.page == Page::Settings) ? 5 : 8;

  for (uint8_t i = 0; i < def.count; ++i) {
    const Binding &b = def.bindings[i];
    if (b.icon == 0) continue;
    int slot = slotForButton(b.button);
    if (slot < 0 || slot > maxSlot) continue;
    int col = slot % 3;
    int row = slot / 3;
    u8g2.drawGlyph(col * 21, row * 16 + 16, b.icon);
  }

  if (s.page == Page::Settings) {
    u8g2.setFont(u8g2_font_5x7_tr);
    char buf[16];
    snprintf(buf, sizeof(buf), "S:%d D:%d", s.sensitivity, s.moveDelay);
    u8g2.drawStr(0, 46, buf);
  } else if (s.page == Page::Mouse && (s.scroll || s.drag)) {
    u8g2.setFont(u8g2_font_5x7_tr);
    if (s.scroll) u8g2.drawStr(0, 6, "S");
    if (s.drag) u8g2.drawStr(8, 6, "D");
  }

  u8g2.sendBuffer();
}
```

The pixel math (`col * 21`, `row * 16 + 16`) matches the previous loop's increments exactly: `x += 21` per column starting at `x=0`, `y += 16` per row starting at `y=16`.

- [ ] **Step 4.4: Build**

Run: `pio run`
Expected: build succeeds. No reference to `pages[][]` should remain.

If the build complains about an unused variable or include, leave it for Task 6's cleanup pass. If it complains about `pages` still being referenced, search for stragglers: `grep -n "pages\[" src/`.

- [ ] **Step 4.5: Hardware smoke test (visual)**

Flash the device and visually verify each page's icons match the previous build, slot for slot:

Run: `pio run -t upload -t monitor`

Expected:
- Page::Mouse — top row `[X | ▲ | ⟲]`, middle `[⊕ | ☰ | ↶]`, bottom `[✥ | ▼ | ↷]`. (The exact glyphs are whichever icons currently render; the test is "did anything move or disappear?")
- Page::Media — top `[X | ▲ | ⛶]`, middle `[⏮ | ▶ | ⏭]`, bottom `[🔊 | ▼ | 🔉]`.
- Page::Settings — top two rows of icons render; bottom row shows the live `S:NNN D:NN` overlay (not icons). Pressing LT/RT still adjusts sensitivity (verifiable by watching the number change).
- Page::Pairing — `[🗑 | ▲ | ⊘]`, `[⊘ | ⌬ | ⊘]`, `[⊘ | ▼ | ⊘]`. The `⊘` placeholders must still appear.
- Mouse-page corner letters `S` and `D` still appear when `scrollEnabled` / `dragEnabled` are toggled.

If any icon moved, disappeared, or duplicated, the slot map or table is wrong — bisect against the previous commit before continuing.

- [ ] **Step 4.6: Commit**

```bash
git add src/main.cpp
git commit -m "Render page icons from pageDefs[]; drop pages[][] array"
```

---

## Task 5 — Wire dispatcher to `executeAction()`; delete `handleButtonPressPage0..3`

Replace the four-way switch + per-page handlers with one lookup-and-execute. `handleButtonPressDisconnected` is unchanged.

**Files:**
- Modify: `src/Keypad.cpp`

- [ ] **Step 5.1: Add `#include "Pages.h"` to `src/Keypad.cpp`**

At the top of `src/Keypad.cpp` (after the existing includes):

```cpp
#include "Keypad.h"
#include "globals.h"
#include <Arduino.h>
#include <BLECombo.h>
#include "Pages.h"
```

- [ ] **Step 5.2: Delete `handleButtonPressPage0..3`**

Delete `src/Keypad.cpp:25-143` — the entire block from `static void handleButtonPressPage0(...)` through the closing brace of `handleButtonPressPage3`.

- [ ] **Step 5.3: Replace `handleButtonPress` with the lookup form**

The current `handleButtonPress` lives at `src/Keypad.cpp:159-175`. Replace it with:

```cpp
void handleButtonPress(Page page, int pressedButton) {
  if (pressedButton == -1) return;
  const Binding* b = findBinding(page, pressedButton);
  if (b) executeAction(b->action);
}
```

`handleButtonPressDisconnected` (at `src/Keypad.cpp:145-157`) is left exactly as-is.

- [ ] **Step 5.4: Build**

Run: `pio run`
Expected: build succeeds. `Keypad.cpp` should no longer reference `bleCombo.write`, `bleCombo.mouseClick`, `scrollEnabled`, `dragEnabled`, `mouseSensitivity`, `mouseMoveDelay`, `enterPairingMode`, or `forgetAllBonds` directly. Sanity check: `grep -n "bleCombo\." src/Keypad.cpp` should now return nothing (BLECombo dispatch all moved to `Pages.cpp`).

- [ ] **Step 5.5: Hardware smoke test (input)**

Run: `pio run -t upload -t monitor`

For each page, confirm every button does what it did before the refactor:

- **Page::Mouse** — A=ESC, UP=prev page, B=toggle scroll (corner `S` appears/disappears), LT=left click, OK=right click, RT=browser back, C=toggle drag (corner `D` appears/disappears, mouse-left held while on), DN=next page, D=browser forward.
- **Page::Media** — A=ESC, UP=prev, B='f', LT=←, OK=play/pause, RT=→, C=vol+, DN=next, D=vol−.
- **Page::Settings** — LT/RT change `S:NNN` by ±10 (LT clamps at 10), C/D change `D:NN` by ±5 (C clamps at 5), OK=play/pause, A=ESC, B='f', UP/DN=page nav.
- **Page::Pairing** — A wipes bonds (host has to re-pair), OK enters pairing mode (current peer disconnects, advertising restarts), UP/DN=page nav, other buttons no-op.
- **Disconnected** — On `ConnState::Connecting`: OK=enter pairing, A=forget bonds. (`handleButtonPressDisconnected` was not touched, so this should still work.)

If any button misbehaves, the bug is in either the binding table (Task 2) or `executeAction` (Task 3) — the dispatcher itself is three lines.

- [ ] **Step 5.6: Commit**

```bash
git add src/Keypad.cpp
git commit -m "Dispatch buttons via findBinding+executeAction; drop per-page handlers"
```

---

## Task 6 — Final cleanup verification

Pure sanity pass — no code changes expected. If something is left orphaned, fix it here.

- [ ] **Step 6.1: Confirm no stale references**

Run: `grep -n "handleButtonPressPage" src/`
Expected: no output (all four handlers deleted).

Run: `grep -n "uint16_t pages\[" src/`
Expected: no output (array deleted).

Run: `grep -rn "pages\[" src/`
Expected: only matches inside comments or unrelated identifiers (e.g. `pageDefs`, `currentPage`). No reference to a `pages[...][...]` array.

- [ ] **Step 6.2: Confirm `ICONS_PER_PAGE` is still used or deleted**

Run: `grep -n "ICONS_PER_PAGE" src/`

If the only reference left is in `globals.h` (its definition), delete the definition. If something still references it, leave it. The previous user is the now-deleted `pages[NUM_PAGES][ICONS_PER_PAGE]` declaration — likely orphaned.

If you delete it, also rebuild: `pio run`.

- [ ] **Step 6.3: Final build**

Run: `pio run`
Expected: clean build, no warnings about unused functions or variables related to this refactor.

- [ ] **Step 6.4: Commit (only if Step 6.2 made changes)**

```bash
git add src/globals.h
git commit -m "Drop now-unused ICONS_PER_PAGE constant"
```

If nothing changed in Step 6.2, skip the commit — the refactor is already complete as of Task 5.

---

## Self-review

Spec-to-plan coverage check:

- Action model (11 kinds, tagged union, `uint16_t key`) — Task 1 (types) + Task 3 (executor).
- `Binding` / `PageDef` structs — Task 1.
- Per-page tables, semantic keying by `BTN_*` — Task 2.
- `findBinding` + `executeAction` dispatch — Task 1 (skeleton) + Task 3 (body) + Task 5 (call site).
- Renderer using `pageDefs` + `slotForButton`, Settings overlay preserved, Mouse `S`/`D` corner preserved — Task 4.
- `Pages.{h,cpp}` file layout, `Keypad.cpp` shrink, `main.cpp` array deletion — Tasks 1, 4, 5.
- Behavioral parity (icons in same slots, every button same action, BAN icons remain on Pairing) — Tasks 2, 4, 5 hardware smoke tests.
- Out-of-scope items (no JSON, disconnected handler unchanged, no defaults) — respected: `handleButtonPressDisconnected` is untouched in Task 5; tables are `constexpr`; every page lists every button it uses.
