# Troubleshooting

## Browser installer

Use desktop Chrome or Edge and a USB data cable. Close Arduino Serial Monitor
and any other program holding the COM port. If connection fails, hold **BOOT**,
start the installer, release **BOOT** when it says *Connecting*, and briefly
press **EN/RST** if necessary.

## Wi-Fi selection

Wi-Fi setup happens entirely on the CYD touchscreen. The pager scans nearby
networks; tap one and enter its password with the on-screen keyboard. ESP32
supports 2.4 GHz Wi-Fi, not 5 GHz-only networks. Open networks are supported by
submitting an empty password. Saved credentials are reused after restart.

To select a different network later, open **Advanced** and tap **Change Wi-Fi**.

## VRChat login and 2FA

Enter the VRChat account username and password on the touchscreen. If VRChat
requires TOTP or email OTP verification, enter the current code on the numeric
screen. The authenticated session cookies are stored in ESP32 Preferences, so a
normal restart does not require signing in again.

Credentials and verification codes are sent directly from the ESP32 to the
official VRChat API over HTTPS. There is no companion app, OSC bridge, captive
portal, or browser-based device configuration.

## Pipeline and notifications

The pager connects directly to `pipeline.vrchat.cloud` over secure WebSockets.
It handles notification, notification-v2, friend, group, boop, invite, and
system events. If updates stop, open **Advanced** and tap **Reconnect**. The
firmware also retries disconnected Wi-Fi and Pipeline connections automatically.

## Touch, display, LED, and audio

This build targets the 2.8-inch ESP32-2432S028R with the ILI9341-compatible
display and XPT2046 touch controller. A wrong orientation, white screen, or
unresponsive touch usually indicates a different CYD hardware revision.

The backlight sleeps after three minutes without touch. The first touch wakes
the display without activating the button underneath it. Notifications also
wake the screen and use the onboard RGB LED and GPIO 26 audio output.
