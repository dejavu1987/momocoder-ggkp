#include "WifiSetup.h"
#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <string.h>
#include "ListPicker.h"
#include "Keypad.h"
#include "Display.h"
#include "WifiConfigs.h"
#include "WifiPage.h"
#include <WiFiClient.h>
#include <WiFiServer.h>

static const char FORM_HTML_PRE[] PROGMEM =
  "<!DOCTYPE html><html><head>"
  "<meta name=viewport content=\"width=device-width,initial-scale=1\">"
  "<title>GGKP Wi-Fi Setup</title>"
  "<style>body{font-family:sans-serif;max-width:320px;margin:2em auto;"
  "padding:0 1em}input[type=password]{width:100%;padding:.5em;"
  "font-size:1em;box-sizing:border-box}button{width:100%;padding:.7em;"
  "font-size:1em;margin-top:1em;background:#007aff;color:#fff;"
  "border:0;border-radius:.5em}.s{font-weight:bold;margin-bottom:1em}"
  "</style></head><body><h2>Add Wi-Fi to GGKP</h2>"
  "<div class=s>SSID: ";
static const char FORM_HTML_MID[] PROGMEM =
  "</div><form method=POST action=\"/save\">"
  "<input type=hidden name=ssid value=\"";
static const char FORM_HTML_POST[] PROGMEM =
  "\"><label>Password<br><input type=password name=password autofocus "
  "minlength=8 maxlength=63></label>"
  "<button type=submit>Save</button></form></body></html>";

static const char SAVED_HTML[] PROGMEM =
  "<!DOCTYPE html><html><head><title>GGKP</title></head><body>"
  "<h2>Saved</h2><p>Disconnect from GGKP-Setup. The device is connecting "
  "to your network now.</p></body></html>";

static int hexNibble(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return 10 + c - 'a';
  if (c >= 'A' && c <= 'F') return 10 + c - 'A';
  return -1;
}

// In-place URL-decode of a x-www-form-urlencoded value. Returns final length.
static size_t urldecodeInPlace(char* s) {
  char* w = s;
  for (char* r = s; *r; ++r) {
    if (*r == '+') { *w++ = ' '; }
    else if (*r == '%' && r[1] && r[2]) {
      int hi = hexNibble(r[1]), lo = hexNibble(r[2]);
      if (hi >= 0 && lo >= 0) { *w++ = (char)((hi << 4) | lo); r += 2; }
      else *w++ = *r;
    } else *w++ = *r;
  }
  *w = 0;
  return (size_t)(w - s);
}

// Find a `name=` field in a form-encoded body. Writes value (decoded) into
// out (max outSz incl NUL). Returns true if found.
static bool formField(const char* body, const char* name,
                      char* out, size_t outSz) {
  size_t nameLen = strlen(name);
  const char* p = body;
  while (*p) {
    if (strncmp(p, name, nameLen) == 0 && p[nameLen] == '=') {
      const char* v = p + nameLen + 1;
      const char* end = strchr(v, '&');
      size_t n = end ? (size_t)(end - v) : strlen(v);
      if (n >= outSz) n = outSz - 1;
      memcpy(out, v, n);
      out[n] = 0;
      urldecodeInPlace(out);
      return true;
    }
    while (*p && *p != '&') ++p;
    if (*p == '&') ++p;
  }
  return false;
}

static WifiSetupState state = WifiSetupState::Idle;
static unsigned long stateEnteredMs = 0;
static char statusMessage[40] = "";
static char currentSsid[33] = "";

struct ScanEntry {
  char    ssid[33];
  uint8_t bssid[6];
  uint8_t channel;
  int8_t  rssi;
};
constexpr int SCAN_MAX = 32;
static ScanEntry scanResults[SCAN_MAX];
static uint8_t   scanCount = 0;

static uint32_t origCpuMhz = 0;

// The scan-pick view reuses the same ListPicker primitive used by the
// Wifi page itself. Items are built directly from scanResults[].
static ListPickerItem scanItems[SCAN_MAX + 1];   // +1 for "Cancel" row
static char           scanLabels[SCAN_MAX][16];
static ListPickerView scanView;
static int8_t         pickedScanIdx = -1;        // index into scanResults

