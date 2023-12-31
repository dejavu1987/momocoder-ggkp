
#include <BLECombo.h>
#include <keyvals.cpp>
BLECombo bleCombo("MomoCoderGGKP");

#define USE_NIMBLE

#include "NimBLEBeacon.h" // Additional BLE functionaity using NimBLE
#include "NimBLEDevice.h" // Additional BLE functionaity using NimBLE
#include "NimBLEUtils.h"  // Additional BLE functionaity using NimBLE

#include "esp_bt_device.h" // Additional BLE functionaity
#include "esp_bt_main.h"   // Additional BLE functionaity
#include "esp_sleep.h"     // Additional BLE functionaity

#define USE_AIR_MOUSE

// #include <ArduinoJson.h> // Using ArduinoJson to read and write config files

// #include <WiFi.h> // Wifi support

// #include <AsyncTCP.h>          //Async Webserver support header
// #include <ESPAsyncWebServer.h> //Async Webserver support header

// #include <ESPmDNS.h> // DNS functionality

// AsyncWebServer webserver(80);

#define I2C_GYRO_SCL 1
#define I2C_GYRO_SDA 2
#define MOUSE_SENSITIVITY 200
#include "AirMouse.h"

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

// Create an array of button names
const int buttonNames[NUM_BUTTONS] = {BTN_LT, BTN_RT, BTN_UP, BTN_DN, BTN_A,
                                      BTN_B,  BTN_C,  BTN_D,  BTN_OK};

int page = 0;

volatile int pressedButton = -1;

void buttonInterrupt() {
  // Check the state of each button to determine which button was pressed
  for (int i = 0; i < NUM_BUTTONS; i++) {
    if (digitalRead(buttonNames[i]) == LOW) {
      pressedButton = buttonNames[i];
      break; // Exit the loop when a button is found
    }
  }
}

/***
 * Pages of icons
 * This should be stored in flash memory
 */
uint16_t pages[3][9] = {{144, 119, 154, 117, 115, 118, 212, 116, 212},
                        {144, 215, 277, 213, 211, 214, 279, 216, 278},
                        {144, 119, 154, 117, 96, 118, 212, 116, 212}};

void setup(void) {
  Serial.begin(115200);
  delay(2000);
  Serial.println("[INFO]: Serial initialized");

  // Configure buttons as INPUT_PULLUP and attach interrupts
  for (int i = 0; i < NUM_BUTTONS; i++) {
    pinMode(buttonNames[i], INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(buttonNames[i]), buttonInterrupt,
                    FALLING);
  }
  Serial.println("[INFO]: Keypad initialized");

  bleCombo.begin();
  Serial.println("[INFO]: Starting BLE");

#ifdef USE_AIR_MOUSE
  Serial.println("[INFO]: Starting AirMouse");
  Wire.begin(I2C_GYRO_SDA, I2C_GYRO_SCL, 100000);

  i2cData[0] = 7;
  i2cData[1] = 0x00;
  i2cData[3] = 0x00;

  while (i2cWrite(0x19, i2cData, 4, false))
    ;
  while (i2cWrite2(0x6B, 0x01, true))
    ;
  while (i2cRead(0x75, i2cData, 1))
    ;
  delay(100);
  while (i2cRead(0x3B, i2cData, 6))
    ;

#endif
}

bool mouseEnabled = false;

void loop(void) {

  if (page == 0) {
    mouseEnabled = true;
  } else {
    mouseEnabled = false;
  }

  if (pressedButton != -1) {
    // A button was pressed; do something based on the pressedButton value
    switch (pressedButton) {
    case BTN_LT:
      // Handle BTN_LT press
      bleCombo.mouseClick(1);
      break;
    case BTN_RT:
      // Handle BTN_RT press
      break;
    case BTN_UP:
      // Handle BTN_UP press
      page--;
      Serial.println("UP ^");
      break;
    case BTN_DN:
      // Handle BTN_DN press
      page++;
      Serial.println("DN v");
      break;
    case BTN_A:
      // Handle BTN_A press
      break;
    case BTN_B:
      // Handle BTN_B press
      break;
    case BTN_C:
      bleCombo.mousePress(1);
      // Handle BTN_C press
      break;
    case BTN_D:
      bleCombo.mouseRelease(1);
      // Handle BTN_D press
      break;
    case BTN_OK:
      // Handle BTN_OK press
      bleCombo.mouseClick(2);
      break;
    }
    // debounce
    delay(200);

    // cycle pages
    if (page < 0)
      page = 2;
    else if (page > 2)
      page = 0;

    Serial.print("Page: ");
    Serial.println(page);
    Serial.print("mouseEnabled: ");
    Serial.println(mouseEnabled);

    // Reset the pressedButton value
    pressedButton = -1;
  }
  if (bleCombo.isConnected()) {
    if (mouseEnabled) {
      while (i2cRead(0x3B, i2cData, 14))
        ;

      gyroX = ((i2cData[8] << 8) | i2cData[9]);
      gyroY = ((i2cData[10] << 8) | i2cData[11]);
      gyroZ = ((i2cData[12] << 8) | i2cData[13]);

      gyroX = gyroX / MOUSE_SENSITIVITY;
      gyroY = gyroY / MOUSE_SENSITIVITY;
      gyroZ = gyroZ / MOUSE_SENSITIVITY;

      if (bleCombo.isConnected()) {
        Serial.print(gyroZ);
        Serial.print(" ,  ");
        Serial.print(gyroX);
        Serial.print("\r\n");
        bleCombo.mouseMove(gyroZ, gyroX + 3);
      }
      delay(20);
    }
  } else {
    Serial.println("Waiting 5s for Bluetooth connection...");
    delay(5000);
  }
}
