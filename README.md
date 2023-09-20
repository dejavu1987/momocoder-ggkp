# momocoder-ggkp

Momocoder GGKP / Smart controller w/ Air Mouse 📝 [Blog post](https://anilmaharjan.com.np/blog/momocoder-ggkp-a-diy-smart-controller/)
![](https://res.cloudinary.com/anilmaharjan-com-np/image/upload/c_scale,h_620,w_827/v1/Blog/Momocoder_GGKP_smart_controller.png?_a=ATFGlAA0)
![](https://res.cloudinary.com/anilmaharjan-com-np/image/upload/v1695149918/Blog/1845356405208.png)

## Hardware components

- ESP32-S3-WROOM-1-N8
- GY521 MPU 6050 Module
- OLED 0.66in 64 x 48 px

## Keypad

```
Input key matrix
Button names
+---+---+---+
| A | UP| B |
+---+---+---+
| LT| OK| RT|
+---+---+---+
| C | DN| D |
+---+---+---+

Pin numbers (GPIO)
+---+---+---+
|  6|  8| 18|
+---+---+---+
|  5| 15| 17|
+---+---+---+
|  4|  7| 16|
+---+---+---+
*/
```

## Features

Motion Control: Navigate with a wave of your hand, thanks to its integrated gyroscope and accelerometer.

Seamless Connectivity: Easily connect to Bluetooth devices using Bluetooth Low Energy (BLE).

Powered by ESP32-S3: Experience top-notch performance and efficiency with this microcontroller.

Compact .66' OLED Display: The compact OLED screen allows limitless possibilities for the device.

Compact and Portable: Its tiny form factor makes it perfect for on-the-go use. The PCB is below 3 cm x 5 cm.

Quick Shortcuts (Macros): Customize your experience with six programmable buttons for shortcuts.

User-Friendly Configuration: Effortlessly personalize the Air Mouse via a Wi-Fi web interface.

### Other features:

The board has breakout pins and pads for most of the pins from esp32-s3, including dedicated pinouts for JTAG and UART, the board can be easily reprogrammed using the USB with custom sketches.
