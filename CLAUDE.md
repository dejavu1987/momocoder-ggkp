# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

PlatformIO/Arduino firmware for the **Momocoder GGKP** — an ESP32-S3 BLE HID combo (keyboard + mouse) with a 3x3 keypad, MPU6050 IMU "air mouse", and a 64x48 SSD1306 OLED. Single environment: `[env:esp32-s3]` on `esp32-s3-devkitc-1`, CPU pinned to 80 MHz, partition table `huge_app.csv`.

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

- `src/main.cpp` — `setup()` / `loop()`, BLE init, MPU6050 wake, RGB LED state machine, deep-sleep timer, the `pages[NUM_PAGES][ICONS_PER_PAGE]` glyph table, and `printPage()` / `renderPage()` which paint the OLED with a `DisplayState`-keyed cache so the bus only streams on real change.
- `src/Keypad.{h,cpp}` — pin map for the 9 buttons, the `IRAM_ATTR buttonInterrupt()` ISR (FALLING on every button), and `handleButtonPress(Page, btn)` which dispatches to per-page handlers `handleButtonPressPage0`/`Page1`/`Page2`/`Page3`. Active behavior depends on `currentPage`.
- `src/AirMouse.{h,cpp}` — MPU6050 I²C helpers (`i2cWrite`, `i2cWrite2`, `i2cRead`) and the `gyroX/Z` + `i2cData[14]` globals consumed by `loop()` to drive `bleCombo.mouseMove`.
- `src/Display.{h,cpp}` — direct U8g2 driver for the EastRising 64x48 OLED via `U8G2_SSD1306_64X48_ER_F_HW_I2C`. The `_ER_` variant applies the 32-column RAM offset this panel needs (Adafruit_SSD1306 doesn't, which produced half-noise output). `displaySetup(sda, scl)` calls `Wire.setPins(sda, scl)` immediately after `u8g2.begin()` because U8g2 internally calls `Wire.begin()` with default ESP32 pins (21/22), which would unhook the MPU6050.
- `src/globals.h` — `enum class Page` (`Mouse`/`Media`/`Settings`/`Pairing`) with `++`/`--` overloads that wrap modulo `NUM_PAGES`, layout constants (`NUM_PAGES`, `ICONS_PER_PAGE`), and `extern` declarations for `bleCombo`, the `mouseEnabled` / `scrollEnabled` / `dragEnabled` / `pairingMode` flags, `mouseSensitivity`, `mouseMoveDelay`, plus `void enterPairingMode()`.

`main.cpp` also does `#include <keyvals.cpp>` — that file ships inside the `BLECombo` library (forked at `https://github.com/earthicko/ESP32-BLE-Combo.git`), not in this repo.

### Runtime model

- All input is interrupt-driven: every button shares one ISR (`IRAM_ATTR` so it survives flash reads) that scans `digitalRead` and stores the GPIO number in `volatile int pressedButton`. `loop()` polls that flag, dispatches via `handleButtonPress(currentPage, pressedButton)`, debounces with `delay(DEBOUNCE_MS)`, and clears it.
- `currentPage` cycles `Mouse → Media → Settings → Pairing → Mouse` via the UP/DN buttons; `++` and `--` on the enum class wrap automatically. `Mouse` and `Settings` enable the air mouse; `Media` is the keyboard/media layer; `Pairing` is BLE re-pairing — pressing OK calls `enterPairingMode()` in `main.cpp` which disconnects the current peer, waits for it to drop, calls `NimBLEDevice::deleteAllBonds()`, and restarts advertising.
- The MPU6050 is read each loop via `i2cRead(0x3B, …, 14)`; gyro bytes at offsets 8–13 are scaled by `mouseSensitivity` (with a fixed +2.5× offset on X to cancel idle drift) and pushed through `bleCombo.mouseMove(z, x)`. `scrollEnabled` reroutes the same values to scroll-wheel deltas. `mouseMoveDelay` throttles the loop.
- After `IDLE_SLEEP_MS` (60 s) with no button press the device calls `esp_deep_sleep_start()`. Wake source is `GPIO_NUM_6` (BTN_A) configured as `ext0` LOW.
- RGB status LED on GPIO 3 (B) / 45 (R) / 47 (G), driven with `analogWrite` via the `setLed(r, g, b)` helper. Green when BLE is connected; fast 150 ms blue blink while `pairingMode` is true and disconnected; otherwise a slow ~3 s red/green/blue cycle (using blocking `delay(1000)`s) while waiting for a host.
- `printPage()` runs every loop iteration but only repaints when its `DisplayState` snapshot (page + scroll + drag + pairing + sensitivity + moveDelay) changes — full-buffer streaming over I²C is ~10 ms otherwise.

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

`U8g2`, `U8g2_for_Adafruit_GFX`, `Adafruit GFX`, `Adafruit BusIO`, `Adafruit SSD1306`, `NimBLE-Arduino`, and the `BLECombo` fork from `earthicko/ESP32-BLE-Combo`. The Adafruit display libs are no longer referenced in code (U8g2 drives the OLED directly) but stay in `lib_deps` for now — drop them when you're sure nothing else pulls them.

## Editing tips specific to this codebase

- New shared state belongs in `globals.h` as an `extern`, defined once in the appropriate `.cpp` (`main.cpp` for top-level state; `Keypad.cpp` for keypad state; etc.).
- Per-page button behavior changes go in the matching `handleButtonPressPageN` function in `src/Keypad.cpp`. Adding a new page means: bumping `NUM_PAGES` in `globals.h`, adding the enum value in `Page`, adding the `pages[]` glyph row in `main.cpp`, and adding the `case` in `handleButtonPress` and (if relevant) `renderPage` in `main.cpp`.
- `printPage()`'s cache compares all of `DisplayState`. If you add new on-screen state, extend the struct + `operator!=` so the OLED actually refreshes.
- `Wire.setPins()` is in ESP32 Arduino core 2.0+. Older cores need `Wire.end(); Wire.begin(sda, scl);` instead in `Display.cpp`.
