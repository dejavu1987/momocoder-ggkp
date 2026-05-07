#ifndef PAGES_H
#define PAGES_H

#include <stdint.h>
#include "globals.h"

enum class ActionKind : uint8_t {
  None,
  Key,           // bleCombo.write(*p.keyPtr) — single-byte HID code
  MediaKey,      // bleCombo.write(*p.mediaPtr) — multi-byte media report
  MouseClick,    // bleCombo.mouseClick(p.mouseBtn)
  ToggleScroll,  // scrollEnabled = !scrollEnabled
  ToggleDrag,    // dragEnabled = !dragEnabled; press/release MOUSE_LEFT
  NavPrev,       // --currentPage
  NavNext,       // ++currentPage
  AdjustSens,    // mouseSensitivity += p.delta (clamped >= 10)
  AdjustDelay,   // mouseMoveDelay   += p.delta (clamped >= 5)
  EnterPairing,  // enterPairingMode()
  ForgetBonds,   // forgetAllBonds()
  WifiRequest,   // wifiRemoteFire(p.urlPart) — connect-on-press HTTP GET
  CycleBrightness, // displayCycleBrightness() — low→mid→high
  ListPickerSlot,    // p.slot = 0..3 — A/B/C/D row select on a list page
  ListPickerLeft,    // LT — page back in current page's list-picker
  ListPickerRight,   // RT — page forward
  ListPickerConfirm, // OK — commit highlighted row to current page's onConfirm
};

struct Action {
  ActionKind kind;
  union {
    const uint8_t*        keyPtr;
    const MediaKeyReport* mediaPtr;
    uint8_t               mouseBtn;
    int8_t                delta;
    const char*           urlPart;
    uint8_t               slot;     // ListPickerSlot row 0..3
  } p;
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
