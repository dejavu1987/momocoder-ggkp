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

#define I2C_GYRO_SCL 1
#define I2C_GYRO_SDA 2

#include "Display.h"
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

int mouseSensitivity = 300;
int mouseMoveDelay = 5;

/***
 * Pages of icons
 * This should be stored in flash memory
 */
uint16_t pages[4][9] = {{144, 119, 154, 117, 115, 118, 212, 116, 212},
                        {144, 215, 277, 213, 211, 214, 279, 216, 278},
                        {144, 119, 154, 117, 96, 118, 212, 116, 212},
                        {144, 119, 154, 117, 247, 118, 212, 116, 212}};

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
    unsigned long deadline = millis() + 1000;
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
  Serial.println("[INFO]: Starting BLE");
  Wire.begin(I2C_GYRO_SDA, I2C_GYRO_SCL);

#ifdef USE_AIR_MOUSE
  esp_sleep_enable_ext0_wakeup(GPIO_NUM_6, LOW);
  Serial.println("[INFO]: Starting AirMouse");
  // Wake the MPU6050 by clearing the SLEEP bit in PWR_MGMT_1 (0x6B).
  i2cWrite2(0x6B, 0x00, true);
#endif

  displaySetup(I2C_GYRO_SDA, I2C_GYRO_SCL);
}

bool mouseEnabled = false;
bool scrollEnabled = false;
bool dragEnabled = false;

/**
 * @brief Function for printing 3x3 icon menu
 */
void printPage(int page) {
  static int lastPage = -1;
  if (page == lastPage) return;
  lastPage = page;

  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_open_iconic_all_2x_t);

  int x = 0;
  int y = 16;
  for (int row = 0; row < 3; row++) {
    for (int col = 0; col < 3; col++) {
      u8g2.drawGlyph(x, y, pages[page][row * 3 + col]);
      x += 21;
    }
    x = 0;
    y += 16;
  }
  u8g2.sendBuffer();
}

void loop(void) {

  if (KEYPAD_PAGE == 0 || KEYPAD_PAGE == 2) {
    mouseEnabled = true;
  } else {
    mouseEnabled = false;
  }

  if (pressedButton != -1) {
    handleButtonPress(KEYPAD_PAGE, pressedButton);

    // debounce
    delay(200);

    // cycle pages
    if (KEYPAD_PAGE < 0)
      KEYPAD_PAGE = 3;
    else if (KEYPAD_PAGE > 3)
      KEYPAD_PAGE = 0;

    Serial.print("Page: ");
    Serial.println(KEYPAD_PAGE);
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
    analogWrite(B_PIN, 0);
    analogWrite(R_PIN, 0);
    analogWrite(G_PIN, 55);

    if (mouseEnabled) {
      while (i2cRead(0x3B, i2cData, 14))
        ;

      gyroX = ((i2cData[8] << 8) | i2cData[9]);
      gyroZ = ((i2cData[12] << 8) | i2cData[13]);

      gyroX = (gyroX + 2.5 * mouseSensitivity) / mouseSensitivity;
      gyroZ = gyroZ / mouseSensitivity;

      if (scrollEnabled && (gyroX || gyroZ)) {
        bleCombo.mouseMove(0, 0, gyroX, gyroZ);
        delay(50);
      } else if (gyroX || gyroZ) {
        bleCombo.mouseMove(gyroZ, gyroX);
      }
      delay(mouseMoveDelay);
    }
    // Sleep after 60 s of no button press
    if (millis() - lastButtonPressTime >= 60000UL) {
      Serial.println("Going to sleep...");
      delay(2000);
      esp_deep_sleep_start();
    }
  } else if (pairingMode) {
    static unsigned long lastBlink = 0;
    static bool ledOn = false;
    if (millis() - lastBlink >= 150) {
      ledOn = !ledOn;
      analogWrite(R_PIN, 0);
      analogWrite(G_PIN, 0);
      analogWrite(B_PIN, ledOn ? 80 : 0);
      lastBlink = millis();
    }
  } else {
    Serial.println("Waiting 5s for Bluetooth connection...");
    // Set the LED to green with varying intensity
    analogWrite(B_PIN, 0);  // Turn off red
    analogWrite(R_PIN, 40); // Set green to maximum intensity
    analogWrite(G_PIN, 0);  // Turn off blue

    // Add a delay to keep the LED green for a while
    delay(1000); // Adjust the delay duration as needed

    // Turn off the LED
    analogWrite(B_PIN, 0);
    analogWrite(R_PIN, 0);
    analogWrite(G_PIN, 30);

    // Add a delay to keep the LED green for a while
    delay(1000); // Adjust the delay duration as needed

    // Turn off the LED
    analogWrite(B_PIN, 20);
    analogWrite(R_PIN, 0);
    analogWrite(G_PIN, 0);

    // Add a delay before repeating the process
    delay(1000); // Adjust the delay duration as needed
  }

  printPage(KEYPAD_PAGE);
}
