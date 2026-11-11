# UInjectForge Assistant for Wii U

This folder contains the Wii U-side homebrew receiver for `UInjectForge Assistant`.

Development builds add on-console editing for every supported configuration
setting and opt-in remembered devices with matching UIF 1.0.6 clients.
See [pairing behavior and protocol](PAIRING.md). This is not included in the
September 21 public release yet.

## Get Started

Download the SD package from [Assistant releases](https://github.com/ZestyTS/UInjectForge-Assistant/releases).
Copy the **contents** of its `sd` folder to the Wii U SD card, preserving an existing
`config.json`. Launch the WUHB from Aroma and keep it open during transfers.
In UIF, open the Wii U tab, use Find Wii U (or the displayed
IP and port), and enter the pairing code shown on the console. No Bridge plugin
is required. Use a trusted local network; do not forward these ports to the Internet.

Tap **Packages & tools**, or press Y (Wii Remote 2), to open manual installation,
title storage, diagnostics and settings. **Y Receiver** returns to the compact
screen. In the 1.0.6 development candidate, tap **Settings** on the receiver, or
open **Settings** in the dashboard, to edit all supported config values. These
controls are not present in the older public/1.0.4-test build. Opening a page never starts an install;
package and target confirmation are still required. Automatic installs are off
until explicitly enabled on the console and requested for a transfer in UIF.

## Implementation

Current state:

- builds as a WUHB/RPX with devkitPro wut
- embeds the shared UInjectForge product mark, product name, short name, and author in WUHB metadata; SD handoff packages also include `icon.png` and `meta.xml` for legacy RPX-style launchers
- reports its component version, source build ID, and supported capabilities through HTTP status and UDP discovery
- mounts the SD card, initializes the Wii U network stack, and listens on port `8573`
- reads settings from `sd:/wiiu/apps/uinjectforge-assistant/config.json`; first successful boot saves a generated `pairingCode` and later launches reuse it, while explicit Forget paired devices rotates and persists it
- shows the Wii U IP address, port, broadcast status, pairing code, protocol version, SD root, and transfer status
- keeps the receiver IP, HTTP port, and pairing code visible in the top bar on every Assistant page
- uses a C++/SDL2/SDL_ttf UI layer instead of raw `OSScreen` debug drawing
- uses fixed pixel layout regions for connection, status, transfer progress, diagnostics, activity, and the bottom action bar so text does not overwrite itself on the TV/GamePad
- exposes five primary pages: Receiver, Packages, Installed Games, Settings, and Diagnostics; all fit in the sidebar without paging
- combines connection, transfer progress, and the last transfer result in Receiver; detailed status, setup checks, the SD log, and remembered devices are under Diagnostics
- keeps Up/Down in the sidebar until A or Open List explicitly enters Packages or Installed Games; B returns to the sidebar without leaving the page
- supports GamePad touch for pages, list rows, refresh, storage target selection, and confirmation; selecting a row never installs or launches it by itself
- uses X (Wii Remote 1) or the on-screen Refresh button to rescan after external FTP/SD changes
- caches fonts, bounded text textures, package checks, setup checks, free-space reads, and controller summaries instead of repeating that work on each redraw; installation still validates the selected package immediately before use
- defaults to a production receiver UI with pairing, receiver address, transfer progress, and one latest-event line so TV/GamePad output stays readable
- opens the full dashboard from Packages & tools; config can still select the startup layout
- shows a QR code on the Pairing page that opens the receiver status page; pairing codes remain on-console, not in the public HTTP response
- shows folders from `sd:/install` so testers can confirm copied WUP packages from the Wii U side
- lets the user select a validated `sd:/install` package on the optional full Packages page, choose NAND or USB, and start a console-confirmed install; the compact receiver screen remains the default UI
- uses the system title catalog as a fallback when direct NAND/USB folder browsing is restricted; folder access and installation availability are reported separately
- writes receiver activity to `sd:/uinjectforge/assistant/assistant.log`, reports that path in the status endpoint, shows a log-tail page on-console, and exposes a paired log-tail endpoint for Desktop diagnostics
- runs SD recovery and network/listener setup on a background thread while the main ProcUI, SDL, touch, and controller loop keeps rendering startup progress and handling HOME
- relies on wut's pre-main socket/Auto Connect initialization instead of initializing the same libraries twice; the remaining blocking network wait stays on the startup worker
- keeps the HTTP listener and UDP discovery socket non-blocking so the receiver loop can continue polling controller exit input
- clears nonblocking mode on each accepted HTTP client socket so status/pair/upload request reads wait for incoming bytes instead of intermittently returning `400 Bad Request`
- replies to UIF discovery probes on the configured UDP port by default so Desktop can find the receiver even when passive broadcast packets are unreliable
- serves a browser-friendly `GET /` status page so mobile users can verify the receiver IP, port, pairing code, and current state before starting a UIF transfer
- can send passive UDP discovery beacons when `passiveBroadcastEnabled` is set, but keeps passive broadcast off by default because some Wii U network environments have frozen on broadcast send
- explicitly enables Wii Remote and Wii U Pro Controller input through WPAD/KPAD
- accepts the first paired transfer protocol used by UIF Desktop/Android
- validates transfer manifests, paths, SD free space, file sizes, and SHA-256 hashes before committing files
- exposes startup view, display size, network discovery/ports, and installation permissions in Settings; stages edits with Save/Cancel/defaults, accepts controller or touch input, and preserves unknown config members when saving
- keeps automatic installs on the compact receiver screen as a confirmed shortcut using the same safe config writer; applies display/install changes live and defers network changes until restart without misreporting the active endpoint
- reports SD free/total space, active transfer required space, and whether the current transfer appears to fit in `GET /api/v1/status`
- reports per-file uploaded state from `GET /api/v1/transfers/{id}` so Desktop can recover a transfer if an upload reached the receiver but the HTTP response was lost
- keeps the Wii U lifecycle and controller input responsive during large uploads, reports the currently receiving file and byte progress, and rejects a connection that stops sending data for 30 seconds
- shows overall and per-file percentages/bars, current filename, average MiB/s and time since the last data/read activity in Receiver; idle screens do not show zero-byte progress, and the independent pre-commit SD hash pass has its own advancing verification progress and lifecycle/input polling
- development builds temporarily suspend runtime automatic power-down during transfers, queued installs and installation; restore an initially enabled runtime setting when idle or exiting, leave an initially disabled setting alone, and do not change persistent System Settings or intercept the Power button
- development builds coalesce fragmented uploads with bounded 256 KiB read/SD buffers, service lifecycle/input/control requests on a 25 ms cadence rather than every data chunk, and retain 500 ms progress redraws; per-file activity timing separates receive wait, SHA verification, SD writes/flush and service/render work, without removing the independent pre-commit hash pass
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
  "autoInstallEnabled": false,
  "allowUiScale": false,
  "uiScale": 1.0
}
```

### Settings In The App

| Section | Controls | Config fields |
| --- | --- | --- |
| Interface | Receiver/Dashboard startup view, custom display size, 45-100% size | `simpleUi`, `uiMode`, `allowUiScale`, `uiScale` |
| Network | Discovery, periodic broadcasts, HTTP and UDP ports (1-65535) | `broadcastEnabled`, `passiveBroadcastEnabled`, `httpPort`, `broadcastPort` |
| Installation | Global permission, manual installation, automatic installation | `installEnabled`, `manualInstallEnabled`, `autoInstallEnabled` |

Select Settings, then A to enter its controls. Up/Down moves between controls;
Left/Right changes the selected value or section. A opens a port keypad or
activates the selected command; B backs out. Touch uses the same editor.
Edits remain staged even when visiting another page. Save applies them, Cancel
changes discards them, and Restore defaults only stages defaults until Save.
Enabling effective automatic installation requires a separate confirmation.
Saving is blocked during active transfers, queued automatic installs and installs.

Successful saves update `config.json` with consistent `simpleUi`/`uiMode` values
and retain unknown JSON members. The previous file remains as `config.json.bak`;
a missing primary after an interrupted rename is read from that backup. Malformed,
oversized or unreadable config files are not silently overwritten; the error is
shown and installation permissions fail closed. Display and installation changes
apply immediately; network changes show a restart notice and take effect after
closing/reopening Assistant. Settings remains available after a listener startup
failure, provided the SD card was mounted.

Older UI builds wrote `sd:/uinjectforge/assistant/preferences.json`. That legacy
automatic-install preference is imported until the first successful settings
save, which writes the internal `settingsSchemaVersion: 1` migration marker.
Subsequent starts use `config.json`; a stale preference cannot override the user.
The marker is internal metadata, not a user setting. No existing config is
rewritten merely by opening Assistant or the editor.

`broadcastPort` defaults to `httpPort` when omitted. Discovery controls the UDP
socket and probe replies; periodic broadcasts default off because some Wii U
network environments have frozen during broadcast sends. Manual installation
defaults on, automatic installation defaults off, and the global permission
overrides both without erasing their remembered choices. System-title packages
are never accepted. `uiMode` overrides `simpleUi` when both occur in an externally
edited file; `full`, `dashboard`, and `advanced` select the dashboard, while
`simple` and `fallback` select the receiver. Custom sizing is off by default.

The SDL window request follows the Wii U 1280x720 baseline and fits the 1920x1080
design to the actual output. If Check Status works using the displayed address
but Find Wii U fails, HTTP transfers can still work; UDP may be blocked by the
router, subnet, or desktop firewall. Changing a port also requires updating any
manually entered endpoint in UIF after restarting Assistant.

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
- `POST /api/v1/install-packages/install` (development capability `install-existing-usb-v1`)
- `GET /api/v1/log`

The receiver allows one active transfer at a time and rejects a second manifest until the current transfer is committed or canceled. Repeating commit or cancel for the last finalized transfer returns the retained terminal result, so a lost HTTP response can be reconciled safely. A paired sender can cancel active staging files.

Manual installation is enabled by default but remains explicit. On the full dashboard, enter Packages with A or Open List, select a non-system-title folder, choose Install, select NAND or USB, and confirm. Touch uses the same confirmation. B cancels a confirmation or returns to the sidebar; it does not exit the app. Automatic installation runs only when Desktop/mobile sends the `auto-install-after-validation` action and the receiver has Automatic installs enabled; otherwise the validated package remains ready on SD for manual installation. Press X or tap Auto install on the compact screen to change that permission. The default transfer action is copy-only. Set `installEnabled=false` as a global kill switch. Installs request an abort after 15 minutes without byte progress or 90 minutes total runtime. HOME remains locked until the install or abort finishes; do not turn off the console during that time. The reduced/simple screen remains transfer-focused; use `simpleUi=false` or `uiMode=full` for the manual install page.

`GET /api/v1/titles` retains `nandReadable` and `usbReadable` as direct folder-access flags and adds `catalogAvailable`, `catalogStage` and `catalogError`. A false folder flag does not mean that MCP installation is unavailable. When the system catalog is the only readable source, titles may use their title ID instead of a friendly name and omit runtime hashes. Those are unknown values, not fabricated metadata. Catalog allocation can fall back to the aligned application heap. A changing, partially understood or unreadable catalog is not reported as complete; `not-requested` means the catalog was unnecessary for that response, not that it failed.

Catalog diagnostics also include `catalogExpectedCount` (before the list call),
`catalogReturnedCount`, `catalogObservedCount` (after the list call),
`catalogCapacityBytes` and `catalogAttempts`. Unknown counts are `-1`, not zero;
older receivers omit these fields. Count disagreements get at most three fresh
snapshots, each with its own allocation and pre/list/post checks. Only a stable
snapshot can publish records. Blank title-ID records fail as `invalid-record`
before visitor callbacks, even if the list call reported success. A stable zero
count is checked twice. These safeguards do not establish the cause of a real
console's count mismatch or permit remote installation from an incomplete list.

Paired, idle-only `GET /api/v1/diagnostics/title-catalog/<mode>` exports records
for private troubleshooting. Modes are `count-sized`, `expanded` (4,096-record
buffer), `app-type/<eight hexadecimal digits>` and `device/usb`, `device/mlc`,
`device/odd` (the same bounded buffer). Device filters use the SDK's USB,
internal-storage and disc-device list calls; auto/unknown device types are not
accepted. Compare repeated per-device records and their union with the ordinary
list to investigate missing or duplicate locations. Matching the numeric count
arithmetically is still diagnostic evidence, not installation permission.
Records include title ID, indexed device, path, app type, version and validity.
Each response records pre/list/post counts, capacity, exported count and error.
`diagnosticOnly=true` and `safeForInstallation=false` are unconditional: a
successful capture does not establish completeness or authorize installation.
The export does not feed the normal title browser, collision check or installer.
Invalid/non-terminated text is bounded and flagged; non-ASCII/control bytes are
replaced for safe diagnostic JSON. Keep library inventories private. No new
screen, app entry, config change, title launch or write action is involved.

### Install An Existing SD Package From UIF

The development `GET /api/v1/install-packages` response includes `titleId`,
`tmdSha256` and `remoteInstallEligible` for each package, plus a response-level
`truncated` flag. Eligibility describes the package, not whether its title ID is
already installed. The native installer checks collisions again before use.

With a matching development desktop and receiver, select a package in UIF's
Wii U tab and choose **Install to USB**. Confirm the package, title ID and target.
Both global and automatic installation permissions must be enabled on-console;
manual installation alone does not authorize a remote install. No files are
uploaded again, and a queued acknowledgement is not an installed result.

The paired POST body has exactly these fields:

```json
{
  "requestId": "0123456789ABCDEF0123456789ABCDEF",
  "packageName": "Example package",
  "titleId": "00050002D1060001",
  "tmdSha256": "0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF",
  "storageTarget": "usb"
}
```

The receiver requires an idle state, a direct `sd:/install` child, a valid
TMD-bound USB policy, complete APP/H3 inventory and no existing matching title.
A complete system catalog proves catalog absence. For generated `00050002`
title IDs only, an incomplete catalog can instead use read-only exact title-folder
checks on healthy MLC and USB01 roots (plus USB02 when present). Only native
NOT_FOUND at the exact target path counts as absent; permission, media, root
changes and cleanup errors block installation. Positive catalog/SYS checks still
reject the title. The catalog remains explicitly incomplete, not silently accepted.
The full late collision check is retained; install setup is refreshed afterward,
then package identity and folders are checked once more before target selection.
Unknown-policy/NAND-only packages require the
existing on-console manual workflow. System titles, updates and DLC are not
accepted, and there is no NAND fallback. The package is rechecked immediately
before MCP submission; it is not deleted or launched afterward.

Paired idle-only `GET /api/v1/diagnostics/title-catalog/folders/<titleId>` exposes
the bounded metadata check for these generated IDs. It never enumerates title
folders or reads keys, saves or title file contents. Replies are diagnostic-only
and never authorize installation by themselves. Read-only named-device probes
also accept `device/slc` and `device/slccmpt`; they do not establish
catalog completeness.

Exclusive, flushed claims in `sd:/uinjectforge/assistant/install-requests`
prevent automatic replay of a request or the same TMD under a new request ID.
These survive restart but are not completed-result receipts. Terminal results
use the existing in-memory request-ID history; if the response/session is lost,
inspect the console before retrying. The desktop never automatically resends
this POST. Cancel only stops desktop monitoring, not an already running console
install. No claims are removed automatically. See the
[October 5 repair checkpoint](../../docs/1.0.6-assistant-repairs-20261005.md) for
tests, candidate identity and pending hardware checks.

Transfer manifests can mark an installable package as requiring Wii U system memory. The receiver then overrides automatic installation to NAND and locks that package to NAND on the manual Packages page for the current receiver session. Cafe2Wii is the first workflow that uses this constraint because the current Aroma c2w patcher only redirects titles launched from MLC.

Supported final SD destinations:

- installable WUP package: `sd:/install/<package>/`
- Loadiine package: `sd:/wiiu/games/<package>/`
- SD runtime package: relative to the SD root

For a development build, run these commands from this component folder in a
POSIX shell. The Dockerfile pins the SDK image by digest:

```sh
docker build -t uinjectforge-assistant-dev .
docker run --rm --network=none -w /src -v "${PWD}:/src" uinjectforge-assistant-dev make clean
docker run --rm --network=none -w /src -v "${PWD}:/src" uinjectforge-assistant-dev make -j2
```

The Docker image needs SDL2 and SDL_ttf Wii U portlibs available. If the image lacks those packages, build with a devkitPro install that has `wiiu-sdl2` and `wiiu-sdl2_ttf` installed.

Build with a local devkitPro/wut install:

```sh
make
```

These direct builds use the development defaults `1.0.0-alpha` / `dev` unless
explicitly overridden. They do not create the release provenance required by
the package script below.

Run the platform-independent security/configuration tests with any C11 compiler:

```sh
make host-test
```

Run the real C++ UI against host SDL with mocked Wii U input/storage APIs (from this folder):

```sh
docker build -f tests/Dockerfile.host -t uinjectforge-assistant-host-tests .
docker run --rm -v "$PWD:/app" -w /app uinjectforge-assistant-host-tests make host-test ui-host-test
```

These tests cover five-page navigation, Diagnostics tab focus/back and log-tail polling, explicit list focus/back, separate install confirmation, touch coordinate transforms, Wii Remote/Classic/Pro input, install navigation lock, restricted-folder catalog fallback, cache reuse, and rendered states across GamePad/TV/4:3 sizes. They write screenshots under `build-host/screenshots`. They do not prove console installation or HOME behavior.

Output files are written beside the Makefile:

- `uinjectforge-assistant.wuhb`
- `uinjectforge-assistant.rpx`

From the full UIF repository root, build an isolated, versioned candidate and
then package that exact source and its validated RPX/WUHB provenance:

```powershell
pwsh -NoProfile -File ./Scripts/Build-WiiUCompanion.ps1 -Component Assistant -Version 1.0.6 -OutputDirectory ./artifacts/assistant-1.0.6-native
pwsh -NoProfile -File ./Scripts/Build-WiiUAssistantPackage.ps1 -AssistantVersion 1.0.6 -AssistantSourceRoot ./artifacts/assistant-1.0.6-native -OutputRoot ./artifacts/assistant-1.0.6-package
```

The isolated native directory must be fresh and under this repository's
`artifacts` directory, with no linked path components. Choose a new directory
name if it already exists. These wrapper scripts belong to the full UIF
repository; a companion-only checkout uses its own standalone release workflow.

That package is intentionally shaped as `sd/wiiu/apps/uinjectforge-assistant/...`;
testers should copy the contents of the generated `sd` folder to the Wii U SD
card root. A development candidate is not a final release. Final shipping must
use the reviewed clean primary commit, completed native acceptance and a matching
standalone public source revision. Keep the complete `source/libmocha` source,
notices and rebuilding/relinking instructions available with that release, as
required by `THIRD-PARTY-NOTICES.md`.

Build hashes and source fingerprints are recorded in the package's RPX/WUHB
BUILD-INFO files and checksum list, not maintained separately in this README.
Rebuild before handing out binaries after any source change. Host tests cannot
prove HOME/Close Software, USB/NAND installation or failure recovery on a Wii U;
those remain mandatory acceptance checks for a stable companion release.
