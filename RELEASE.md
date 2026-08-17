# Release procedure

Releases use semantic version tags such as `v1.0.0`. The browser installer
always points to a versioned, merged firmware image committed under
`docs/firmware/` so an old release remains identifiable and reproducible.

## Prepare

1. Update `VERSION` in the Arduino sketch.
2. Update the version and binary path in `docs/manifest.json`.
3. Update the download links in `docs/index.html` and version in `CHANGELOG.md`.
4. Run `scripts/build-firmware.ps1` on Windows with Arduino IDE 2.x and ESP32
   board package 3.3.11 installed.
5. Confirm CI compiles the source, then browser-flash the generated merged image
   onto an ESP32-2432S028R and perform the smoke test below.

## Hardware smoke test

- New install opens the touchscreen Wi-Fi scan.
- On-device password entry connects to a 2.4 GHz network and survives restart.
- VRChat username/password login and TOTP or email OTP verification work.
- A successful session survives restart and reconnects to the Pipeline.
- Invites, friend events, boops, group events, and system alerts render correctly.
- Touch navigation, RGB notification alerts, audio, and display timeout work.

## Publish

Create an annotated `vX.Y.Z` tag and a GitHub release. Attach the merged `.bin`,
its `.sha256` file, and the automatically generated source archives. GitHub
Pages deploys the checked-in installer independently from `main`.
