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
- `src/WifiRemote.{h,cpp}` — connect-on-press HTTPS to `momoggkp.vercel.app`. `wifiRemoteFire(name)` runs the full associate → TLS → GET → teardown cycle (~1.2–1.6 s warm). Reads SSID/password/channel/BSSID from `wifiConfigsGetActive()` (NVS) per press; `WiFiClientSecure::setInsecure()` keeps SNI-correct routing without a CA bundle. Bumps CPU to 240 MHz only inside this function so other pages keep the 80 MHz idle profile.
- `src/WifiConfigs.{h,cpp}` — per-location Wi-Fi credentials. NVS-backed slot storage (namespace `"wifi"`, one binary blob per slot via `Preferences::putBytes`). Public API: `wifiConfigsBegin/Count/Get/GetActive/AddOrUpdate/DeleteActive/SetActive`. Hard cap `WIFI_MAX_CONFIGS = 16` (UI-cosmetic).
- `src/Settings.{h,cpp}` — persisted user settings (NVS namespace `"settings"`): `mouseSensitivity`, `mouseMoveDelay`, OLED brightness. Owns the in-RAM definitions of `mouseSensitivity` / `mouseMoveDelay` (declared `extern` in `globals.h`). `settingsBegin()` runs in `setup()` *before* `displaySetup()` so the OLED comes up at the saved contrast; `displaySetup()` reads `settingsGetBrightness()` to seed `currentBrightness`. `settingsSave()` is called from `executeAction(AdjustSens|AdjustDelay)` (only when the value actually changed) and from `displayCycleBrightness()`.
- `src/ListPicker.{h,cpp}` — generic 4-row list-picker primitive used by the Wifi page (and reusable for any future "pick one of N" UI). Two-step `highlight → OK` model; `LT/RT` paginate; full-row inversion + active-dot marker; rows 11 px tall + 4 px footer for the page indicator.
- `src/WifiPage.{h,cpp}` — glue between `WifiConfigs` and `ListPicker`. Owns the picker view and items array; injects synthetic `+ Add new...` and `Delete current` rows; maps `OK` to `setActive` / `wifiSetupBegin()` / `wifiConfigsDeleteActive()` based on the confirmed item's `kind`.
- `src/WifiSetup.{h,cpp}` — captive-portal state machine (`Scanning/PickingSsid/WaitingForClient/WaitingForSubmit/Saving/Done/Failed`). Hand-rolled HTTP/1.1 server on `WiFiServer` (no `WebServer` dep). Long-press-A cancel polled inside `wifiSetupTick()`. Wi-Fi only powered on for the duration of the flow.
- `src/globals.h` — `enum class Page` (`Mouse`/`Media`/`Remote`/`Wifi`/`Settings`, `NUM_PAGES = 5`) with wrap-around `++`/`--` overloads, `enum class ConnState`, and `extern` declarations for `bleCombo`, `mouseEnabled` / `scrollEnabled` / `dragEnabled` flags, `mouseSensitivity`, `mouseMoveDelay`, plus `enterPairingMode()` / `forgetAllBonds()`.

`main.cpp` also does `#include <keyvals.cpp>` — that file ships inside the `BLECombo` library (forked at `https://github.com/earthicko/ESP32-BLE-Combo.git`), not in this repo.

### Runtime model