enum class ScanItemKind : uint8_t { Net = 0, Cancel = 1 };

static const char* AP_SSID = "GGKP-Setup";
static const unsigned long SETUP_AP_TIMEOUT_MS = 90UL * 1000UL;
static WiFiServer httpServer(80);
static bool       httpStarted = false;

static char submittedPassword[65] = "";

static void bumpCpu() {
  origCpuMhz = getCpuFrequencyMhz();
  if (origCpuMhz < 240) {
    setCpuFrequencyMhz(240);
    Serial.printf("[WIFISETUP] cpu %u -> 240 MHz\n", (unsigned)origCpuMhz);
  }
}
static void restoreCpu() {
  if (origCpuMhz && origCpuMhz < 240) {
    setCpuFrequencyMhz(origCpuMhz);
    Serial.printf("[WIFISETUP] cpu restored to %u MHz\n", (unsigned)origCpuMhz);
  }
  origCpuMhz = 0;
}

static void startScan() {
  bumpCpu();
  WiFi.mode(WIFI_STA);
  // async=true so we can stay responsive; scanComplete() polls below.
  int rc = WiFi.scanNetworks(/*async*/true, /*show_hidden*/false);
  Serial.printf("[WIFISETUP] scanNetworks(async) rc=%d\n", rc);
}

static int cmpRssiDesc(const void* a, const void* b) {
  const ScanEntry* ea = (const ScanEntry*)a;
  const ScanEntry* eb = (const ScanEntry*)b;
  if (ea->rssi == eb->rssi) return 0;
  return (ea->rssi > eb->rssi) ? -1 : 1;
}

static bool sameNet(const ScanEntry& a, const ScanEntry& b) {
  return strncmp(a.ssid, b.ssid, 33) == 0 &&
         memcmp(a.bssid, b.bssid, 6) == 0;
}

static void harvestScan(int n) {
  scanCount = 0;
  for (int i = 0; i < n && scanCount < SCAN_MAX; ++i) {
    String ssid = WiFi.SSID(i);
    if (ssid.length() == 0) continue;  // hidden
    ScanEntry e{};
    strncpy(e.ssid, ssid.c_str(), sizeof(e.ssid) - 1);
    memcpy(e.bssid, WiFi.BSSID(i), 6);
    e.channel = (uint8_t)WiFi.channel(i);
    e.rssi    = (int8_t)WiFi.RSSI(i);
    // De-dup exact (ssid,bssid) match (some drivers report duplicates).
    bool dup = false;
    for (uint8_t j = 0; j < scanCount; ++j) {
      if (sameNet(scanResults[j], e)) { dup = true; break; }
    }
    if (!dup) scanResults[scanCount++] = e;
  }
  qsort(scanResults, scanCount, sizeof(ScanEntry), cmpRssiDesc);
  Serial.printf("[WIFISETUP] scan harvested %u networks\n",
                (unsigned)scanCount);
  WiFi.scanDelete();
}

static void buildScanItems() {
  uint16_t n = 0;
  for (uint8_t i = 0; i < scanCount; ++i) {
    strncpy(scanLabels[i], scanResults[i].ssid, sizeof(scanLabels[i]) - 1);
    scanLabels[i][sizeof(scanLabels[i]) - 1] = 0;
    scanItems[n++] = {scanLabels[i], (uint8_t)ScanItemKind::Net, i};
  }
  scanItems[n++] = {"Cancel", (uint8_t)ScanItemKind::Cancel, 0};
  listPickerInit(scanView, scanItems, n, LIST_PICKER_NO_ACTIVE);
}

static void startAp() {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_AP);
  bool ok = WiFi.softAP(AP_SSID, /*password*/nullptr);
  Serial.printf("[WIFISETUP] softAP \"%s\" %s, IP=%s\n", AP_SSID,
                ok ? "up" : "FAILED",
                WiFi.softAPIP().toString().c_str());
  httpServer.begin();
  httpStarted = true;
}

static void stopAp() {
  if (httpStarted) {
    httpServer.stop();
    httpStarted = false;
  }
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  Serial.println("[WIFISETUP] AP stopped");
}

