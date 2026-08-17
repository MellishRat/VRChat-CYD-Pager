# VRChat CYD Pager

A standalone VRChat notification pager for the common **ESP32-2432S028R Cheap
Yellow Display**. It connects directly to VRChat's API and Pipeline WebSocket—no
OSC router, companion application, or captive portal is involved.

## Install

Open the [browser installer](https://mellishrat.github.io/VRChat-CYD-Pager/) in
desktop Chrome or Edge and connect the CYD over a data-capable USB cable.

After installation, use the CYD touchscreen to select your Wi-Fi network and
enter its password. Then enter your VRChat credentials on the device and finish
TOTP or email verification if requested. The authenticated session is saved.

## Repository layout

| Path | Contents |
|---|---|
| `firmware/VRChat_CYD_Pager/` | Arduino source |
| `docs/` | GitHub Pages installer and help |
| `docs/firmware/` | Versioned merged binary and checksum |
| `scripts/build-firmware.ps1` | Reproducible Windows build/release script |
| `.github/workflows/` | Compile verification and Pages deployment |

## Features

- On-device Wi-Fi scan, password entry, and saved credentials.
- Direct VRChat login with TOTP and email OTP support.
- Saved authentication session and quiet Pipeline reconnection.
- Invites, friend events, boops, group events, and system alerts.
- Touchscreen notification detail views, RGB LED alerts, and audio cues.
- Configurable time zone and three-minute display timeout.

## Build from source

See [firmware/README.md](firmware/README.md). Firmware 0.5.0 is built with ESP32
Arduino core 3.3.11 and the pinned libraries documented there.

## Release checklist

1. Update the version in the sketch, manifest, file names, changelog, and page.
2. Run `powershell -ExecutionPolicy Bypass -File scripts/build-firmware.ps1`.
3. Verify the SHA-256 and browser-install the merged image on real hardware.
4. Tag `vX.Y.Z`; attach the `.bin`, `.sha256`, and source archive to the release.

Licensed under the [MIT License](LICENSE).
