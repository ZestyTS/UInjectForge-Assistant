# UInjectForge Assistant for Wii U

This folder contains the Wii U-side homebrew receiver for `UInjectForge Assistant`.

Current state:

- builds as a WUHB/RPX with devkitPro wut
- embeds the shared UInjectForge product mark, product name, short name, and author in WUHB metadata; SD handoff packages also include `icon.png` and `meta.xml` for legacy RPX-style launchers
- reports its component version, source build ID, and supported capabilities through HTTP status and UDP discovery
- mounts the SD card, initializes the Wii U network stack, and listens on port `8573`
- reads optional settings from `sd:/wiiu/apps/uinjectforge-assistant/config.json`
- shows the Wii U IP address, port, broadcast status, pairing code, protocol version, SD root, and transfer status
- keeps the receiver IP, HTTP port, and pairing code visible in the top bar on every Assistant page
- uses a C++/SDL2/SDL_ttf UI layer instead of raw `OSScreen` debug drawing
- uses fixed pixel layout regions for connection, status, transfer progress, diagnostics, activity, and the bottom action bar so text does not overwrite itself on the TV/GamePad
- exposes Companion-style pages for Overview, Receiver, Pairing, Transfer, Results, Activity, Packages, Log, Title Storage, Network, Health, Setup Check, Diagnostics, Controls, and Config
- keeps the older one-screen receiver/status/transfer/diagnostics surfaces available through the Overview, Receiver, Transfer, Activity, and Diagnostics pages
- keeps Up/Down in the sidebar until A or Open List explicitly enters Packages or Title Storage; B returns to the sidebar without leaving the page
- supports GamePad touch for pages, list rows, refresh, storage target selection, and confirmation; selecting a row never installs or launches it by itself
- uses X (Wii Remote 1) or the on-screen Refresh button to rescan after external FTP/SD changes
- caches fonts, bounded text textures, package checks, setup checks, free-space reads, and controller summaries instead of repeating that work on each redraw; installation still validates the selected package immediately before use
- defaults to a production receiver UI with pairing, receiver address, transfer progress, and one latest-event line so TV/GamePad output stays readable
- can run the full Companion-style dashboard pages through config when intentionally collecting advanced receiver evidence
- shows a QR code on the Pairing page that opens the receiver status page with the current IP, port, and pairing code
- shows folders from `sd:/install` so testers can confirm copied WUP packages from the Wii U side
- lets the user select a validated `sd:/install` package on the optional full Packages page, choose NAND or USB, and start a console-confirmed install; the compact receiver screen remains the default UI
- uses the system title catalog as a fallback when direct NAND/USB folder browsing is restricted; folder access and installation availability are reported separately
- writes receiver activity to `sd:/uinjectforge/assistant/assistant.log`, reports that path in the status endpoint, shows a log-tail page on-console, and exposes a paired log-tail endpoint for Desktop diagnostics
- draws startup progress before SD, config, network, socket, and listener setup so a startup failure leaves a visible last step
- keeps the HTTP listener and UDP discovery socket non-blocking so the receiver loop can continue polling controller exit input
- clears nonblocking mode on each accepted HTTP client socket so status/pair/upload request reads wait for incoming bytes instead of intermittently returning `400 Bad Request`
- replies to UIF discovery probes on the configured UDP port by default so Desktop can find the receiver even when passive broadcast packets are unreliable
- serves a browser-friendly `GET /` status page so mobile users can verify the receiver IP, port, pairing code, and current state before starting a UIF transfer
- can send passive UDP discovery beacons when `passiveBroadcastEnabled` is set, but keeps passive broadcast off by default because some Wii U network environments have frozen on broadcast send
- explicitly enables Wii Remote and Wii U Pro Controller input through WPAD/KPAD
- accepts the first paired transfer protocol used by UIF Desktop/Android
- validates transfer manifests, paths, SD free space, file sizes, and SHA-256 hashes before committing files
- reports SD free/total space, active transfer required space, and whether the current transfer appears to fit in `GET /api/v1/status`
- reports per-file uploaded state from `GET /api/v1/transfers/{id}` so Desktop can recover a transfer if an upload reached the receiver but the HTTP response was lost
- keeps the Wii U lifecycle and controller input responsive during large uploads, reports the currently receiving file and byte progress, and rejects a connection that stops sending data for 30 seconds
- hashes each file while streaming it to SD, then verifies the persisted size and validates the committed staging set before publication
- stages files under `sd:/uinjectforge/assistant/incoming/<transfer-id>/`
- clears only interrupted `incoming` staging on the next launch while preserving recovery backups for inspection
- moves validated files to the final SD destination only after every file validates
- can install validated non-system WUP packages; manual installs require on-console confirmation and automatic installs require an explicit UIF action
- exposes read-only `sd:/install` package summaries after pairing, including title-ticket/TMD/cert presence and `.app` / `.h3` counts
- exposes read-only installed-title path hints after pairing and fills display names/product codes from `meta/meta.xml` when readable
- enables the Wii U HOME menu while idle or transferring; locks HOME and in-app navigation during installation, then restores the previous HOME state after completion or a completed abort
- disables the earlier `B`/`+` fallback close path; HOME is the only supported exit path
- preserves the hardware-confirmed ProcUI exit path: HOME-owned shutdown skips app-owned socket/controller/SDL teardown and lets the system reclaim those resources
- retains asynchronous installation buffers until the IOS completion callback has finished, including when cancellation is requested; an external process exit never frees an outstanding IOS request
- uses the WHBProc app lifecycle with a lightweight SDL video event pump each frame so SDL can process Wii U window/system events while direct VPAD/KPAD polling keeps full ownership of navigation
- requests a 1280x720 SDL window like hb-appstore/Chesto and fits the 1920x1080 UIF dashboard into the actual renderer output
- polls Wii Remote, Classic Controller, and Wii U Pro Controller channels for navigation input
- reports detected Wii Remote, Classic Controller, and Wii U Pro Controller input in the status endpoint
- reports broadcast socket state plus discovery probe/reply and loop-heartbeat counters in the status endpoint so UDP discovery failures can be diagnosed after a manual HTTP status check
- reports HTTP request/error counters, the last HTTP request/error, and pairing attempt/failure counters so pairing-code and timeout reports can be diagnosed from Desktop
- adds a full-dashboard Setup Check page that verifies SD access/free space, Assistant files, Aroma Bridge, FTPiiU, Nintendont, NooDS, PPSSPP runtime/assets, WiiStation, Not64 (including adjacent `data.iso`), Genesis Plus GX, controller INIs, Loadiine, and valid versus incomplete `sd:/install` packages
- exposes the same versioned setup object from HTTP status and UDP discovery so Desktop/mobile diagnostics do not need a separate console-readiness vocabulary
- has hardware-confirmed launch, pairing, validated receipt, HOME exit, package browsing, and manual installation; overlapping batch auto-install and complete title-location reporting still need hardware proof

