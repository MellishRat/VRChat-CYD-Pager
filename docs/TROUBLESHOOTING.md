# Troubleshooting

## Browser installer

The installer requires desktop Chrome, Edge, or another Chromium browser with
Web Serial. Firefox, Safari, iOS browsers, and embedded social-media browsers do
not expose serial devices. Use a USB cable that carries data, not a charge-only
cable. Close Arduino IDE's Serial Monitor and any other program holding the COM
port.

If the ESP32 does not enter its bootloader, hold **BOOT**, start the installer,
release **BOOT** when it says *Connecting*, and briefly press **EN/RST** if needed.

## Wi-Fi

The ESP32 supports 2.4 GHz Wi-Fi only. WPA2 networks are recommended. Captive,
enterprise (802.1X), and many guest networks are unsuitable. Guest/AP isolation
must be disabled so the PC can send UDP packets to the pager.

Hold **BOOT** while resetting to force setup mode. Holding **BOOT** for five
seconds while running erases the stored Wi-Fi settings.

## OSC

VRChat sends OSC to the loopback interface. It does not send directly to the
pager. Configure an OSC router/bridge on the VRChat PC:

1. Receive VRChat OSC locally.
2. Forward `/chatbox/input` to the pager IP shown on its footer.
3. Use UDP port `9001`, unless you changed it in the pager web interface.

You can test without VRChat by sending an OSC message with one string argument
to `/cyd/message`. If that works, the firmware and network are healthy and the
remaining issue is the PC-side route or firewall.

## Display

This release targets the common 2.8-inch ESP32-2432S028R with an ILI9341 display.
A white screen, inverted colours, or corrupted geometry usually means the board
is a different CYD revision/display controller and needs a matching build.

