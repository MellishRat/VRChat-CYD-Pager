# VRChat CYD Pager

A Wi-Fi OSC message display for the common **ESP32-2432S028R Cheap Yellow
Display**. Flash it from a browser, connect it to 2.4 GHz Wi-Fi, then forward
VRChat chatbox OSC messages to the IP shown on screen.

## Install

Open the [browser installer](https://mellishrat.github.io/VRChat-CYD-Pager/) in
desktop Chrome or Edge and connect the CYD over a data-capable USB cable.

After installation, join the `CYD-Pager-xxxx` Wi-Fi network and enter your Wi-Fi
details at `http://192.168.4.1`. The pager displays its LAN address and UDP port.

> VRChat emits OSC locally on the PC. An OSC router/bridge must forward
> `/chatbox/input` to the pager's LAN address. See
> [Troubleshooting](docs/TROUBLESHOOTING.md#osc).

## Repository layout

| Path | Contents |
|---|---|
| `firmware/VRChat_CYD_Pager/` | Arduino source |
| `docs/` | GitHub Pages installer and help |
| `docs/firmware/` | Versioned merged binary and checksum |
| `scripts/build-firmware.ps1` | Reproducible Windows build/release script |
| `.github/workflows/` | Compile verification and Pages deployment |

## Controls and endpoints

- Hold **BOOT during reset**: force setup mode.
- Hold **BOOT for 5 seconds**: erase Wi-Fi settings and restart.
- `http://<pager-ip>/`: change Wi-Fi, name, or UDP port.
- OSC `/chatbox/input`: reads the first string argument from VRChat chatbox data.
- OSC `/cyd/message`: reads one string argument for direct testing/integrations.

## Build from source

See [firmware/README.md](firmware/README.md). Release 1.0.0 is built with ESP32
Arduino core 3.3.11 and contains no third-party Arduino libraries.

## Release checklist

1. Update the version in the sketch, manifest, file names, changelog, and page.
2. Run `powershell -ExecutionPolicy Bypass -File scripts/build-firmware.ps1`.
3. Verify the SHA-256 and browser-install the merged image on real hardware.
4. Tag `vX.Y.Z`; attach the `.bin`, `.sha256`, and source archive to the release.

Licensed under the [MIT License](LICENSE).