static void enterState(WifiSetupState s) {
  Serial.printf("[WIFISETUP] %u -> %u\n",
                (unsigned)state, (unsigned)s);
  state = s;
  stateEnteredMs = millis();
}

void wifiSetupBegin() {
  if (state != WifiSetupState::Idle) {
    Serial.println("[WIFISETUP] begin ignored — already active");
    return;
  }
  statusMessage[0] = 0;
  currentSsid[0] = 0;
  enterState(WifiSetupState::Scanning);
}

void wifiSetupCancel() {
  if (state == WifiSetupState::Idle) return;
  Serial.println("[WIFISETUP] cancel");
  WiFi.scanDelete();
  if (httpStarted) {
    httpServer.stop();
    httpStarted = false;
  }
  WiFi.softAPdisconnect(true);
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  restoreCpu();
  enterState(WifiSetupState::Idle);
}

static void sendForm(WiFiClient& cli, const char* ssid) {
  cli.print(F("HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n"
              "Connection: close\r\n\r\n"));
  cli.print((__FlashStringHelper*)FORM_HTML_PRE);
  cli.print(ssid);
  cli.print((__FlashStringHelper*)FORM_HTML_MID);
  cli.print(ssid);
  cli.print((__FlashStringHelper*)FORM_HTML_POST);
}

static void sendSaved(WiFiClient& cli) {
  cli.print(F("HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n"
              "Connection: close\r\n\r\n"));
  cli.print((__FlashStringHelper*)SAVED_HTML);
}

static void sendRedirect(WiFiClient& cli) {
  cli.print(F("HTTP/1.1 302 Found\r\nLocation: http://192.168.4.1/\r\n"
              "Connection: close\r\nContent-Length: 0\r\n\r\n"));
}

// Read one HTTP request from cli into buf (header + up to bodyMax bytes
// of body). Returns true if a full request line + headers were read.
// Body length is capped at bodyMax — fine because our only POST body is
// "ssid=...&password=..." which is well under 256 bytes.
static bool readRequest(WiFiClient& cli, char* buf, size_t bufSz,
                        const char** method, const char** path,
                        const char** body) {
  size_t n = 0;
  unsigned long deadline = millis() + 2000;
  // Headers
  while (n < bufSz - 1 && millis() < deadline) {
    while (cli.available() && n < bufSz - 1) {
      buf[n++] = (char)cli.read();
      if (n >= 4 &&
          buf[n - 4] == '\r' && buf[n - 3] == '\n' &&
          buf[n - 2] == '\r' && buf[n - 1] == '\n') {
        goto headersDone;
      }
    }
    delay(2);
  }
headersDone:
  buf[n] = 0;
  if (n == 0) return false;

  *method = buf;
  char* sp = strchr(buf, ' ');
  if (!sp) return false;
  *sp = 0;
  *path = sp + 1;
  char* sp2 = strchr((char*)*path, ' ');
  if (sp2) *sp2 = 0;

  // Find Content-Length and read body if present.
  *body = "";
  const char* clHdr = strstr(sp2 ? sp2 + 1 : buf + n, "Content-Length:");
  size_t contentLen = 0;
  if (clHdr) contentLen = (size_t)atoi(clHdr + 15);
  if (contentLen > 0 && n < bufSz - 1) {
    size_t off = n;
    size_t want = contentLen;
    if (want > bufSz - 1 - off) want = bufSz - 1 - off;
    deadline = millis() + 2000;
    while (want > 0 && millis() < deadline) {
      while (cli.available() && want > 0) {
        buf[off++] = (char)cli.read();
        want--;
      }
      delay(2);
    }
    buf[off] = 0;
    *body = buf + n;
  }
  return true;
}

