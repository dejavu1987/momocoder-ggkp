#include "AirMouse.h"
#include <Wire.h>

int16_t gyroX, gyroZ;
uint8_t i2cData[14];

uint8_t i2cWrite(uint8_t registerAddress, uint8_t *data, uint8_t length,
                 bool sendStop) {
  Wire.beginTransmission(IMUAddress);
  Wire.write(registerAddress);
  Wire.write(data, length);
  return Wire.endTransmission(sendStop);
}

uint8_t i2cWrite2(uint8_t registerAddress, uint8_t data, bool sendStop) {
  return i2cWrite(registerAddress, &data, 1, sendStop);
}

// PWR_MGMT_1 (0x6B): clear all bits to wake; set bit 6 (0x40) to sleep.
// Sleep drops the MPU6050 from ~3.9 mA to ~5 µA.
void mpuWake()  { i2cWrite2(0x6B, 0x00, true); }
void mpuSleep() { i2cWrite2(0x6B, 0x40, true); }

uint8_t i2cRead(uint8_t registerAddress, uint8_t *data, uint8_t nbytes) {
  uint32_t timeOutTimer;
  Wire.beginTransmission(IMUAddress);
  Wire.write(registerAddress);
  if (Wire.endTransmission(false))
    return 1;
  Wire.requestFrom(IMUAddress, nbytes, (uint8_t) true);
  for (uint8_t i = 0; i < nbytes; i++) {
    if (Wire.available()) {
      data[i] = Wire.read();
    } else {
      timeOutTimer = micros();
      while (((micros() - timeOutTimer) < I2C_TIMEOUT) && !Wire.available())
        ;
      if (Wire.available())
        data[i] = Wire.read();
      else
        return 2;
    }
  }
  return 0;
}
