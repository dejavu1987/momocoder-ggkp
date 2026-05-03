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

The firmware is a flat Arduino sketch — `src/main.cpp` is the only translation unit. The other files in `src/` are `.h` headers that are `#include`d **once** from `main.cpp` and contain full definitions (variables and functions live in the headers, not just declarations). Including any of them from a second `.cpp` would produce multiple-definition link errors. `src/globals.h` is the exception: it holds `extern` declarations and is the proper way to share state.

Layout:

- `src/main.cpp` — `setup()` / `loop()`, BLE init, MPU6050 init, RGB LED status, deep-sleep timer.
- `src/Keypad.h` — pin map for the 9 buttons, the `buttonInterrupt()` ISR (FALLING on every button), and `handleButtonPress(page, btn)` dispatch. The active behavior depends on `KEYPAD_PAGE`.
- `src/AirMouse.h` — MPU6050 I²C read/write helpers and the `gyroX/Y/Z` globals consumed by `loop()` to drive `bleCombo.mouseMove`.
- `src/Display.h` — direct U8g2 driver for the EastRising 64x48 OLED via `U8G2_SSD1306_64X48_ER_F_HW_I2C`. The `_ER_` variant applies the 32-column RAM offset that this panel needs (Adafruit_SSD1306 doesn't, which produced half-noise output). `displaySetup(sda, scl)` calls `Wire.setPins(sda, scl)` immediately after `u8g2.begin()` because U8g2 internally calls `Wire.begin()` with default ESP32 pins (21/22), which would unhook the MPU6050.
- `src/globals.h` — shared `extern` declarations for `bleCombo`, `mouseEnabled`, `scrollEnabled`, `dragEnabled`, `mouseSensitivity`, `mouseMoveDelay`.
- `src/*.bkp` — old snapshots, ignored by the PlatformIO builder (only `.c/.cpp/.cc/...` are compiled).

`main.cpp` also does `#include <keyvals.cpp>` — that file ships inside the `BLECombo` library (forked at `https://github.com/earthicko/ESP32-BLE-Combo.git`), not in this repo.

### Runtime model

- All input is interrupt-driven: every button shares one ISR that scans `digitalRead` and stores the GPIO number in `volatile int pressedButton`. `loop()` polls that flag, dispatches, debounces with `delay(200)`, and clears it.
- `KEYPAD_PAGE` cycles 0→1→2→3 via the UP/DN buttons. Pages 0 and 2 enable the air mouse; page 1 is the default keyboard/media layer; page 3 is BLE pairing — pressing OK calls `enterPairingMode()` (in `main.cpp`) which disconnects the current peer, waits for it to drop, calls `NimBLEDevice::deleteAllBonds()`, and restarts advertising. Page semantics live in `handleButtonPressPage0` / `handleButtonPressPage2` / `handleButtonPressPage3` / `handleButtonPressDefault` in `Keypad.h`.
- The MPU6050 is read each loop via `i2cRead(0x3B, …, 14)`; the gyro bytes at offsets 8–13 are scaled by `mouseSensitivity` (with a fixed +2.5× offset on X to cancel idle drift) and pushed through `bleCombo.mouseMove(z, x)`. `scrollEnabled` reroutes the same values to scroll-wheel deltas. `mouseMoveDelay` throttles the loop.
- After 60 s with no button press the device calls `esp_deep_sleep_start()`. Wake source is `GPIO_NUM_6` (BTN_A) configured as `ext0` LOW.
- RGB status LED on GPIO 3 (B) / 45 (R) / 47 (G), driven with `analogWrite`. Green when BLE is connected; fast 150 ms blue blink while `pairingMode` is true and disconnected; otherwise a slow ~3 s red/green/blue cycle (using blocking `delay(1000)`s) while waiting for a host.

### Pin map (do not change without checking the PCB)

```
Keypad (INPUT_PULLUP, FALLING interrupt)   I²C (Wire)
+----+----+----+                            SDA = GPIO 2
|  6 |  8 | 18 |   A   UP  B                SCL = GPIO 1
+----+----+----+                            MPU6050 @ 0x68
|  5 | 15 | 17 |   LT  OK  RT               SSD1306 @ 0x3C
+----+----+----+
|  4 |  7 | 16 |   C   DN  D
+----+----+----+

RGB LED: B=GPIO3, R=GPIO45, G=GPIO47
Wake button (deep sleep ext0): GPIO6 (BTN_A)
```

## Library dependencies (pinned in `platformio.ini`)

`U8g2`, `U8g2_for_Adafruit_GFX`, `Adafruit GFX`, `Adafruit BusIO`, `Adafruit SSD1306`, `NimBLE-Arduino`, and the `BLECombo` fork from `earthicko/ESP32-BLE-Combo`. `USE_NIMBLE` is defined in `main.cpp` so BLECombo links against NimBLE rather than the stock Bluedroid stack — keep both consistent if swapping libraries.

## Editing tips specific to this codebase

- New shared state belongs in `globals.h` as an `extern`, defined once in `main.cpp`. Adding a new `.cpp` that includes `Keypad.h` or `AirMouse.h` will break the link until those headers are split into declaration/definition pairs.
- Per-page button behavior changes go in the matching `handleButtonPressPageN` function in `src/Keypad.h`. Adding a new page also requires updating the wraparound logic in `loop()` (the `if (KEYPAD_PAGE < 0) … > 2 …` block) and the `pages[3][9]` icon table in `main.cpp`.
- The display is wired up but unused; if reviving it, uncomment `displaySetup()` in `setup()` and note that the OLED shares the I²C bus with the MPU6050 on SDA=2 / SCL=1.
