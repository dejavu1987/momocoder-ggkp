#include "globals.h"
#include <BLECombo.h>

#define NUM_BUTTONS 9

unsigned long lastButtonPressTime = 0;

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

// Create an array of button names
const int buttonNames[NUM_BUTTONS] = {BTN_LT, BTN_RT, BTN_UP, BTN_DN, BTN_A,
                                      BTN_B,  BTN_C,  BTN_D,  BTN_OK};

int KEYPAD_PAGE = 0;

volatile int pressedButton = -1;

void buttonInterrupt() {
  // Check the state of each button to determine which button was pressed
  for (int i = 0; i < NUM_BUTTONS; i++) {
    if (digitalRead(buttonNames[i]) == LOW) {
      pressedButton = buttonNames[i];
      break; // Exit the loop when a button is found
    }
  }
  lastButtonPressTime = millis();
}

void handleButtonPressPage0(int pressedButton) {
  switch (pressedButton) {
  case BTN_LT:
    bleCombo.mouseClick(MOUSE_LEFT);
    break;
  case BTN_RT:
    bleCombo.mouseClick(MOUSE_BACK);
    break;
  case BTN_UP:
    KEYPAD_PAGE--;
    break;
  case BTN_DN:
    KEYPAD_PAGE++;
    break;
  case BTN_A:
    bleCombo.write(KEY_ESC);
    break;
  case BTN_B:
    scrollEnabled = !scrollEnabled;
    break;

  case BTN_C:
    dragEnabled = !dragEnabled;
    if (dragEnabled) {
      bleCombo.mousePress(MOUSE_LEFT);
    } else {
      bleCombo.mouseRelease(MOUSE_LEFT);
    }
    break;

  case BTN_D:
    bleCombo.mouseClick(MOUSE_FORWARD);
    break;

  case BTN_OK:
    bleCombo.mouseClick(MOUSE_RIGHT);
    break;
  }
}

void handleButtonPressPage2(int pressedButton) {
  switch (pressedButton) {
  case BTN_LT:
    if (mouseSensitivity > 10)
      mouseSensitivity -= 10;
    break;
  case BTN_RT:
    mouseSensitivity += 10;
    break;
  case BTN_UP:
    KEYPAD_PAGE--;
    break;
  case BTN_DN:
    KEYPAD_PAGE++;
    break;
  case BTN_A:
    bleCombo.write(KEY_ESC);
    break;
  case BTN_B:
    bleCombo.write('f');
    break;
  case BTN_C:
    if (mouseMoveDelay > 5)
      mouseMoveDelay -= 5;
    break;
  case BTN_D:
    mouseMoveDelay += 5;
    break;
  case BTN_OK:
    bleCombo.write(KEY_MEDIA_PLAY_PAUSE);
    break;
  }
}

void handleButtonPressDefault(int pressedButton) {
  switch (pressedButton) {
  case BTN_LT:
    bleCombo.write(KEY_LEFT_ARROW);
    break;
  case BTN_RT:
    bleCombo.write(KEY_RIGHT_ARROW);
    break;
  case BTN_UP:
    KEYPAD_PAGE--;
    break;
  case BTN_DN:
    KEYPAD_PAGE++;
    break;
  case BTN_A:
    bleCombo.write(KEY_ESC);
    break;
  case BTN_B:
    bleCombo.write('f');
    break;
  case BTN_C:
    bleCombo.write(KEY_MEDIA_VOLUME_UP);
    break;
  case BTN_D:
    bleCombo.write(KEY_MEDIA_VOLUME_DOWN);
    break;
  case BTN_OK:
    bleCombo.write(KEY_MEDIA_PLAY_PAUSE);
    break;
  }
}

void handleButtonPress(int page, int pressedButton) {
  if (pressedButton != -1) {
    switch (page) {
    case 0:
      handleButtonPressPage0(pressedButton);
      break;
    case 2:
      handleButtonPressPage2(pressedButton);
      break;
    default:
      handleButtonPressDefault(pressedButton);
      break;
    }
  }
}
