#include "WifiRemote.h"
#include <Arduino.h>
#include <WiFi.h>

// Network creds. Override via -D in platformio.ini if you don't want them
// committed (e.g. -DWIFI_SSID=\"foo\" -DWIFI_PASSWORD=\"bar\").
#ifndef WIFI_SSID
#define WIFI_SSID     "YOUR_SSID"
#endif
#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD "YOUR_PASSWORD"
#endif
#ifndef WIFI_CHANNEL
#define WIFI_CHANNEL  6
#endif

// AP MAC. Skipping the scan needs both channel + BSSID; without these
// WiFi.begin() spends ~1.5-2 s scanning every channel before associating.
// Look it up while connected:  arp -an | grep <router-ip>
static uint8_t WIFI_BSSID[6] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

static const char*    HOST    = "momoggkp.vercel.app";
static const uint16_t PORT    = 80;
static const unsigned long ASSOCIATE_TIMEOUT_MS = 5000;
static const unsigned long HTTP_READ_TIMEOUT_MS = 4000;

static IPAddress cachedIp;
static bool      cachedIpValid = false;

static bool ensureCachedIp() {
  if (cachedIpValid) return true;
  IPAddress ip;
  if (!WiFi.hostByName(HOST, ip)) {
    Serial.printf("[WIFI] DNS lookup for %s failed\n", HOST);
    return false;
  }
  cachedIp = ip;
  cachedIpValid = true;
  Serial.printf("[WIFI] DNS cached: %s -> %s\n",
                HOST, ip.toString().c_str());
  return true;
}

static bool associate() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD, WIFI_CHANNEL, WIFI_BSSID);
  unsigned long deadline = millis() + ASSOCIATE_TIMEOUT_MS;
  while (WiFi.status() != WL_CONNECTED && millis() < deadline) {
    delay(50);
  }
  if (WiFi.status() != WL_CONNECTED) {
    Serial.printf("[WIFI] associate timeout (status=%d)\n", WiFi.status());
    return false;
  }
  Serial.printf("[WIFI] associated, IP=%s, RSSI=%d\n",
                WiFi.localIP().toString().c_str(), WiFi.RSSI());
  return true;
}

static void teardown() {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
}

// Raw HTTP/1.1 GET. Reason for not using HTTPClient: we connect to the
// cached IP but need Host: momoggkp.vercel.app for vercel's edge router.
// HTTPClient derives the Host header from whatever was passed to begin(),
// and addHeader("Host", ...) emits a duplicate. Two-line raw GET is simpler.
// Returns the HTTP status code, or <=0 on transport failure.
static int doGet(const char* buttonName) {
  WiFiClient client;
  if (!client.connect(cachedIp, PORT)) {
    Serial.println("[WIFI] tcp connect failed");
    return -1;
  }

  client.print("GET /buttonPress/");
  client.print(buttonName);
  client.print(" HTTP/1.1\r\nHost: ");
  client.print(HOST);
  client.print("\r\nUser-Agent: GGKP/1\r\nConnection: close\r\n\r\n");

  unsigned long deadline = millis() + HTTP_READ_TIMEOUT_MS;
  while (!client.available() && client.connected() && millis() < deadline) {
    delay(10);
  }
  if (!client.available()) {
    Serial.println("[WIFI] http read timeout");
    client.stop();
    return -2;
  }

  String statusLine = client.readStringUntil('\n');
  client.stop();

  // "HTTP/1.1 200 OK\r"
  int sp = statusLine.indexOf(' ');
  int code = (sp > 0) ? statusLine.substring(sp + 1, sp + 4).toInt() : -3;
  Serial.printf("[WIFI] GET /buttonPress/%s -> %d\n", buttonName, code);
  return code;
}

void wifiRemoteFire(const char* buttonName) {
  if (!buttonName) return;
  unsigned long t0 = millis();

  if (!associate()) {
    teardown();
    return;
  }
  if (!ensureCachedIp()) {
    teardown();
    return;
  }

  int code = doGet(buttonName);
  // Vercel rotates CDN IPs; a stale cache shows up as connect-refused or
  // long timeout. Re-resolve once and retry.
  if (code <= 0) {
    Serial.println("[WIFI] re-resolving DNS and retrying once");
    cachedIpValid = false;
    if (ensureCachedIp()) doGet(buttonName);
  }

  teardown();
  Serial.printf("[WIFI] total elapsed: %lu ms\n", millis() - t0);
}
