#include "HwUtil.h"
#include <Arduino.h>
#include <WiFi.h>

static uint32_t cpuBoostOrigMhz = 0;
static uint32_t cpuBoostTargetMhz = 0;

void cpuBumpTo(uint32_t targetMhz, const char* tag) {
  cpuBoostOrigMhz = getCpuFrequencyMhz();
  cpuBoostTargetMhz = targetMhz;
  if (cpuBoostOrigMhz < targetMhz) {
    setCpuFrequencyMhz(targetMhz);
    Serial.printf("[%s] cpu %u -> %u MHz\n", tag,
                  (unsigned)cpuBoostOrigMhz, (unsigned)targetMhz);
  }
}

void cpuRestore(const char* tag) {
  if (cpuBoostOrigMhz && cpuBoostOrigMhz < cpuBoostTargetMhz) {
    setCpuFrequencyMhz(cpuBoostOrigMhz);
    Serial.printf("[%s] cpu restored to %u MHz\n", tag,
                  (unsigned)cpuBoostOrigMhz);
  }
  cpuBoostOrigMhz = 0;
  cpuBoostTargetMhz = 0;
}

void wifiPowerOff() {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
}
