#include "WifiRemote.h"
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_wifi.h>

// SSID/password come from wifi_secrets.ini (gitignored) via build_flags
// interpolation in platformio.ini. The placeholders here are only hit if
// that file is missing — Serial logs will show "YOUR_SSID" associate fail.
#ifndef WIFI_SSID
#define WIFI_SSID     "YOUR_SSID"
#endif
#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD "YOUR_PASSWORD"
#endif
#ifndef WIFI_CHANNEL
#define WIFI_CHANNEL  6
#endif
#ifndef DEVICE_TOKEN
#define DEVICE_TOKEN  "YOUR_DEVICE_TOKEN"
#endif

// AP MAC. Skipping the scan needs both channel + BSSID; without these
// WiFi.begin() spends ~1.5-2 s scanning every channel before associating.
// f8:d2:ac:6f:cc:18 = AbiJamun 2.4 GHz radio (the 5 GHz radio at ...:20 and
// the LAN MAC at 02:10:18:... are different — must be the 2.4 GHz BSSID).
static uint8_t WIFI_BSSID[6] = { 0xF8, 0xD2, 0xAC, 0x6F, 0xCC, 0x18 };

static const char*    HOST    = "momoggkp.vercel.app";
static const uint16_t PORT    = 443;
static const unsigned long ASSOCIATE_TIMEOUT_MS = 5000;
static const unsigned long HTTP_READ_TIMEOUT_MS = 4000;

// DNS note: with HTTPS we connect by hostname (so TLS SNI is set — vercel
// routes by SNI). Arduino's WiFiClientSecure has no public setSNIHostname,
// so caching an IP and using connect(IPAddress,port) breaks vercel routing.
// Internally lwIP caches DNS within an associated session anyway, so the
// per-press cost is one resolve at most.

static const char *wifiStatusName(wl_status_t s) {
  switch (s) {
    case WL_IDLE_STATUS:      return "IDLE";
    case WL_NO_SSID_AVAIL:    return "NO_SSID";
    case WL_SCAN_COMPLETED:   return "SCAN_DONE";
    case WL_CONNECTED:        return "CONNECTED";
    case WL_CONNECT_FAILED:   return "CONNECT_FAILED";
    case WL_CONNECTION_LOST:  return "CONNECTION_LOST";
    case WL_DISCONNECTED:     return "DISCONNECTED";
    case WL_NO_SHIELD:        return "NO_SHIELD";
    default:                  return "?";
  }
}

static bool associate() {
  unsigned long t0 = millis();
  Serial.printf("[WIFI] associate begin ssid=\"%s\" ch=%d "
                "bssid=%02X:%02X:%02X:%02X:%02X:%02X timeout=%lums\n",
                WIFI_SSID, WIFI_CHANNEL,
                WIFI_BSSID[0], WIFI_BSSID[1], WIFI_BSSID[2],
                WIFI_BSSID[3], WIFI_BSSID[4], WIFI_BSSID[5],
                ASSOCIATE_TIMEOUT_MS);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD, WIFI_CHANNEL, WIFI_BSSID);
  unsigned long deadline = millis() + ASSOCIATE_TIMEOUT_MS;
  wl_status_t lastStatus = (wl_status_t)-1;
  while (WiFi.status() != WL_CONNECTED && millis() < deadline) {
    wl_status_t s = WiFi.status();
    if (s != lastStatus) {
      Serial.printf("[WIFI]   status %s (t+%lums)\n",
                    wifiStatusName(s), millis() - t0);
      lastStatus = s;
    }
    delay(50);
  }
  if (WiFi.status() != WL_CONNECTED) {
    Serial.printf("[WIFI] associate timeout after %lums (status=%s)\n",
                  millis() - t0, wifiStatusName(WiFi.status()));
    return false;
  }
  Serial.printf("[WIFI] associated in %lums IP=%s RSSI=%d ch=%d\n",
                millis() - t0,
                WiFi.localIP().toString().c_str(),
                WiFi.RSSI(), WiFi.channel());
  return true;
}

static void teardown() {
  unsigned long t0 = millis();
  Serial.println("[WIFI] teardown begin");
  // Both arduino calls below end up in esp_wifi_deinit(), which races on
  // back-to-back presses and logs "wifi:timeout when WiFi un-init, type=4"
  // async — cosmetic, every request still completes. We tried bypassing
  // via raw esp_wifi_disconnect()+stop() to silence it, but that wedged
  // arduino's internal WiFi state machine and broke the next associate
  // (NO_SHIELD). The arduino-managed path is the only one WiFi.begin()
  // recovers from cleanly.
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  Serial.printf("[WIFI] teardown done in %lums\n", millis() - t0);
}

