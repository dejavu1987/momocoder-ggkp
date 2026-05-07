# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

PlatformIO/Arduino firmware for the **Momocoder GGKP** — an ESP32-S3 BLE HID combo (keyboard + mouse) with a 3x3 keypad, MPU6050 IMU "air mouse", a 64x48 SSD1306 OLED, and a Wi-Fi-remote page that fires HTTPS GETs at the companion app (`~/dejavu/momoggkp`, deployed at `momoggkp.vercel.app`). Single environment: `[env:esp32-s3]` on `esp32-s3-devkitc-1`, CPU pinned to 80 MHz (briefly bumped to 240 MHz inside `wifiRemoteFire` for the TLS handshake), partition table `huge_app.csv`.

## Common commands

PlatformIO is required (CLI or VS Code extension). All commands run from the project root.

```bash
pio run                              # build for esp32-s3
pio run -t upload                    # build + flash over USB CDC
pio run -t upload -t monitor         # flash then open serial monitor
pio device monitor -b 115200         # serial monitor only
pio run -t clean                     # clean build artifacts
pio pkg install                      # install/refresh lib_deps
```

The board exposes USB CDC (`ARDUINO_USB_CDC_ON_BOOT=1`); `Serial` prints over USB, no extra UART adapter.

## Architecture

The firmware is split into proper `.h`/`.cpp` pairs. `src/globals.h` is the shared declarations file — every `.cpp` includes it to see `extern` state and forward declarations.

Layout:

- `src/main.cpp` — `setup()` / `loop()`, BLE init, MPU6050 wake/sleep on page transitions, red-LED `LedPulse` state machine, deep-sleep + OLED-power-save timers, and `printPage()` / `renderPage()` which paint the OLED with a `DisplayState`-keyed cache so the I²C bus only streams on real change.
- `src/Keypad.{h,cpp}` — pin map for the 9 buttons, the `IRAM_ATTR buttonInterrupt()` ISR (FALLING on every button), and `handleButtonPress(Page, btn)` which delegates to `findBinding(page, btn)` + `executeAction(action)` from `Pages.cpp`. Disconnected-state presses (BLE not yet linked) fall through to `handleButtonPressDisconnected()` which pairs with the Settings page's pairing controls.
- `src/Pages.{h,cpp}` — per-page `Binding[]` tables and the `ActionKind` enum (`Key`, `MediaKey`, `MouseClick`, `ToggleScroll`, `ToggleDrag`, `NavPrev`, `NavNext`, `AdjustSens`, `AdjustDelay`, `EnterPairing`, `ForgetBonds`, `WifiRequest`, `CycleBrightness`). `executeAction()` is the single dispatch point — every action lives in one switch.
- `src/AirMouse.{h,cpp}` — MPU6050 I²C helpers (`i2cWrite`, `i2cWrite2`, `i2cRead`), the `mpuWake()` / `mpuSleep()` controls used by `loop()` on page transitions, and the `gyroX/Z` + `i2cData[14]` globals.
- `src/Display.{h,cpp}` — direct U8g2 driver for the EastRising 64x48 OLED via `U8G2_SSD1306_64X48_ER_F_HW_I2C`. The `_ER_` variant applies the 32-column RAM offset this panel needs (Adafruit_SSD1306 didn't, which produced half-noise output). `displaySetup(sda, scl)` calls `Wire.setPins(sda, scl)` immediately after `u8g2.begin()` because U8g2 internally calls `Wire.begin()` with default ESP32 pins (21/22), which would unhook the MPU6050. Also exposes `displaySetPowerSave(bool)` and `displaySetBrightness(uint8_t)` / `displayCycleBrightness()` (Settings B cycles low/mid/high SSD1306 contrast).
- `src/WifiRemote.{h,cpp}` — connect-on-press HTTPS to `momoggkp.vercel.app`. `wifiRemoteFire(name)` runs the full associate → TLS → GET → teardown cycle (~1.2–1.6 s warm). Uses pre-known channel + BSSID and `WiFiClientSecure::setInsecure()` for SNI-correct routing without a CA bundle. Bumps CPU to 240 MHz only inside this function so other pages keep the 80 MHz idle profile.
- `src/globals.h` — `enum class Page` (`Mouse`/`Media`/`Remote`/`Settings`, `NUM_PAGES = 4`) with wrap-around `++`/`--` overloads, `enum class ConnState`, and `extern` declarations for `bleCombo`, `mouseEnabled` / `scrollEnabled` / `dragEnabled` flags, `mouseSensitivity`, `mouseMoveDelay`, plus `enterPairingMode()` / `forgetAllBonds()`.

`main.cpp` also does `#include <keyvals.cpp>` — that file ships inside the `BLECombo` library (forked at `https://github.com/earthicko/ESP32-BLE-Combo.git`), not in this repo.

### Runtime model

- All input is interrupt-driven: every button shares one ISR (`IRAM_ATTR` so it survives flash reads) that scans `digitalRead` and stores the GPIO number in `volatile int pressedButton`. `loop()` polls that flag, dispatches via `handleButtonPress(currentPage, pressedButton)` → `findBinding` → `executeAction`, debounces with `delay(DEBOUNCE_MS)`, and clears it.
- `currentPage` cycles `Mouse → Media → Remote → Settings → Mouse` via the UP/DN buttons; `++` and `--` on the enum class wrap modulo `NUM_PAGES`. `Mouse` and `Settings` enable the air mouse and keep MPU6050 awake; `Media`/`Remote` sleep it via `mpuSleep()`. BLE pairing controls live on Settings (OK = re-pair, A = forget all bonds) — there is no separate Pairing page.
- The MPU6050 is read each loop via `i2cRead(0x3B, …, 14)` only when `mouseEnabled`; gyro bytes at offsets 8–13 are scaled by `mouseSensitivity` (with a fixed +2.5× offset on X to cancel idle drift) and pushed through `bleCombo.mouseMove(z, x)`. `scrollEnabled` reroutes the same values to scroll-wheel deltas. `mouseMoveDelay` throttles the loop.
- Two idle timeouts (in `main.cpp`): `OLED_IDLE_MS` (30 s) puts the SSD1306 into power-save without repainting, and `IDLE_SLEEP_CONNECTED_MS` / `IDLE_SLEEP_DISCONNECTED_MS` (60 s / 120 s) call `esp_deep_sleep_start()`. Wake source is `GPIO_NUM_6` (BTN_A) configured as `ext0` LOW. The first button press after OLED-sleep is swallowed (just wakes the screen), preventing accidental actions on a blank display.
- Only the **red** LED (GPIO 45) is wired on this PCB — `B_PIN`/`G_PIN` are NC. Status is encoded entirely in red brightness/blink patterns (see `LedPulse` in `main.cpp`): dim 100/1900 ms heartbeat while reconnecting, bright 150/150 ms blink while discoverable, off when connected. OLED carries any other status.
- `printPage()` runs every loop iteration but only repaints when its `DisplayState` snapshot (page + scroll + drag + sensitivity + moveDelay + connState) changes — full-buffer streaming over I²C is ~10 ms otherwise.

### Wi-Fi Remote page

- The Remote page (`Page::Remote`) binds 7 buttons (`A`/`B`/`left`/`ok`/`right`/`C`/`D`) to HTTPS GETs at `https://momoggkp.vercel.app/buttonPress/{name}`. UP/DN remain page nav.
- BLE stays connected throughout. Wi-Fi flares up only during the request (~1.2–1.6 s) — coexistence on ESP32-S3 handles the brief overlap. No full Wi-Fi off/on cycle between pages.
- `WifiRemote.cpp` does: `WiFi.begin(SSID, PASS, channel, BSSID)` (channel + BSSID hard-coded → no scan, ~200 ms associate), `WiFiClientSecure::setInsecure()` + `connect(HOST, 443)` (SNI is set from hostname; vercel.app routes by SNI, so caching an IP would break routing), raw HTTP/1.1 GET with a manual `Host:` and `Authorization: Bearer ${DEVICE_TOKEN}` header. Teardown via `WiFi.disconnect(true) + WiFi.mode(WIFI_OFF)` — yes, this logs `E (NNNN) wifi:timeout when WiFi un-init, type=4`, but it's **cosmetic**. Bypassing via direct `esp_wifi_disconnect()` + `esp_wifi_stop()` wedges arduino-esp32's internal state machine and the next `WiFi.begin()` returns `NO_SHIELD`. The arduino-managed teardown is the only path that recovers cleanly; we accept the warning.
- CPU bumped to 240 MHz only inside `wifiRemoteFire()` (around the TLS handshake), restored to 80 MHz before returning. Cuts handshake from ~2200 ms to ~1000 ms while keeping every other page on the lower-power 80 MHz idle.
- Credentials live in `wifi_secrets.ini` (gitignored), interpolated as `build_flags` via `extra_configs` in `platformio.ini`. Three required defines: `WIFI_SSID`, `WIFI_PASSWORD`, `DEVICE_TOKEN`. The token must match the `DEVICE_TOKEN` env var on the Vercel `momoggkp` project byte-for-byte.

### Pin map (do not change without checking the PCB)

```
Keypad (INPUT_PULLUP, FALLING interrupt)   I²C (Wire)
+----+----+----+                            SDA = GPIO 2 (I2C_SDA)
|  6 |  8 | 18 |   A   UP  B                SCL = GPIO 1 (I2C_SCL)
+----+----+----+                            MPU6050 @ 0x68
|  5 | 15 | 17 |   LT  OK  RT               SSD1306 @ 0x3C
+----+----+----+
|  4 |  7 | 16 |   C   DN  D
+----+----+----+

RGB LED: B=GPIO3, R=GPIO45, G=GPIO47
Wake button (deep sleep ext0): GPIO6 (BTN_A)
```

## Library dependencies (pinned in `platformio.ini`)

`U8g2`, `U8g2_for_Adafruit_GFX`, `Adafruit GFX`, `Adafruit BusIO`, `Adafruit SSD1306`, `NimBLE-Arduino`, and the `BLECombo` fork from `earthicko/ESP32-BLE-Combo`. The Adafruit display libs are no longer referenced in code (U8g2 drives the OLED directly) but stay in `lib_deps` for now — drop them when you're sure nothing else pulls them. `WiFi` and `WiFiClientSecure` come bundled with the Arduino-ESP32 framework — no `lib_deps` entry needed.

## Editing tips specific to this codebase

- New shared state belongs in `globals.h` as an `extern`, defined once in the appropriate `.cpp` (`main.cpp` for top-level state; `Keypad.cpp` for keypad state; etc.).
- Per-page button behavior is data, not code: edit the appropriate `Binding[]` table in `src/Pages.cpp`. Adding a new page means bumping `NUM_PAGES` in `globals.h`, adding the enum value in `Page`, adding the table + `pageDefs[]` entry in `Pages.cpp`, and (if it's a non-mouse page) the `mouseEnabled` toggle in `loop()` already handles MPU sleep automatically.
- New action types (e.g., a new side effect) go in `enum class ActionKind` plus a `case` in `executeAction()` in `Pages.cpp`. The dispatcher is one switch — keep it that way.
- `printPage()`'s cache compares all of `DisplayState`. If you add new on-screen state, extend the struct + `operator!=` so the OLED actually refreshes.
- `Wire.setPins()` is in ESP32 Arduino core 2.0+. Older cores need `Wire.end(); Wire.begin(sda, scl);` instead in `Display.cpp`.
- If you change AP / move the device to a new network, the channel + BSSID in `wifi_secrets.ini` and `WIFI_BSSID[6]` in `WifiRemote.cpp` both need to update — `arp -an` only gives the gateway MAC, not the radio BSSID. Use macOS Wireless Diagnostics → Window → Scan to read the 2.4 GHz radio's BSSID + channel directly.
