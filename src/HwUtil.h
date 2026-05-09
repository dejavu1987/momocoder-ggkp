#ifndef HWUTIL_H
#define HWUTIL_H

#include <stdint.h>

// Stateful CPU clock bump. cpuBumpTo() raises the clock to targetMhz if it's
// currently below, remembering the original; cpuRestore() puts it back.
// Callers are responsible for pairing the calls — only one bump can be in
// flight at a time, which is fine because the firmware is single-threaded
// and the only callers (Wi-Fi remote + Wi-Fi setup) never overlap. The tag
// prefixes serial logs so you can tell callers apart in a trace.
void cpuBumpTo(uint32_t targetMhz, const char* tag);
void cpuRestore(const char* tag);

// STA shutdown: drop association and power down the radio. Both calls end up
// in esp_wifi_deinit(), which logs a cosmetic "wifi:timeout when WiFi un-init,
// type=4" on back-to-back invocations — every request still completes.
// Bypassing via raw esp_wifi_* wedges arduino-esp32's state machine
// (NO_SHIELD on the next associate); the arduino-managed path is the only
// one WiFi.begin() recovers from cleanly.
void wifiPowerOff();

#endif // HWUTIL_H