// Raw HTTPS/1.1 GET. setInsecure() skips cert verification — fine for a
// personal device and saves the ~10 KB CA bundle in flash. SNI is set
// from the hostname passed to connect(), which vercel needs for routing.
// Returns the HTTP status code, or <=0 on transport failure.
static int doGet(const char* buttonName) {
  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(HTTP_READ_TIMEOUT_MS / 1000);
  Serial.printf("[WIFI] GET https://%s:%u/buttonPress/%s\n",
                HOST, PORT, buttonName);

  unsigned long tConnect = millis();
  if (!client.connect(HOST, PORT)) {
    Serial.printf("[WIFI] tls connect failed after %lums\n",
                  millis() - tConnect);
    return -1;
  }
  Serial.printf("[WIFI] tls connected in %lums\n", millis() - tConnect);

  unsigned long tWrite = millis();
  client.print("GET /buttonPress/");
  client.print(buttonName);
  client.print(" HTTP/1.1\r\nHost: ");
  client.print(HOST);
  client.print("\r\nUser-Agent: GGKP/1\r\nAuthorization: Bearer ");
  client.print(DEVICE_TOKEN);
  client.print("\r\nConnection: close\r\n\r\n");
  Serial.printf("[WIFI] request sent (%lums)\n", millis() - tWrite);

  unsigned long deadline = millis() + HTTP_READ_TIMEOUT_MS;
  unsigned long tWait = millis();
  while (!client.available() && client.connected() && millis() < deadline) {
    delay(10);
  }
  if (!client.available()) {
    Serial.printf("[WIFI] http read timeout after %lums (connected=%d)\n",
                  millis() - tWait, client.connected());
    client.stop();
    return -2;
  }
  Serial.printf("[WIFI] response head ready in %lums\n", millis() - tWait);

  String statusLine = client.readStringUntil('\n');
  statusLine.trim();
  Serial.printf("[WIFI] status line: \"%s\"\n", statusLine.c_str());

  // Drain remaining bytes for clean TLS shutdown; count for visibility.
  size_t bodyBytes = 0;
  while (client.available()) {
    client.read();
    bodyBytes++;
  }
  client.stop();
  if (bodyBytes) Serial.printf("[WIFI] drained %u trailing bytes\n",
                               (unsigned)bodyBytes);

  // "HTTP/1.1 200 OK"
  int sp = statusLine.indexOf(' ');
  int code = (sp > 0) ? statusLine.substring(sp + 1, sp + 4).toInt() : -3;
  Serial.printf("[WIFI] GET /buttonPress/%s -> %d\n", buttonName, code);
  return code;
}

void wifiRemoteFire(const char* buttonName) {
  if (!buttonName) {
    Serial.println("[WIFI] fire: null button name, skip");
    return;
  }
  unsigned long t0 = millis();
  Serial.printf("[WIFI] === fire button=\"%s\" ===\n", buttonName);

  // Loud warning if the placeholders are still in the binary — easy first-
  // boot footgun to overlook in a flood of WL_DISCONNECTED messages.
  if (strcmp(WIFI_SSID, "YOUR_SSID") == 0) {
    Serial.println("[WIFI] WARNING: WIFI_SSID is still the placeholder; "
                   "wifi_secrets.ini missing or build_flags not interpolated");
  }

  // Bump CPU to 240 MHz for the duration of this request. The TLS handshake
  // is mbedTLS-bound (ECDHE + ECDSA verify) and roughly scales inverse to
  // clock; expect ~750ms vs ~2200ms at 80 MHz. Scoped here only so other
  // pages keep their 80 MHz idle profile. Restored before returning.
  uint32_t origMhz = getCpuFrequencyMhz();
  if (origMhz < 240) {
    setCpuFrequencyMhz(240);
    Serial.printf("[WIFI] cpu %u -> 240 MHz for handshake\n",
                  (unsigned)origMhz);
  }

  if (!associate()) {
    Serial.println("[WIFI] aborting: associate failed");
    teardown();
    if (origMhz < 240) setCpuFrequencyMhz(origMhz);
    Serial.printf("[WIFI] === fire end (%lums, no request) ===\n",
                  millis() - t0);
    return;
  }

  int code = doGet(buttonName);
  // One retry on transport failure (TLS hiccup, transient network drop).
  if (code <= 0) {
    Serial.printf("[WIFI] retry: previous attempt returned %d\n", code);
    code = doGet(buttonName);
  }

  teardown();
  if (origMhz < 240) {
    setCpuFrequencyMhz(origMhz);
    Serial.printf("[WIFI] cpu restored to %u MHz\n", (unsigned)origMhz);
  }
  Serial.printf("[WIFI] === fire end (%lums, final code=%d) ===\n",
                millis() - t0, code);
}