- All input is interrupt-driven: every button shares one ISR (`IRAM_ATTR` so it survives flash reads) that scans `digitalRead` and stores the GPIO number in `volatile int pressedButton`. `loop()` polls that flag, dispatches via `handleButtonPress(currentPage, pressedButton)` → `findBinding` → `executeAction`, debounces with `delay(DEBOUNCE_MS)`, and clears it.
- `currentPage` cycles `Mouse → Media → Remote → Wifi → Settings → Mouse` via the UP/DN buttons; `++` and `--` on the enum class wrap modulo `NUM_PAGES`. `Mouse` and `Settings` enable the air mouse and keep MPU6050 awake; `Media`/`Remote`/`Wifi` sleep it via `mpuSleep()`. BLE pairing controls live on Settings (OK = re-pair, A = forget all bonds) — there is no separate Pairing page. The Wifi page is a list-picker of saved configs (see Wi-Fi Remote section).
- The MPU6050 is read each loop via `i2cRead(0x3B, …, 14)` only when `mouseEnabled`; gyro bytes at offsets 8–13 are scaled by `mouseSensitivity` (with a fixed +2.5× offset on X to cancel idle drift) and pushed through `bleCombo.mouseMove(z, x)`. `scrollEnabled` reroutes the same values to scroll-wheel deltas. `mouseMoveDelay` throttles the loop.
- Two idle timeouts (in `main.cpp`): `OLED_IDLE_MS` (30 s) puts the SSD1306 into power-save without repainting, and `IDLE_SLEEP_CONNECTED_MS` / `IDLE_SLEEP_DISCONNECTED_MS` (60 s / 120 s) call `esp_deep_sleep_start()`. Wake source is `GPIO_NUM_6` (BTN_A) configured as `ext0` LOW. The first button press after OLED-sleep is swallowed (just wakes the screen), preventing accidental actions on a blank display.
- Only the **red** LED (GPIO 45) is wired on this PCB — `B_PIN`/`G_PIN` are NC. Status is encoded entirely in red brightness/blink patterns (see `LedPulse` in `main.cpp`): dim 100/1900 ms heartbeat while reconnecting, bright 150/150 ms blink while discoverable, off when connected. OLED carries any other status.
- `printPage()` runs every loop iteration but only repaints when its `DisplayState` snapshot (page + scroll + drag + sensitivity + moveDelay + connState) changes — full-buffer streaming over I²C is ~10 ms otherwise.

### Wi-Fi remote + multi-config

The Remote page (`Page::Remote`) binds 7 buttons (`A`/`B`/`left`/`ok`/`right`/`C`/`D`) to HTTPS GETs at `https://momoggkp.vercel.app/buttonPress/{name}`. UP/DN remain page nav.

Credentials are **per-saved-config**, not per-build. `WifiConfig` slots are stored in NVS namespace `"wifi"` (one blob per slot via `Preferences::putBytes`); `wifiConfigsGetActive()` returns the current `(ssid, password, bssid, channel)` tuple, and `wifiRemoteFire()` reads it on every press. If no active config exists (first boot, or active deleted), the press logs `[WIFI] no active Wi-Fi config — go to Wifi page to add one` and returns without associating.

Adding a new config happens entirely on-device:

- The Wifi page (between Remote and Settings) is a list-picker of saved configs plus synthetic `+ Add new...` and `Delete current` rows. `A/B/C/D` highlight a row, `OK` confirms; `LT/RT` paginate; `UP/DN` page-nav.
- Confirming `+ Add new...` enters the captive-portal flow (`WifiSetup` state machine: `Scanning → PickingSsid → WaitingForClient → WaitingForSubmit → Saving → Done`). The device runs an open AP `GGKP-Setup` at `192.168.4.1` and serves a one-page form. Submitting validates the password by attempting a real STA association (8 s timeout) before persisting; on failure the slot is not saved and the OLED shows `Failed: wrong password`.
- Long-press `A` (>= 1 s) cancels any non-Idle setup state. OLED idle and deep-sleep are suppressed for the duration of the flow.

The TLS-handshake-time CPU bump to 240 MHz inside `wifiRemoteFire()` is unchanged. `WifiSetup` does the same trick during scan + validation, restoring 80 MHz before returning. Teardown via `WiFi.disconnect(true) + WiFi.mode(WIFI_OFF)` still logs `E (NNNN) wifi:timeout when WiFi un-init, type=4` cosmetically — bypassing wedges arduino-esp32's state machine, so we accept the warning.

`wifi_secrets.ini` keeps only `DEVICE_TOKEN` (the bearer auth for the vercel app). All other Wi-Fi build_flags were removed when multi-config landed — first flash boots with zero saved configs and the user adds their first network through the captive portal.

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
- A new "pick one of N" page should reuse `ListPicker` rather than rolling its own UI. Add an `enum *ItemKind` for synthetic rows (Add/Delete/etc.), build `ListPickerItem[]` with kind-tagged userIds, and dispatch from a single `onConfirm()` switch — `WifiPage.cpp` is the reference implementation.
- Moving the device to a new network used to require editing `wifi_secrets.ini` + `WIFI_BSSID[6]`; now it's done on-device through the Wifi page's `+ Add new...` captive-portal flow. `arp -an` and macOS Wireless Diagnostics are no longer relevant for the GGKP — the on-device scan picks the BSSID + channel directly.