void wifiSetupTick() {
  if (state == WifiSetupState::Idle) return;

  switch (state) {
    case WifiSetupState::Scanning: {
      // First tick after entering Scanning: kick off async scan.
      static unsigned long scanStartedMs = 0;
      if (scanStartedMs != stateEnteredMs) {
        startScan();
        scanStartedMs = stateEnteredMs;
      }
      int n = WiFi.scanComplete();
      if (n == WIFI_SCAN_RUNNING) {
        // Hard timeout — don't get stuck.
        if (millis() - stateEnteredMs > 10000) {
          Serial.println("[WIFISETUP] scan timeout");
          WiFi.scanDelete();
          strncpy(statusMessage, "scan timeout",
                  sizeof(statusMessage) - 1);
          enterState(WifiSetupState::Failed);
        }
        return;
      }
      if (n < 0 || n == 0) {
        Serial.printf("[WIFISETUP] scan returned %d\n", n);
        WiFi.scanDelete();
        strncpy(statusMessage,
                (n <= 0) ? "scan failed" : "no networks",
                sizeof(statusMessage) - 1);
        enterState(WifiSetupState::Failed);
        return;
      }
      harvestScan(n);
      restoreCpu();
      buildScanItems();
      enterState(WifiSetupState::PickingSsid);
      break;
    }
    case WifiSetupState::WaitingForClient: {
      if (WiFi.softAPgetStationNum() > 0) {
        Serial.println("[WIFISETUP] client associated to setup AP");
        enterState(WifiSetupState::WaitingForSubmit);
      } else if (millis() - stateEnteredMs > SETUP_AP_TIMEOUT_MS) {
        strncpy(statusMessage, "setup timeout", sizeof(statusMessage) - 1);
        stopAp();
        enterState(WifiSetupState::Failed);
      }
      break;
    }
    case WifiSetupState::WaitingForSubmit: {
      if (millis() - stateEnteredMs > SETUP_AP_TIMEOUT_MS) {
        strncpy(statusMessage, "setup timeout", sizeof(statusMessage) - 1);
        stopAp();
        enterState(WifiSetupState::Failed);
        break;
      }
      WiFiClient cli = httpServer.available();
      if (!cli) break;
      static char reqBuf[1024];
      const char *method = "", *path = "", *body = "";
      bool ok = readRequest(cli, reqBuf, sizeof(reqBuf),
                            &method, &path, &body);
      Serial.printf("[WIFISETUP] http %s %s ok=%d\n", method, path, ok);
      if (!ok) { cli.stop(); break; }

      // Captive-portal probes from various OSes — redirect to /.
      if (strcmp(path, "/") != 0 && strcmp(path, "/save") != 0) {
        sendRedirect(cli);
        cli.flush();
        cli.stop();
        break;
      }

      if (strcmp(method, "GET") == 0 && strcmp(path, "/") == 0) {
        sendForm(cli, currentSsid);
        cli.flush();
        cli.stop();
        break;
      }

      if (strcmp(method, "POST") == 0 && strcmp(path, "/save") == 0) {
        char pwd[65] = "";
        bool havePwd = formField(body, "password", pwd, sizeof(pwd));
        size_t pwdLen = strlen(pwd);
        if (!havePwd || pwdLen < 8 || pwdLen > 63) {
          // Re-serve the form. Phone validation should also catch this.
          sendForm(cli, currentSsid);
          cli.flush();
          cli.stop();
          break;
        }
        strncpy(submittedPassword, pwd, sizeof(submittedPassword) - 1);
        submittedPassword[sizeof(submittedPassword) - 1] = 0;
        sendSaved(cli);
        cli.flush();
        cli.stop();
        // Tear AP and validate in Saving (Task 11).
        stopAp();
        enterState(WifiSetupState::Saving);
        break;
      }

      // Unknown method/path — redirect.
      sendRedirect(cli);
      cli.flush();
      cli.stop();
      break;
    }
    case WifiSetupState::Saving: {
      // Single-shot: kick off association on first tick; poll thereafter.
      static unsigned long savingStartedMs = 0;
      if (savingStartedMs != stateEnteredMs) {
        savingStartedMs = stateEnteredMs;
        if (pickedScanIdx < 0) {
          strncpy(statusMessage, "internal error",
                  sizeof(statusMessage) - 1);
          enterState(WifiSetupState::Failed);
          break;
        }
        const ScanEntry& e = scanResults[pickedScanIdx];
        bumpCpu();
        WiFi.mode(WIFI_STA);
        WiFi.begin(e.ssid, submittedPassword, (int32_t)e.channel,
                   const_cast<uint8_t*>(e.bssid));
        Serial.printf("[WIFISETUP] validating creds for \"%s\"\n", e.ssid);
        break;
      }
      wl_status_t s = WiFi.status();
      if (s == WL_CONNECTED) {
        const ScanEntry& e = scanResults[pickedScanIdx];
        WifiConfig cfg{};
        strncpy(cfg.ssid, e.ssid, sizeof(cfg.ssid) - 1);
        strncpy(cfg.password, submittedPassword, sizeof(cfg.password) - 1);
        memcpy(cfg.bssid, e.bssid, 6);
        cfg.channel = e.channel;
        int8_t idx = wifiConfigsAddOrUpdate(cfg, /*setAsActive*/true);
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
        restoreCpu();
        // Wipe submitted password from RAM after persist.
        memset(submittedPassword, 0, sizeof(submittedPassword));
        if (idx < 0) {
          strncpy(statusMessage, "storage full",
                  sizeof(statusMessage) - 1);
          enterState(WifiSetupState::Failed);
        } else {
          wifiPageRefresh();
          enterState(WifiSetupState::Done);
        }
        break;
      }
      if (millis() - stateEnteredMs > 8000) {
        Serial.printf("[WIFISETUP] validation timeout (status=%d)\n", (int)s);
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
        restoreCpu();
        memset(submittedPassword, 0, sizeof(submittedPassword));
        strncpy(statusMessage, "wrong password", sizeof(statusMessage) - 1);
        enterState(WifiSetupState::Failed);
      }
      break;
    }
    case WifiSetupState::Done: {
      if (millis() - stateEnteredMs > 1500) {
        enterState(WifiSetupState::Idle);
      }
      break;
    }
    case WifiSetupState::Failed: {
      if (millis() - stateEnteredMs > 3000) {
        enterState(WifiSetupState::Idle);
      }
      break;
    }
    default:
      break;
  }
}

