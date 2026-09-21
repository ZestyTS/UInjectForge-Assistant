# Assistant 1.0.0-preview.20260921

This replaces the August 30 public package. UIF and Assistant are separate
downloads; update the Assistant on the Wii U SD card as well as UIF.

## Changes

- Fix SD transfer staging at `/vol/external01/...`: directory creation now skips
  virtual mount roots. Startup checks staging access before advertising readiness.
- Keep the receiver responsive during startup and expose startup progress.
- Decode escaped/Unicode transfer names correctly and constrain GameCube SD
  configuration destinations to supported paths.
- Retain transfer results by job ID and serialize installation ownership so a
  later upload cannot overwrite the status of an earlier installation.
- Validate persistent, TMD-bound install policy records; preserve required NAND
  destinations and block malformed records rather than treating them as USB-safe.
- Make the Packages & tools dashboard and confirmed automatic-install setting
  accessible from the compact receiver interface.

## Updating

Copy the contents of `sd` in the release ZIP to your SD card root. Replace both
`uinjectforge-assistant.wuhb` and `uinjectforge-assistant.rpx` under
`wiiu/apps/uinjectforge-assistant`. Keep your existing `config.json`; the ZIP
ships only `config.example.json`. Fully close and relaunch Assistant before
checking its version/build ID, pairing, and running UIF's Send Smoke Test.

## Validation Limits

Release packaging requires native host/security/UI tests and a fresh Wii U
cross-build. These do not prove console launch, transfer, install, or HOME exit.
This remains a prerelease pending those checks on this exact package. Automatic
installation remains off by default; use Copy only for the initial transfer test.
