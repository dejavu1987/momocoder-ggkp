#include "WifiRemote.h"
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_wifi.h>

#include "HwUtil.h"
#include "WifiConfigs.h"

// SSID/password/BSSID/channel are read per-press from WifiConfigs (NVS).
// DEVICE_TOKEN remains a build-time secret from wifi_secrets.ini — it's
// the bearer auth for the vercel app, not per-network.
#ifndef DEVICE_TOKEN
#define DEVICE_TOKEN  "YOUR_DEVICE_TOKEN"
#endif

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

static bool associate(const WifiConfig& cfg) {
  unsigned long t0 = millis();
  Serial.printf("[WIFI] associate begin ssid=\"%s\" ch=%u "
                "bssid=%02X:%02X:%02X:%02X:%02X:%02X timeout=%lums\n",
                cfg.ssid, (unsigned)cfg.channel,
                cfg.bssid[0], cfg.bssid[1], cfg.bssid[2],
                cfg.bssid[3], cfg.bssid[4], cfg.bssid[5],
                ASSOCIATE_TIMEOUT_MS);
  WiFi.mode(WIFI_STA);
  // WiFi.begin signature: ssid, pass, channel, bssid, connect=true.
  // bssid is non-const uint8_t* in the API even though we don't mutate it.
  WiFi.begin(cfg.ssid, cfg.password, (int32_t)cfg.channel,
             const_cast<uint8_t*>(cfg.bssid));
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
  wifiPowerOff();
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

  // The TLS handshake is mbedTLS-bound (ECDHE + ECDSA verify) and roughly
  // scales inverse to clock; expect ~750ms vs ~2200ms at 80 MHz. Scoped here
  // only so other pages keep their 80 MHz idle profile.
  cpuBumpTo(240, "WIFI");

  bool fired = false;
  int  code  = 0;
  const WifiConfig* cfg = wifiConfigsGetActive();
  if (!cfg) {
    Serial.println("[WIFI] no active Wi-Fi config — go to Wifi page to add one");
  } else if (!associate(*cfg)) {
    Serial.println("[WIFI] aborting: associate failed");
    teardown();
  } else {
    fired = true;
    code = doGet(buttonName);
    // One retry on transport failure (TLS hiccup, transient network drop).
    if (code <= 0) {
      Serial.printf("[WIFI] retry: previous attempt returned %d\n", code);
      code = doGet(buttonName);
    }
    teardown();
  }

  cpuRestore("WIFI");
  if (fired) {
    Serial.printf("[WIFI] === fire end (%lums, final code=%d) ===\n",
                  millis() - t0, code);
  } else {
    Serial.printf("[WIFI] === fire end (%lums, no request) ===\n",
                  millis() - t0);
  }
}
