#ifndef WIFI_REMOTE_H
#define WIFI_REMOTE_H

// Connect-on-press Wi-Fi remote. Each call:
//   1. WiFi.mode(STA) + WiFi.begin(ssid, pass, channel, bssid) — fast associate
//      (no scan, BSSID locked).
//   2. Resolve momoggkp.vercel.app once, cache the IP for subsequent presses.
//   3. Raw HTTP/1.1 GET to /buttonPress/<buttonName> with Host: momoggkp...
//      (raw socket so the IP-vs-vhost split is explicit).
//   4. WiFi.disconnect(true); WiFi.mode(WIFI_OFF).
//
// Total elapsed: ~600-1000 ms warm, ~2 s cold (DNS + TLS-free GET).
// Blocks the caller — every other loop responsibility (LED, OLED) freezes
// for the duration. Acceptable for this page.
void wifiRemoteFire(const char* buttonName);

#endif // WIFI_REMOTE_H
