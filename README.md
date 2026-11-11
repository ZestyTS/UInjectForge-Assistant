# UInjectForge Assistant

Wii U companion for UInjectForge on desktop and mobile.
Receive builds over your local network, inspect SD packages, and install
selected titles to available system memory or USB storage.

## 1.0.6 source preparation

This branch prepares the newer Assistant used with UIF 1.0.6. It adds negotiated
FTP for bulk data while HTTP handles pairing, control, verification and install
requests. Older receivers retain the HTTP path. The published September preview
remains a separate download until the new package completes release acceptance.

Source, tests and dependency notices were synchronized from the reviewed UIF
component; see [source mapping](UPSTREAM-SOURCE.json). The standalone release
workflow is preserved. Final source/build identity and exact-package hardware
checks are still required. No universal speed or completed overlap claim is made.

## Install

Download the ZIP from this repository's Releases, then copy the **contents**
of its `sd` folder to your SD card root. Launch the Assistant WUHB under Aroma.
Keep existing configuration when updating.

The September 21 package replaces the August 30 download, including the SD
staging-path fix. If UIF reports `Could not create transfer staging folder`
with `/vol/external01/...` and `errno 2`, replace both the RPX and WUHB from the
new ZIP, fully close Assistant, then relaunch it. Updating UIF alone does not
update the Assistant files on the Wii U SD card.

In UIF, choose Find Wii U or enter the displayed IP address and port,
then enter the pairing code shown on the console. Default port: 8573.
Allow local network access through your firewall; do not expose this port
to the Internet.

Manual installation requires on-console confirmation. Automatic installation
is off by default; press X or tap Auto install in Assistant, then confirm to
enable it. UIF still requires Copy and install for each transfer.
Do not remove storage or power off the console while installing.

Tap **Packages & tools** or press Y (Wii Remote 2) to open manual installation,
title storage, diagnostics and settings. **Y Receiver** returns to the compact
screen. No config-file editing is required, and opening the dashboard never
authorizes an install. The optional Aroma Bridge is for background discovery;
Assistant does not require it.

## Status

Preview software. New releases need hardware checks for transfers,
installation, controller interaction, and HOME/Close Software. Host tests
cannot prove those behaviors.

- [Build and package](BUILDING.md)
- [Source provenance and release checklist](PUBLICATION.md)
- [Protocol and development reference](REFERENCE.md)
- [License](LICENSE) and [third-party notices](THIRD-PARTY-NOTICES.md)

The development reference also describes the full UIF checkout. Use BUILDING.md
for this standalone repository's release workflow. [Pairing details](PAIRING.md).
