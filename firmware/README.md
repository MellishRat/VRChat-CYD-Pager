# Original firmware source

`VRChat_CYD_Pager/VRChat_CYD_Pager.ino` is the complete stable direct-pipeline
sketch supplied for this project. It scans and joins Wi-Fi through the CYD
touchscreen, authenticates directly with VRChat, supports TOTP/email OTP, saves
the authenticated session, and consumes the VRChat Pipeline WebSocket. It does
not use OSC or a captive portal.

## Reproducible build

- Board package: **esp32 by Espressif Systems 3.3.11**
- Board: **ESP32 Dev Module** (`esp32:esp32:esp32`)
- Flash size: **4 MB**
- Partition scheme: **Default 4 MB with spiffs**
- Flash mode/frequency: **DIO / 80 MHz**
- CPU frequency: **240 MHz**
- PSRAM: **Disabled**

Libraries: TFT_eSPI 2.5.43, XPT2046_Touchscreen 1.4, ArduinoJson 7.4.3, and
ArduinoWebsockets 0.5.4. Replace TFT_eSPI's `User_Setup.h` with
`firmware/TFT_eSPI_User_Setup.h` before compiling. Run
`scripts/build-firmware.ps1` to compile, merge, hash, and copy the browser image.

## Pin map

| Function | GPIO |
|---|---:|
| ILI9341 SCK | 14 |
| ILI9341 MOSI | 13 |
| ILI9341 MISO | 12 |
| ILI9341 CS | 15 |
| ILI9341 DC | 2 |
| ILI9341 reset | Connected to board reset (`-1` in TFT_eSPI) |
| Backlight | 21 |
| Touch CS / IRQ | 33 / 36 |
| Touch MOSI / MISO / CLK | 32 / 39 / 25 |
| RGB LED | 4 / 16 / 17 |
| Audio | 26 |
