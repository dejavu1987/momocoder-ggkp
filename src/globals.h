#ifndef GLOBALS_H
#define GLOBALS_H

#include <BLECombo.h>

constexpr int NUM_PAGES = 4;
constexpr int MAX_PAGE = NUM_PAGES - 1;
constexpr int ICONS_PER_PAGE = 9; // 3x3 grid, also matches NUM_BUTTONS

extern BLECombo bleCombo;
extern bool mouseEnabled;
extern bool scrollEnabled;
extern bool dragEnabled;
extern bool pairingMode;
extern int mouseSensitivity;
extern int mouseMoveDelay;

void enterPairingMode();

#endif // GLOBALS_H
