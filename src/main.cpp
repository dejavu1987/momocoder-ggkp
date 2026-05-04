#include <BLECombo.h>
#include <Wire.h>
#include <keyvals.cpp>

#include "globals.h"

BLECombo bleCombo("MomoCoderGGKP");

#define B_PIN 3
#define R_PIN 45
#define G_PIN 47

#include <NimBLEBeacon.h> // Additional BLE functionaity using NimBLE
#include <NimBLEDevice.h> // Additional BLE functionaity using NimBLE
#include <NimBLEUtils.h>  // Additional BLE functionaity using NimBLE

#include <esp_bt_device.h> // Additional BLE functionaity
#include <esp_bt_main.h>   // Additional BLE functionaity
#include <esp_sleep.h>     // Additional BLE functionaity

#define I2C_SCL 1
#define I2C_SDA 2

#include "Display.h"
#include "Icons.h"
#include "Keypad.h"
#include "Pages.h"

#define USE_AIR_MOUSE

#ifdef USE_AIR_MOUSE
#include "AirMouse.h"
#endif
// #include <ArduinoJson.h> // Using ArduinoJson to read and write config files

// #include <WiFi.h> // Wifi support

// #include <AsyncTCP.h>          //Async Webserver support header
// #include <ESPAsyncWebServer.h> //Async Webserver support header

// #include <ESPmDNS.h> // DNS functionality

// AsyncWebServer webserver(80);

// Loop timing
constexpr unsigned long DEBOUNCE_MS = 200;
constexpr unsigned long OLED_IDLE_MS                = 30000UL;
constexpr unsigned long IDLE_SLEEP_CONNECTED_MS    = 60000UL;
constexpr unsigned long IDLE_SLEEP_DISCONNECTED_MS = 120000UL;

static unsigned long idleTimeoutMs(ConnState s) {
  return (s == ConnState::Connected) ? IDLE_SLEEP_CONNECTED_MS
                                     : IDLE_SLEEP_DISCONNECTED_MS;
}
constexpr unsigned long DISCONNECT_TIMEOUT_MS = 1000;
constexpr unsigned long DEEP_SLEEP_HOLD_MS = 2000;
constexpr unsigned long SCROLL_PAUSE_MS = 50;

int mouseSensitivity = 300;
int mouseMoveDelay = 5;

struct LedPulse {
  unsigned long onMs;
  unsigned long offMs;
  uint8_t brightness;
};

LedPulse ledPulseFor(ConnState s) {
  switch (s) {
  case ConnState::Connecting:   return {100, 1900, 30};   // dim heartbeat
  case ConnState::Discoverable: return {150,  150, 80};   // bright fast blink
  default:                      return {0,    0,   0};    // off
  }
}

void updateLed(ConnState s) {
  static unsigned long lastChange = 0;
  static bool isOn = false;
  LedPulse p = ledPulseFor(s);

  if (p.onMs == 0 && p.offMs == 0) {
    if (isOn) {
      analogWrite(R_PIN, 0);
      isOn = false;
    }
    return;
  }

  unsigned long now = millis();
  unsigned long target = isOn ? p.onMs : p.offMs;
  if (now - lastChange >= target) {
    isOn = !isOn;
    lastChange = now;
    analogWrite(R_PIN, isOn ? p.brightness : 0);
  }
}

ConnState connState = ConnState::Booting;

void transitionTo(ConnState newState) {
  if (connState == newState) return;
  Serial.print("[STATE] ");
  Serial.print(static_cast<int>(connState));
  Serial.print(" -> ");
  Serial.println(static_cast<int>(newState));
  connState = newState;

  if (newState == ConnState::Connecting || newState == ConnState::Discoverable) {
    auto *adv = NimBLEDevice::getAdvertising();
    if (adv && !adv->isAdvertising()) {
      Serial.println("[AUTO] BLE advertising start");
      adv->start();
    }
  }
}

void updateConnState() {
  bool connected = bleCombo.isConnected();

  switch (connState) {
  case ConnState::Booting:
    transitionTo(NimBLEDevice::getNumBonds() > 0 ? ConnState::Connecting
                                                 : ConnState::Discoverable);
    break;
  case ConnState::Connecting:
  case ConnState::Discoverable:
    if (connected) transitionTo(ConnState::Connected);
    break;
  case ConnState::Connected:
    if (!connected) transitionTo(ConnState::Connecting);
    break;
  default:
    break;
  }
}

