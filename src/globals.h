#ifndef GLOBALS_H
#define GLOBALS_H

#include <BLECombo.h>

constexpr int NUM_PAGES = 4;
constexpr int ICONS_PER_PAGE = 9; // 3x3 grid, also matches NUM_BUTTONS

enum class Page : int {
  Mouse = 0,    // air mouse active
  Media = 1,    // keyboard / media keys
  Settings = 2, // air mouse active + sensitivity / delay tuning
  Pairing = 3,  // BLE re-pairing
};

inline Page &operator++(Page &p) {
  p = static_cast<Page>((static_cast<int>(p) + 1) % NUM_PAGES);
  return p;
}

inline Page &operator--(Page &p) {
  p = static_cast<Page>((static_cast<int>(p) - 1 + NUM_PAGES) % NUM_PAGES);
  return p;
}

extern BLECombo bleCombo;
extern bool mouseEnabled;
extern bool scrollEnabled;
extern bool dragEnabled;
extern bool pairingMode;
extern int mouseSensitivity;
extern int mouseMoveDelay;

void enterPairingMode();
void forgetAllBonds();

#endif // GLOBALS_H
