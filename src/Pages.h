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
