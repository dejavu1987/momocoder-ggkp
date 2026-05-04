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
