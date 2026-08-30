# Standalone source

Imported from UInjectForge commit `629c424039246f5346259a4b448bb10236883c3a`,
directory `tools/UInjectForge.Assistant.WiiU`. Runtime source is unchanged.
The previous preview binaries identify their original UIF build, not a commit
in this new repository. New builds must record this repository's commit.

Original Assistant code is MIT licensed; see LICENSE. Libraries retain their
own notices in THIRD-PARTY-NOTICES.md and licenses/.

## Release checklist

1. Run the host tests and compile the Wii U build using BUILDING.md.
2. Record the source commit and toolchain image digest with the release.
3. Test launch, pairing, transfer, install, and HOME/Close Software on Wii U.
4. Package the binaries, icon, metadata, README, LICENSE, and third-party notices.
5. Publish a prerelease until current hardware checks pass. Do not describe
   a successful compilation as proof of installation or shutdown behavior.

Source changes here do not automatically update the copy embedded in the UIF
development repository. Synchronize reviewed changes explicitly until that
integration is migrated to a pinned component release.
