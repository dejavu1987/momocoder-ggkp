#include "Keypad.h"
#include "globals.h"
#include <Arduino.h>
#include <BLECombo.h>

volatile unsigned long lastButtonPressTime = 0;

const int buttonNames[NUM_BUTTONS] = {BTN_LT, BTN_RT, BTN_UP, BTN_DN, BTN_A,
                                      BTN_B,  BTN_C,  BTN_D,  BTN_OK};

int KEYPAD_PAGE = 0;

volatile int pressedButton = -1;

void IRAM_ATTR buttonInterrupt() {
  for (int i = 0; i < NUM_BUTTONS; i++) {
    if (digitalRead(buttonNames[i]) == LOW) {
      pressedButton = buttonNames[i];
      break;
    }
  }
  lastButtonPressTime = millis();
}

static void handleButtonPressPage0(int pressedButton) {
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

static void handleButtonPressPage1(int pressedButton) {
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

static void handleButtonPressPage2(int pressedButton) {
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

static void handleButtonPressPage3(int pressedButton) {
  switch (pressedButton) {
  case BTN_OK:
    enterPairingMode();
    break;
  case BTN_UP:
    KEYPAD_PAGE--;
    break;
  case BTN_DN:
    KEYPAD_PAGE++;
    break;
  }
}

void handleButtonPress(int page, int pressedButton) {
  if (pressedButton == -1) return;
  switch (page) {
  case 0:
    handleButtonPressPage0(pressedButton);
    break;
  case 1:
    handleButtonPressPage1(pressedButton);
    break;
  case 2:
    handleButtonPressPage2(pressedButton);
    break;
  case 3:
    handleButtonPressPage3(pressedButton);
    break;
  }
}
