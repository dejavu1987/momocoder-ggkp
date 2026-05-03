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
constexpr unsigned long IDLE_SLEEP_MS = 60000UL;
constexpr unsigned long PAIRING_BLINK_MS = 150;
constexpr unsigned long DISCONNECT_TIMEOUT_MS = 1000;
constexpr unsigned long DEEP_SLEEP_HOLD_MS = 2000;
constexpr unsigned long SCROLL_PAUSE_MS = 50;

// LED brightness presets (0-255 PWM duty)
constexpr uint8_t LED_OFF = 0;
constexpr uint8_t LED_CONNECTED = 55;
constexpr uint8_t LED_PAIRING_BLINK = 80;
constexpr uint8_t LED_WAIT_R = 40;
constexpr uint8_t LED_WAIT_G = 30;
constexpr uint8_t LED_WAIT_B = 20;

int mouseSensitivity = 300;
int mouseMoveDelay = 5;

void setLed(uint8_t r, uint8_t g, uint8_t b) {
  analogWrite(R_PIN, r);
  analogWrite(G_PIN, g);
  analogWrite(B_PIN, b);
}

// 3x3 icon layout per page. Glyphs are from u8g2_font_open_iconic_all_2x_t;
// see Icons.h for the full name table.
//
// Position layout (matches the physical keypad):
//   A  | UP | B
//   LT | OK | RT
//   C  | DN | D
const uint16_t pages[NUM_PAGES][ICONS_PER_PAGE] = {
    // Page::Mouse — air mouse with click bindings.
    //  ESC          UP→nav         scroll-toggle
    //  L-click      R-click(menu)  browser-back
    //  drag-toggle  DN→nav         browser-forward
    {ICON_CIRCLE_X,  ICON_CHEVRON_TOP,    ICON_LOOP,
     ICON_TARGET,    ICON_MENU,           ICON_ACTION_UNDO,
     ICON_MOVE,      ICON_CHEVRON_BOTTOM, ICON_ACTION_REDO},

    // Page::Media — keyboard / media keys.
    //  ESC        UP→nav   fullscreen
    //  prev       play     next
    //  vol+       DN→nav   vol-
    {ICON_CIRCLE_X,            ICON_CHEVRON_TOP,    ICON_FULLSCREEN_ENTER,
     ICON_MEDIA_SKIP_BACKWARD, ICON_MEDIA_PLAY,     ICON_MEDIA_SKIP_FORWARD,
     ICON_VOLUME_HIGH,         ICON_CHEVRON_BOTTOM, ICON_VOLUME_LOW},

    // Page::Settings — air-mouse tuning. The bottom row is replaced by a
    // live "S:NNN D:NN" overlay in renderPage(), so those glyphs aren't shown.
    //  ESC        UP→nav    fullscreen
    //  sens-      play      sens+
    //  delay-     DN→nav    delay+
    {ICON_CIRCLE_X,  ICON_CHEVRON_TOP,    ICON_FULLSCREEN_ENTER,
     ICON_MINUS,     ICON_MEDIA_PLAY,     ICON_PLUS,
     ICON_BOLT,      ICON_CHEVRON_BOTTOM, ICON_TIMER},

    // Page::Pairing — only OK/UP/DN do anything; ICON_BAN flags the
    // inactive slots so the user knows nothing else fires.
    {ICON_BAN,       ICON_CHEVRON_TOP,    ICON_BAN,
     ICON_BAN,       ICON_BLUETOOTH,      ICON_BAN,
     ICON_BAN,       ICON_CHEVRON_BOTTOM, ICON_BAN}};

bool pairingMode = false;

void enterPairingMode() {
  pairingMode = true;
  Serial.println("[INFO]: Entering BLE pairing mode");

  auto *server = NimBLEDevice::getServer();
  if (server && server->getConnectedCount() > 0) {
    auto info = server->getPeerInfo(0);
    server->disconnect(info.getConnHandle());
    // disconnect is async — block briefly so the loop's isConnected check
    // doesn't immediately clear pairingMode before the peer is actually gone.
    unsigned long deadline = millis() + DISCONNECT_TIMEOUT_MS;
    while (server->getConnectedCount() > 0 && millis() < deadline) {
      delay(20);
    }
  }
  NimBLEDevice::deleteAllBonds();
  NimBLEDevice::startAdvertising();
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
  // Wake the MPU6050 by clearing the SLEEP bit in PWR_MGMT_1 (0x6B).
  i2cWrite2(0x6B, 0x00, true);
#endif

  displaySetup(I2C_SDA, I2C_SCL);
}

