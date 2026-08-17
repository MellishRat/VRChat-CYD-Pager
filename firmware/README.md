# Firmware source

Open `VRChat_CYD_Pager/VRChat_CYD_Pager.ino` in Arduino IDE 2.x.

## Reproducible build

- Board package: **esp32 by Espressif Systems 3.3.11**
- Board: **ESP32 Dev Module** (`esp32:esp32:esp32`)
- Flash size: **4 MB**
- Partition scheme: **Default 4 MB with spiffs**
- Flash mode/frequency: **DIO / 80 MHz**
- CPU frequency: **240 MHz**
- PSRAM: **Disabled**

The sketch intentionally uses only libraries bundled with the ESP32 Arduino core.
Run `scripts/build-firmware.ps1` from PowerShell to compile, export, merge, hash, and
copy the browser-installable image into `docs/firmware/`.

## Pin map

| Function | GPIO |
|---|---:|
| ILI9341 SCK | 14 |
| ILI9341 MOSI | 13 |
| ILI9341 MISO | 12 |
| ILI9341 CS | 15 |
| ILI9341 DC | 2 |
| ILI9341 reset | 4 |
| Backlight | 21 |
| BOOT/config reset | 0 |

