# UInjectForge Assistant

Wii U companion for UInjectForge on desktop and mobile.
Receive builds over your local network, inspect SD packages, and install
selected titles to available system memory or USB storage.

## Install

Download the ZIP from this repository's Releases, then copy the **contents**
of its `sd` folder to your SD card root. Launch the Assistant WUHB under Aroma.
Keep existing configuration when updating.

In UIF, choose Find Wii U or enter the displayed IP address and port,
then enter the pairing code shown on the console. Default port: 8573.
Allow local network access through your firewall; do not expose this port
to the Internet.

Manual installation requires on-console confirmation. Automatic installation
is off by default and must be explicitly enabled in the receiver configuration.
Do not remove storage or power off the console while installing.

## Status

Preview software. New releases need hardware checks for transfers,
installation, controller interaction, and HOME/Close Software. Host tests
cannot prove those behaviors.

- [Build and package](BUILDING.md)
- [Source provenance and release checklist](PUBLICATION.md)
- [Protocol and development reference](REFERENCE.md)
- [License](LICENSE) and [third-party notices](THIRD-PARTY-NOTICES.md)

The development reference is retained from the parent project; its old build
hashes and parent-repository packaging command are historical, not standalone
release instructions. Use BUILDING.md instead.