Optional config:

```json
{
  "httpPort": 8573,
  "broadcastEnabled": true,
  "passiveBroadcastEnabled": false,
  "broadcastPort": 8573,
  "simpleUi": true,
  "uiMode": "simple",
  "installEnabled": true,
  "manualInstallEnabled": true,
  "autoInstallEnabled": false
}
```

`broadcastPort` defaults to `httpPort` when omitted. `broadcastEnabled` controls the UDP discovery socket and direct probe replies. `passiveBroadcastEnabled` controls unsolicited broadcast beacons and defaults to `false`; leave it off unless you are specifically testing passive discovery. `installEnabled` is the global install kill switch. `manualInstallEnabled` defaults to `true`; `autoInstallEnabled` defaults to `false` and must be enabled before a paired UIF batch can install without a separate on-console action. System-title packages are never accepted. The reduced screen is the default. Set `simpleUi` to `false` or `uiMode` to `full`, `dashboard`, or `advanced` to use the full page dashboard. The SDL window request follows hb-appstore's Wii U 1280x720 baseline, then the renderer fits UIF's 1920x1080 design into the actual output. If UIF Desktop can reach `Check Status` by manually entering the visible Wii U IP address but `Find Wii U` fails, HTTP transfer can still work; UDP discovery is often blocked by router, subnet, or Windows firewall rules.

For discovery troubleshooting, run `Find Wii U` once in UIF, then run `Check Status` with the visible IP address. If `discoveryProbeCount` stays `0`, the Wii U never saw the UDP probe. If the count increases but Desktop still does not auto-fill the receiver, the return UDP packet is likely blocked on the Desktop/Android side.

For pairing troubleshooting, run `Check Status` after a failed pair attempt and record `pairingAttemptCount`, `pairingFailureCount`, `lastHttpRequest`, and `lastHttpError`. The receiver accepts the code from UIF's JSON body and also tolerates form data, raw digits, and numeric JSON so reports of a visibly correct code being rejected should leave a diagnostic reason instead of silently timing out.

The versioned `setup` object also reports nullable `wiiStationInstalled`, `not64Installed`, and `genesisPlusGxInstalled` fields. Current UIF clients use those fields to validate a selected guided vWii forwarder preset when the SD card is not mounted on the client device. Older Assistant builds remain compatible because missing fields deserialize as unknown rather than failed.

Implemented receiver endpoints:

