# Building the Assistant

## Release Package

From a clean committed checkout, using PowerShell 7.4+ and Docker:

```powershell
./Scripts/Build-Release.ps1 -Version 1.0.6
```

This builds an isolated archive of HEAD, runs host/security/UI tests, and
cross-compiles with networking disabled. It produces an SD-overlay ZIP, a
SHA-256 file, and source/toolchain provenance under a new `artifacts` folder.
The command version must match the reviewed mapping's component version.
The matching `UInjectForge-Assistant-<version>-Source.zip` is produced from the
same Git commit; `SHA256SUMS.txt` covers both the binary and source ZIPs. Keep both
archives available with the release. The source includes libmocha and its
rebuilding/relinking materials. The build validates `UPSTREAM-SOURCE.json` before
and after native compilation and embeds the native `src-...` fingerprint ID;
provenance also records the standalone Git commit and upstream source identity.

It does not publish automatically. GitHub Actions is manual-only to avoid
unrequested hosted build charges. Publishing must include the exact source tag
and the package's checksum file; retain prerelease status until hardware checks
for that package pass.

## Metadata and package checks

These checks use small authored fixtures and stub Docker for the package test;
they do not compile or exercise Wii U software. Run from the physical checkout
path, using a fresh fixture directory for each command:

```powershell
$checks = Join-Path $PWD 'artifacts'
$null = New-Item -ItemType Directory -Force $checks
./Scripts/Test-SourceMapping.ps1 -TempRoot (Join-Path $checks ("source-mapping-" + [Guid]::NewGuid().ToString('N')))
./Scripts/Test-ReleasePreparation.ps1 -TempRoot (Join-Path $checks ("release-preparation-" + [Guid]::NewGuid().ToString('N')))
```

## Individual Development Builds

From this repository root, with Docker installed:

```powershell
docker build -t uif-assistant-source .
$revision = git rev-parse --short=12 HEAD
docker run --rm --network none -v "${PWD}:/app" -w /app uif-assistant-source make -j2 UIF_COMPONENT_BUILD_ID=$revision
```

Outputs: `uinjectforge-assistant.rpx` and `uinjectforge-assistant.wuhb`.
Use a fresh checkout for release builds; otherwise run `make clean` inside
the same container before compiling. Never run clean in your only copy of a
retained working binary.

Host tests:

```powershell
docker build -f tests/Dockerfile.host -t uif-assistant-tests .
docker run --rm --network none -v "${PWD}:/app" -w /app uif-assistant-tests make host-test ui-host-test
```

Place the RPX/WUHB, `assets/uinjectforge-icon.png` renamed to `icon.png`,
and the files from `packaging/` in
`sd/wiiu/apps/uinjectforge-assistant/`. Keep `config.example.json` as an example:
do not overwrite a user's `config.json`.
Distribute LICENSE, THIRD-PARTY-NOTICES.md, licenses/, and README.md with the ZIP.
Include SHA-256 checksums, source commit, Docker image ID/digest and build date.
Build artifacts belong in GitHub Releases, not in source commits.