// Drop the active connection (if any) and re-advertise so a new host can pair.
// Existing bonds stay intact — wiping them here is what was forcing macOS into
// the "Forget Device + re-pair every time" loop, because the host kept its
// bond while the ESP32 lost its half. Use forgetAllBonds() for a true reset.
void enterPairingMode() {
  Serial.println("[INFO]: Entering BLE pairing mode");

  auto *server = NimBLEDevice::getServer();
  if (server && server->getConnectedCount() > 0) {
    auto info = server->getPeerInfo(0);
    server->disconnect(info.getConnHandle());
    unsigned long deadline = millis() + DISCONNECT_TIMEOUT_MS;
    while (server->getConnectedCount() > 0 && millis() < deadline) {
      delay(20);
    }
  }
  transitionTo(ConnState::Discoverable);
}

// Wipe all stored bonds — destructive, every previously-paired host has to
// "Forget Device" and pair again. Wired to BTN_A on the Settings page.
void forgetAllBonds() {
  Serial.println("[INFO]: Wiping all BLE bonds");
  auto *server = NimBLEDevice::getServer();
  if (server && server->getConnectedCount() > 0) {
    auto info = server->getPeerInfo(0);
    server->disconnect(info.getConnHandle());
    unsigned long deadline = millis() + DISCONNECT_TIMEOUT_MS;
    while (server->getConnectedCount() > 0 && millis() < deadline) {
      delay(20);
    }
  }
  NimBLEDevice::deleteAllBonds();
  transitionTo(ConnState::Discoverable);
}

void setup(void) {
  Serial.begin(115200);
  delay(2000);
  Serial.println("[INFO]: Serial initialized");

  // Set the RGB pins as outputs
  pinMode(B_PIN, OUTPUT);
  pinMode(R_PIN, OUTPUT);
  pinMode(G_PIN, OUTPUT);

  // Configure buttons as INPUT_PULLUP and attach interrupts
  for (int i = 0; i < NUM_BUTTONS; i++) {
    pinMode(buttonNames[i], INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(buttonNames[i]), buttonInterrupt,
                    FALLING);
  }
  Serial.println("[INFO]: Keypad initialized");

  bleCombo.begin();
  // BLECombo defaults to setSecurityAuth(bond=true, mitm=true, sc=true) and
  // never sets IO capability, so NimBLE falls back to DisplayOnly. macOS then
  // expects a passkey workflow this keypad can't satisfy, and pairing fails
  // intermittently. HID devices without a display want Just Works pairing.
  NimBLEDevice::setSecurityAuth(/*bond=*/true, /*mitm=*/false, /*sc=*/true);
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
  Serial.println("[INFO]: Starting BLE");
  Wire.begin(I2C_SDA, I2C_SCL);

#ifdef USE_AIR_MOUSE
  esp_sleep_enable_ext0_wakeup(GPIO_NUM_6, LOW);
  Serial.println("[INFO]: Starting AirMouse");
  mpuWake();
#endif

  displaySetup(I2C_SDA, I2C_SCL);
}

bool mouseEnabled = false;
bool scrollEnabled = false;
bool dragEnabled = false;
bool oledAsleep = false;

// Encodes everything that affects what's drawn on screen so the display only
// repaints on a real change (full-buffer streaming over I²C is ~10 ms).
struct DisplayState {
  ConnState conn;
  Page page;
  bool scroll;
  bool drag;
  int sensitivity;
  int moveDelay;

  bool operator!=(const DisplayState &o) const {
    return conn != o.conn || page != o.page || scroll != o.scroll ||
           drag != o.drag || sensitivity != o.sensitivity ||
           moveDelay != o.moveDelay;
  }
};

void renderStatusScreen(ConnState s) {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_5x7_tr);

  if (s == ConnState::Connecting) {
    u8g2.drawStr(0, 7, "Connecting...");
    u8g2.setFont(u8g2_font_open_iconic_all_2x_t);
    u8g2.drawGlyph(24, 28, ICON_BLUETOOTH);
    u8g2.setFont(u8g2_font_5x7_tr);
    u8g2.drawStr(0, 40, "OK = pair new");
    u8g2.drawStr(0, 47, "A  = forget");
  } else { // Discoverable
    u8g2.drawStr(14, 7, "Pair me");
    u8g2.setFont(u8g2_font_open_iconic_all_2x_t);
    u8g2.drawGlyph(24, 30, ICON_BLUETOOTH);
    u8g2.setFont(u8g2_font_4x6_tr);
    u8g2.drawStr(0, 41, "MomoCoderGGKP");
  }

  u8g2.sendBuffer();
}