bool mouseEnabled = false;
bool scrollEnabled = false;
bool dragEnabled = false;

// Encodes everything that affects what's drawn on screen so the display only
// repaints on a real change (full-buffer streaming over I²C is ~10 ms).
struct DisplayState {
  Page page;
  bool scroll;
  bool drag;
  bool pairing;
  int sensitivity;
  int moveDelay;

  bool operator!=(const DisplayState &o) const {
    return page != o.page || scroll != o.scroll || drag != o.drag ||
           pairing != o.pairing || sensitivity != o.sensitivity ||
           moveDelay != o.moveDelay;
  }
};

void renderPage(const DisplayState &s) {
  const int p = static_cast<int>(s.page);
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_open_iconic_all_2x_t);

  // Page 2 (Settings) replaces the bottom row with live values; the top two
  // rows of icons stay as a button-mapping reminder.
  const int rows = (s.page == Page::Settings) ? 2 : 3;
  int y = 16;
  for (int row = 0; row < rows; row++) {
    int x = 0;
    for (int col = 0; col < 3; col++) {
      u8g2.drawGlyph(x, y, pages[p][row * 3 + col]);
      x += 21;
    }
    y += 16;
  }

  if (s.page == Page::Settings) {
    u8g2.setFont(u8g2_font_5x7_tr);
    char buf[16];
    snprintf(buf, sizeof(buf), "S:%d D:%d", s.sensitivity, s.moveDelay);
    u8g2.drawStr(0, 46, buf);
  } else if (s.page == Page::Mouse && (s.scroll || s.drag)) {
    // Tiny status letter in the corner to show toggle state without
    // sacrificing an icon position.
    u8g2.setFont(u8g2_font_5x7_tr);
    if (s.scroll) u8g2.drawStr(0, 6, "S");
    if (s.drag) u8g2.drawStr(8, 6, "D");
  } else if (s.page == Page::Pairing && s.pairing) {
    u8g2.setFont(u8g2_font_5x7_tr);
    u8g2.drawStr(20, 46, "PAIR");
  }

  u8g2.sendBuffer();
}

void printPage() {
  static DisplayState last = {Page::Mouse, false, false, false, -1, -1};
  DisplayState now = {currentPage,      scrollEnabled,    dragEnabled,
                      pairingMode,      mouseSensitivity, mouseMoveDelay};
  if (now != last) {
    last = now;
    renderPage(now);
  }
}

void loop(void) {

  mouseEnabled = (currentPage == Page::Mouse || currentPage == Page::Settings);

  if (pressedButton != -1) {
    handleButtonPress(currentPage, pressedButton);
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
    if (pairingMode) {
      pairingMode = false;
      Serial.println("[INFO]: Paired, exiting pairing mode");
    }
    setLed(LED_OFF, LED_CONNECTED, LED_OFF);

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
    if (millis() - lastButtonPressTime >= IDLE_SLEEP_MS) {
      Serial.println("Going to sleep...");
      delay(DEEP_SLEEP_HOLD_MS);
      esp_deep_sleep_start();
    }
  } else if (pairingMode) {
    static unsigned long lastBlink = 0;
    static bool ledOn = false;
    if (millis() - lastBlink >= PAIRING_BLINK_MS) {
      ledOn = !ledOn;
      setLed(LED_OFF, LED_OFF, ledOn ? LED_PAIRING_BLINK : LED_OFF);
      lastBlink = millis();
    }
  } else {
    Serial.println("Waiting for Bluetooth connection...");
    setLed(LED_WAIT_R, LED_OFF, LED_OFF);
    delay(1000);
    setLed(LED_OFF, LED_WAIT_G, LED_OFF);
    delay(1000);
    setLed(LED_OFF, LED_OFF, LED_WAIT_B);
    delay(1000);
  }

  printPage();
}
