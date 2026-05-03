#ifndef KEYPAD_H
#define KEYPAD_H

#include <Arduino.h>

#define NUM_BUTTONS 9

/*
Input key matrix
Button names
+---+---+---+
| A | UP| B |
+---+---+---+
| LT| OK| RT|
+---+---+---+
| C | DN| D |
+---+---+---+

Pin numbers
+---+---+---+
|  6|  8| 18|
+---+---+---+
|  5| 15| 17|
+---+---+---+
|  4|  7| 16|
+---+---+---+
*/

#define BTN_LT 5
#define BTN_RT 17
#define BTN_UP 8
#define BTN_DN 7
#define BTN_A 6
#define BTN_B 18
#define BTN_C 4
#define BTN_D 16
#define BTN_OK 15

extern const int buttonNames[NUM_BUTTONS];
extern volatile unsigned long lastButtonPressTime;
extern int KEYPAD_PAGE;
extern volatile int pressedButton;

void IRAM_ATTR buttonInterrupt();
void handleButtonPress(int page, int pressedButton);

#endif // KEYPAD_H
