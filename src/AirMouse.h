#ifndef AIRMOUSE_H
#define AIRMOUSE_H

#include <stdint.h>

extern int16_t gyroX, gyroZ;
extern uint8_t i2cData[14];

constexpr uint8_t IMUAddress = 0x68;
constexpr uint16_t I2C_TIMEOUT = 1000;

uint8_t i2cWrite(uint8_t registerAddress, uint8_t *data, uint8_t length,
                 bool sendStop);
uint8_t i2cWrite2(uint8_t registerAddress, uint8_t data, bool sendStop);
uint8_t i2cRead(uint8_t registerAddress, uint8_t *data, uint8_t nbytes);

#endif // AIRMOUSE_H
