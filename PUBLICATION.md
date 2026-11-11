# Standalone source

This Assistant 1.0.6 source offer corresponds to the reviewed UInjectForge
primary commit `04366fc389338700c6ad50eb86071207731cdfef` and its
`tools/UInjectForge.Assistant.WiiU` directory. `UPSTREAM-SOURCE.json` pins all 77
native fingerprint inputs by relative path, Git mode, size and SHA-256.
Verify the matching Assistant binary package and its build provenance before
publishing the pair.

The older September preview remains tied to standalone commit
`0aa98695437f2c9f5ffe7626a5a64e0addc0a5a1`. New builds must record their exact
standalone commit, component version and native source fingerprint without
relabelling that preview or the historical Fast2 test binaries.

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

The component now includes libmocha under LGPL-3.0. Keep its complete source,
original Makefile, LICENSE and UPSTREAM.md, the dependency notices and usable
rebuilding/relinking instructions available alongside each binary release.
Publish the matching standalone source tag/archive and retain its checksum.
