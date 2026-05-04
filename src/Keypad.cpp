#include "Keypad.h"
#include "globals.h"
#include <Arduino.h>
#include <BLECombo.h>
#include "Pages.h"

volatile unsigned long lastButtonPressTime = 0;

const int buttonNames[NUM_BUTTONS] = {BTN_LT, BTN_RT, BTN_UP, BTN_DN, BTN_A,
                                      BTN_B,  BTN_C,  BTN_D,  BTN_OK};

Page currentPage = Page::Mouse;

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


void handleButtonPressDisconnected(int pressedButton) {
  if (connState == ConnState::Connecting) {
    switch (pressedButton) {
    case BTN_OK:
      enterPairingMode();
      break;
    case BTN_A:
      forgetAllBonds();
      break;
    }
  }
  // ConnState::Discoverable: every button is a no-op.
}

void handleButtonPress(Page page, int pressedButton) {
  if (pressedButton == -1) return;
  const Binding* b = findBinding(page, pressedButton);
  if (b) executeAction(b->action);
}
