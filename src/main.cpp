#include <BLECombo.h>
#include <Wire.h>
#include <keyvals.cpp>

#include "globals.h"

BLECombo bleCombo("MomoCoderGGKP");

#define B_PIN 3
#define R_PIN 45
#define G_PIN 47

#define USE_NIMBLE

#include <NimBLEBeacon.h> // Additional BLE functionaity using NimBLE
#include <NimBLEDevice.h> // Additional BLE functionaity using NimBLE
#include <NimBLEUtils.h>  // Additional BLE functionaity using NimBLE

#include <esp_bt_device.h> // Additional BLE functionaity
#include <esp_bt_main.h>   // Additional BLE functionaity
#include <esp_sleep.h>     // Additional BLE functionaity

#include "Display.h"
#include "Keypad.h"

#define USE_AIR_MOUSE

#ifdef USE_AIR_MOUSE
#define I2C_GYRO_SCL 1
#define I2C_GYRO_SDA 2
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
uint16_t pages[3][9] = {{144, 119, 154, 117, 115, 118, 212, 116, 212},
                        {144, 215, 277, 213, 211, 214, 279, 216, 278},
                        {144, 119, 154, 117, 96, 118, 212, 116, 212}};

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
  const int MPU = 0x68; // MPU6050 I2C address
  esp_sleep_enable_ext0_wakeup(GPIO_NUM_6, LOW);
  Serial.println("[INFO]: Starting AirMouse");
  // Initialize comunication
  Wire.beginTransmission(MPU); // Start communication with MPU6050 // MPU=0x68
  Wire.write(0x6B);            // Talk to the register 6B
  Wire.write(0x00);            // Make reset - place a 0 into the 6B register
  Wire.endTransmission(true);
#endif

  // displaySetup();
}

bool mouseEnabled = false;
bool scrollEnabled = false;
bool dragEnabled = false;

/**
 * @brief Function for printing 3x3 icon menu
 */
void printPage(int page) {
  int x = 0;
  int y = 16;

  for (int row = 0; row < 3; row++) {
    for (int col = 0; col < 3; col++) {
      // u8g2.drawGlyph(x, y, pages[page][row * 3 + col]);
      // Implement some drawing functions
      x += 21; // Increment the x-coordinate for the next column
    }
    x = 0;   // Reset x-coordinate for the next row
    y += 16; // Increment the y-coordinate for the next row
  }
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
      KEYPAD_PAGE = 2;
    else if (KEYPAD_PAGE > 2)
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
    analogWrite(B_PIN, 0);
    analogWrite(R_PIN, 0);
    analogWrite(G_PIN, 55);

    if (mouseEnabled) {
      while (i2cRead(0x3B, i2cData, 14))
        ;

      gyroX = ((i2cData[8] << 8) | i2cData[9]);
      gyroY = ((i2cData[10] << 8) | i2cData[11]);
      gyroZ = ((i2cData[12] << 8) | i2cData[13]);

      gyroX = (gyroX + 2.5 * mouseSensitivity) / mouseSensitivity;
      gyroY = gyroY / mouseSensitivity;
      gyroZ = gyroZ / mouseSensitivity;

      if (bleCombo.isConnected()) {
        if (scrollEnabled && (gyroX || gyroZ)) {
          bleCombo.mouseMove(0, 0, gyroX, gyroZ);
          delay(50);
        } else {
          if (gyroX || gyroZ) {
            bleCombo.mouseMove(gyroZ, gyroX);
          }
        }
      }
      delay(mouseMoveDelay);
    }
    // Check if 30 seconds have passed since the last button press
    if (millis() - lastButtonPressTime >= 60000UL) {
      Serial.println("Going to sleep...");
      delay(2000);
      esp_deep_sleep_start();
    }

    printPage(KEYPAD_PAGE);
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
}