- `GET /api/v1/status`
- `GET /`
- `POST /api/v1/pair`
- `POST /api/v1/transfers`
- `GET /api/v1/transfers/{id}`
- `PUT /api/v1/transfers/{id}/files/{relativePath}`
- `POST /api/v1/transfers/{id}/commit`
- `DELETE /api/v1/transfers/{id}`
- `GET /api/v1/titles`
- `GET /api/v1/install-packages`
- `GET /api/v1/log`

The receiver allows one active transfer at a time and rejects a second manifest until the current transfer is committed or canceled. Repeating commit or cancel for the last finalized transfer returns the retained terminal result, so a lost HTTP response can be reconciled safely. A paired sender can cancel active staging files.

Manual installation is enabled by default but remains explicit. On the full dashboard, enter Packages with A or Open List, select a non-system-title folder, choose Install, select NAND or USB, and confirm. Touch uses the same confirmation. B cancels a confirmation or returns to the sidebar; it does not exit the app. Automatic installation runs only when Desktop/mobile sends the `auto-install-after-validation` action and the receiver has `autoInstallEnabled=true`; otherwise the validated package remains ready on SD for manual installation. The default transfer action is copy-only. Set `installEnabled=false` as a global kill switch. Installs request an abort after 15 minutes without byte progress or 90 minutes total runtime. HOME remains locked until the install or abort finishes; do not turn off the console during that time. The reduced/simple screen remains transfer-focused; use `simpleUi=false` or `uiMode=full` for the manual install page.

`GET /api/v1/titles` retains `nandReadable` and `usbReadable` as direct folder-access flags and adds `catalogAvailable`. A false folder flag does not mean that MCP installation is unavailable. When the system catalog is the only readable source, titles may use their title ID instead of a friendly name and omit runtime hashes. Those are unknown values, not fabricated metadata.

Transfer manifests can mark an installable package as requiring Wii U system memory. The receiver then overrides automatic installation to NAND and locks that package to NAND on the manual Packages page for the current receiver session. Cafe2Wii is the first workflow that uses this constraint because the current Aroma c2w patcher only redirects titles launched from MLC.

Supported final SD destinations:

- installable WUP package: `sd:/install/<package>/`
- Loadiine package: `sd:/wiiu/games/<package>/`
- SD runtime package: relative to the SD root

Build with Docker:

```powershell
docker run --rm -w /app -v "I:/UInjectForge_Rebuild/UInjectForge/tools/UInjectForge.Assistant.WiiU:/app" wiiuenv/devkitppc:20220907 sh -lc "set -e; make clean; make -j2"
```

The Docker image needs SDL2 and SDL_ttf Wii U portlibs available. If the image lacks those packages, build with a devkitPro install that has `wiiu-sdl2` and `wiiu-sdl2_ttf` installed.

Build with a local devkitPro/wut install:

```sh
make
```

Run the platform-independent security/configuration tests with any C11 compiler:

```sh
make host-test
```

Run the real C++ UI against host SDL with mocked Wii U input/storage APIs (from this folder):

```sh
docker build -f tests/Dockerfile.host -t uinjectforge-assistant-host-tests .
docker run --rm -v "$PWD:/app" -w /app uinjectforge-assistant-host-tests make host-test ui-host-test
```

These tests cover explicit list focus/back, separate install confirmation, touch coordinate transforms, Wii Remote/Classic/Pro input, install navigation lock, restricted-folder catalog fallback, cache reuse, and 60 rendered pages across GamePad/TV/4:3 sizes. They write screenshots under `build-host/screenshots`. They do not prove console installation or HOME behavior.

Output files are written beside the Makefile:

- `uinjectforge-assistant.wuhb`
- `uinjectforge-assistant.rpx`

Create a tester SD-root overlay ZIP from the repo root:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\Scripts\Build-WiiUAssistantPackage.ps1
```

That package is intentionally shaped as `sd/wiiu/apps/uinjectforge-assistant/...`; testers should copy the contents of the generated `sd` folder to the Wii U SD card root.

Latest local SDL build hashes:

- `uinjectforge-assistant.rpx` SHA-256 `23698D6C7104511BF3156F8C0DD9D970A2C7A64E32FC745E8EAE05584A1A68B2`
- `uinjectforge-assistant.wuhb` SHA-256 `FCBBC5B9FAAFF59A58B8E7C4F919710E94D2D6A84CC573FC93AF37E702775895`

Rebuild before handing the Assistant binary to testers if any source file in this folder changes.

Treat this as a technical-alpha component. Copy/manual-install behavior is hardware-confirmed; keep automatic batch installation opt-in until its USB/NAND and failure-recovery checklist passes on real hardware.
