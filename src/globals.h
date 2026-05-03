#include <BLECombo.h>
#ifndef GLOBALS_H
#define GLOBALS_H

extern BLECombo bleCombo;
extern bool mouseEnabled;
extern bool scrollEnabled;
extern bool dragEnabled;
extern bool pairingMode;
extern int mouseSensitivity;
extern int mouseMoveDelay;

void enterPairingMode();

#endif // GLOBALS_H