void wifiSetupHandleButton(int button) {
  if (state != WifiSetupState::PickingSsid) {
    // BTN_A long-press is handled by polling in tick() (Task 13);
    // taps in non-Idle non-Picking states are ignored.
    return;
  }
  switch (button) {
    case BTN_A:  listPickerOnSlot(scanView, 0); break;
    case BTN_B:  listPickerOnSlot(scanView, 1); break;
    case BTN_C:  listPickerOnSlot(scanView, 2); break;
    case BTN_D:  listPickerOnSlot(scanView, 3); break;
    case BTN_LT: listPickerOnLeft(scanView);   break;
    case BTN_RT: listPickerOnRight(scanView);  break;
    case BTN_OK: {
      int32_t idx = listPickerOnOk(scanView);
      if (idx < 0) return;
      const ListPickerItem& it = scanItems[idx];
      if ((ScanItemKind)it.kind == ScanItemKind::Cancel) {
        wifiSetupCancel();
        return;
      }
      pickedScanIdx = (int8_t)it.userId;
      strncpy(currentSsid, scanResults[pickedScanIdx].ssid,
              sizeof(currentSsid) - 1);
      Serial.printf("[WIFISETUP] picked SSID \"%s\"\n", currentSsid);
      startAp();
      enterState(WifiSetupState::WaitingForClient);
      break;
    }
    default: break;
  }
}

bool wifiSetupIsActive() { return state != WifiSetupState::Idle; }
WifiSetupState wifiSetupGetState() { return state; }
const char* wifiSetupGetStatusMessage() { return statusMessage; }
const char* wifiSetupGetCurrentSsid() { return currentSsid; }

void wifiSetupRender() {
  if (state == WifiSetupState::PickingSsid) {
    listPickerRender(scanView);
    return;
  }
  // Per-state OLED screens (Scanning/WaitingForClient/etc.) are added in
  // Task 12. Until then, leave the previous frame on the screen.
}

WifiSetupDigest wifiSetupGetDigest() {
  WifiSetupDigest d{};
  d.state = (uint8_t)state;
  d.pickerPage = scanView.pageIdx;
  d.highlight  = scanView.highlightSlot;
  return d;
}