void renderPage(const DisplayState &s) {
  const PageDef &def = pageDefs[static_cast<int>(s.page)];
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_open_iconic_all_2x_t);

  // Page::Settings replaces the bottom row with a "S:NNN D:NN" overlay,
  // so suppress glyphs in slots 6..8.
  const int maxSlot = (s.page == Page::Settings) ? 5 : 8;

  for (uint8_t i = 0; i < def.count; ++i) {
    const Binding &b = def.bindings[i];
    if (b.icon == 0) continue;
    int slot = slotForButton(b.button);
    if (slot < 0 || slot > maxSlot) continue;
    int col = slot % 3;
    int row = slot / 3;
    u8g2.drawGlyph(col * 21, row * 16 + 16, b.icon);
  }

  if (s.page == Page::Settings) {
    u8g2.setFont(u8g2_font_5x7_tr);
    char buf[16];
    snprintf(buf, sizeof(buf), "S:%d D:%d", s.sensitivity, s.moveDelay);
    u8g2.drawStr(0, 46, buf);
  } else if (s.page == Page::Mouse && (s.scroll || s.drag)) {
    u8g2.setFont(u8g2_font_5x7_tr);
    if (s.scroll) u8g2.drawStr(0, 6, "S");
    if (s.drag) u8g2.drawStr(8, 6, "D");
  }

  u8g2.sendBuffer();
}

void printPage() {
  if (oledAsleep) return;
  static DisplayState last = {ConnState::Booting, Page::Mouse,
                              false, false, -1, -1};
  DisplayState now = {connState,       currentPage,
                      scrollEnabled,   dragEnabled,
                      mouseSensitivity, mouseMoveDelay};
  if (now != last) {
    last = now;
    if (connState == ConnState::Connected) {
      renderPage(now);
    } else if (connState == ConnState::Connecting ||
               connState == ConnState::Discoverable) {
      renderStatusScreen(connState);
    }
    // ConnState::Booting: leave the splash from displaySetup() in place.
  }
}

void loop(void) {
  updateConnState();

  mouseEnabled = (currentPage == Page::Mouse || currentPage == Page::Settings);

  static bool prevMouseEnabled = true;  // setup() already woke the MPU
  if (mouseEnabled != prevMouseEnabled) {
    if (mouseEnabled) {
      Serial.println("[AUTO] MPU6050 wake (mouse-enabled page)");
      mpuWake();
    } else {
      Serial.println("[AUTO] MPU6050 sleep (non-mouse page)");
      mpuSleep();
    }
    prevMouseEnabled = mouseEnabled;
  }

  if (pressedButton != -1 && oledAsleep) {
    // First press after OLED sleep just wakes the screen — swallow it so the
    // user doesn't accidentally trigger an action they couldn't see.
    Serial.print("[AUTO] OLED wake (button GPIO ");
    Serial.print(pressedButton);
    Serial.println(")");
    displaySetPowerSave(false);
    oledAsleep = false;
    delay(DEBOUNCE_MS);
    pressedButton = -1;
  } else if (pressedButton != -1) {
    if (connState == ConnState::Connected) {
      handleButtonPress(currentPage, pressedButton);
    } else {
      handleButtonPressDisconnected(pressedButton);
    }
    delay(DEBOUNCE_MS);

    Serial.print("Page: ");
    Serial.println(static_cast<int>(currentPage));
    Serial.print("mouseEnabled: ");
    Serial.println(mouseEnabled);
    Serial.print("MRefreshDelay: ");
    Serial.println(mouseMoveDelay);
    Serial.print("MSensitivity: ");
    Serial.println(mouseSensitivity);

    // Reset the pressedButton value
    pressedButton = -1;
  }
  if (bleCombo.isConnected()) {
    if (mouseEnabled) {
      while (i2cRead(0x3B, i2cData, 14))
        ;

      gyroX = ((i2cData[8] << 8) | i2cData[9]);
      gyroZ = ((i2cData[12] << 8) | i2cData[13]);

      gyroX = (gyroX + 2.5 * mouseSensitivity) / mouseSensitivity;
      gyroZ = gyroZ / mouseSensitivity;

      if (scrollEnabled && (gyroX || gyroZ)) {
        bleCombo.mouseMove(0, 0, gyroX, gyroZ);
        delay(SCROLL_PAUSE_MS);
      } else if (gyroX || gyroZ) {
        bleCombo.mouseMove(gyroZ, gyroX);
      }
      delay(mouseMoveDelay);
    }
  }
  // LED is fully driven by connState now; advertising restart is handled
  // by transitionTo()'s entry actions.
  updateLed(connState);

  if (!oledAsleep && connState != ConnState::Booting &&
      millis() - lastButtonPressTime >= OLED_IDLE_MS) {
    Serial.print("[AUTO] OLED power save (idle ");
    Serial.print(OLED_IDLE_MS);
    Serial.println(" ms)");
    displaySetPowerSave(true);
    oledAsleep = true;
  }

  if (connState != ConnState::Booting &&
      millis() - lastButtonPressTime >= idleTimeoutMs(connState)) {
    Serial.print("[AUTO] Deep sleep (idle ");
    Serial.print(idleTimeoutMs(connState));
    Serial.println(" ms), waiting for ext0 wake on GPIO6");
    delay(DEEP_SLEEP_HOLD_MS);
    esp_deep_sleep_start();
  }

  printPage();
}
