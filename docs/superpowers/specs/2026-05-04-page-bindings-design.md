# Page bindings — unify icons + actions per (page, button)

## Problem

Configuring "what page N button X does" is spread across three places:

1. `pages[NUM_PAGES][ICONS_PER_PAGE]` in `src/main.cpp` — the icon glyph for each
   slot, indexed positionally as `[row*3+col]`. The mapping from slot index back
   to a `BTN_*` is comment-only.
2. `handleButtonPressPage0..3` in `src/Keypad.cpp` — four parallel
   `switch (BTN_*)` blocks, one per page, each doing whatever a button does on
   that page.
3. The `Page` enum in `src/globals.h`, plus the dispatch switch in
   `handleButtonPress(page, btn)`.

Adding a page means touching all three (plus `NUM_PAGES`, plus a new
`handleButtonPressPageN`). The icon table and the action table can drift
silently — nothing checks that the glyph drawn over `BTN_C` matches what
pressing `BTN_C` on that page actually does.

Goal: one declarative table per page, keyed by `BTN_*`, holding both the icon
and the action. One dispatcher. One renderer that reads the same table.

## Design

### Action model

A tagged union. Each kind covers exactly one of the existing behaviors:

```cpp
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
```

`Action` is 4 bytes. `key` is 16-bit because BLECombo's media key codes
(`KEY_MEDIA_VOLUME_UP` etc.) don't fit in 8 bits; regular ASCII keys fit fine.
`ActionKind::None` is the default for unmapped buttons.

### Binding & PageDef

```cpp
struct Binding {
  int      button;  // BTN_* pin number
  uint16_t icon;    // ICON_* glyph index, 0 = no icon
  Action   action;
};

struct PageDef {
  Page             id;
  const Binding*   bindings;
  uint8_t          count;
};
```

A page may declare fewer than 9 bindings; unlisted buttons are no-ops and draw
no glyph (matches today's `ICON_BAN` placeholders on the Pairing page).

### Tables

One `constexpr Binding` array per page in `src/Pages.cpp`, plus a top-level
`constexpr PageDef pageDefs[NUM_PAGES]`. Example for `Page::Mouse`:

```cpp
constexpr Binding mouseBindings[] = {
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
```

`Page::Pairing` declares only the 4 buttons it actually uses.

### Dispatcher

```cpp
const Binding* findBinding(Page page, int button);
void executeAction(const Action& a);

void handleButtonPress(Page page, int button) {
  if (button == -1) return;
  const Binding* b = findBinding(page, button);
  if (b) executeAction(b->action);
}
```

`executeAction` is one switch over `ActionKind` — the union of every behavior
currently scattered across `handleButtonPressPage0..3`. Adjustments use the
existing clamps (`mouseSensitivity > 10` before subtracting, `mouseMoveDelay
> 5` before subtracting).

### Renderer

A fixed slot map encodes the keypad layout once:

```cpp
// Slot index 0..8 in row-major order matches the existing renderer's loop.
//   A  | UP | B
//   LT | OK | RT
//   C  | DN | D
constexpr int slotForButton(int button) { ... }  // returns 0..8 or -1
```

`renderPage()` iterates `pageDefs[page].bindings`, looks up each binding's
slot, and draws the icon at `(slot%3 * 21, slot/3 * 16 + 16)`. Buttons with no
binding draw nothing (the panel is already cleared by `u8g2.clearBuffer()`).

The Settings live overlay (`S:NNN D:NN`) stays as a special case in
`renderPage()` — it is UI state derived from globals, not part of any binding.
Same for the `S`/`D` corner letters on `Page::Mouse`.

## File layout

New files:

- `src/Pages.h` — `ActionKind`, `Action`, `Binding`, `PageDef`, the helper
  `slotForButton()`, and `extern const PageDef pageDefs[NUM_PAGES]`.
- `src/Pages.cpp` — per-page binding tables, `pageDefs[]`, `findBinding()`,
  `executeAction()`.

Changes:

- `src/Keypad.cpp` — delete `handleButtonPressPage0..3`; rewrite
  `handleButtonPress` as a `findBinding` + `executeAction` call.
  `handleButtonPressDisconnected` is unchanged (out of scope — see below).
- `src/main.cpp` — delete the `pages[NUM_PAGES][ICONS_PER_PAGE]` array;
  rewrite `renderPage()` to iterate `pageDefs[page].bindings` and use
  `slotForButton()`.
- `src/globals.h` — no change to the `Page` enum or `NUM_PAGES`.

## Behavioral parity

The refactor is behavior-preserving. After the change, on each page every
button must do exactly what it does today and show exactly the same glyph in
exactly the same slot. The Settings overlay and the Mouse-page `S`/`D` corner
letters render identically.

## Out of scope

- **JSON loading on device.** Tables stay `constexpr`. A future loader can
  parse JSON into the same `Binding[]` shape without changing consumers.
- **Disconnected button handling.** `handleButtonPressDisconnected()` is two
  bindings keyed off `ConnState`, not `Page`. Folding it into this table model
  would force a synthetic page or a parallel table — neither is justified for
  two buttons.
- **Defaults / inheritance between pages.** `BTN_UP`/`BTN_DN`/`BTN_OK` repeat
  across pages today. Each page will list them explicitly. Adding a default
  layer is premature.
- **No new pages, no new icons, no new actions.** Pure restructuring.

## Risks

- The icon-position math (`x = col * 21`, `y = row * 16 + 16`) and the Settings
  "draw only top 2 rows" rule live in `renderPage()`. Both must be preserved.
  The slot-map approach makes this explicit rather than implicit-by-iteration.
- `Action` is a tagged union — UB if `kind` and accessed payload field
  disagree. The constructor pattern in the tables (always pair the kind with
  the matching designated initializer) keeps this safe; `executeAction` reads
  only the field that matches its case branch.
