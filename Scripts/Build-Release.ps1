#requires -Version 7.4
[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [ValidatePattern('^[0-9A-Za-z][0-9A-Za-z._+-]{0,31}$')]
    [string]$Version
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
function Invoke-Checked([string]$Program, [string[]]$Arguments) {
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Program failed with exit code $LASTEXITCODE." }
}

Push-Location $root
try {
    $status = @(Invoke-Checked git @('status', '--porcelain'))
    if ($status.Count) { throw 'Commit reviewed changes before building a release; the checkout must be clean.' }
    $commit = (Invoke-Checked git @('rev-parse', 'HEAD')).Trim()
    $buildId = $commit.Substring(0, 12)
    $output = Join-Path $root ("artifacts/$Version-" + [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss') + '-' + [Guid]::NewGuid().ToString('N').Substring(0, 8))
    $null = New-Item -ItemType Directory -Path $output
    $sourceArchive = Join-Path $output 'source.zip'
    Invoke-Checked git @('archive', '--format=zip', "--output=$sourceArchive", $commit)
    $source = Join-Path $output 'source'
    [IO.Compression.ZipFile]::ExtractToDirectory($sourceArchive, $source)

    $builder = 'uinjectforge-assistant-release-builder'
    $tester = 'uinjectforge-assistant-release-tests'
    Invoke-Checked docker @('build', '-t', $builder, $source)
    Invoke-Checked docker @('build', '-t', $tester, '-f', (Join-Path $source 'tests/Dockerfile.host'), $source)
    $builderId = (Invoke-Checked docker @('image', 'inspect', '--format', '{{.Id}}', $builder)).Trim()
    $testerId = (Invoke-Checked docker @('image', 'inspect', '--format', '{{.Id}}', $tester)).Trim()
    $mount = "${source}:/app"
    Invoke-Checked docker @('run', '--rm', '--network', 'none', '-v', $mount, '-w', '/app', $testerId, 'make', 'host-test', 'ui-host-test')
    Invoke-Checked docker @('run', '--rm', '--network', 'none', '-v', $mount, '-w', '/app', $builderId, 'make', '-j2', "UIF_COMPONENT_VERSION=$Version", "UIF_COMPONENT_BUILD_ID=$buildId")

    $stem = 'uinjectforge-assistant'
    $rpx = Join-Path $source "$stem.rpx"
    $wuhb = Join-Path $source "$stem.wuhb"
    $rpxBytes = [IO.File]::ReadAllBytes($rpx)
    $wuhbBytes = [IO.File]::ReadAllBytes($wuhb)
    if ($rpxBytes.Length -lt 64 -or [Convert]::ToHexString([byte[]]$rpxBytes[0..3]) -ne '7F454C46' -or $rpxBytes[5] -ne 2 -or $rpxBytes[19] -ne 20) {
        throw 'RPX does not identify as a big-endian PowerPC ELF.'
    }
    if ($wuhbBytes.Length -lt 64 -or [Text.Encoding]::ASCII.GetString($wuhbBytes, 0, 4) -cne 'WUHB') {
        throw 'Invalid WUHB bundle header.'
    }
    $elfText = [Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes((Join-Path $source "$stem.elf")))
    if (!$elfText.Contains($Version) -or !$elfText.Contains($buildId)) { throw 'Compiled version/build identity is missing.' }

    $package = Join-Path $output 'package'
    $app = Join-Path $package 'sd/wiiu/apps/uinjectforge-assistant'
    $null = New-Item -ItemType Directory -Path $app -Force
    Copy-Item -LiteralPath $rpx, $wuhb -Destination $app
    Copy-Item -LiteralPath (Join-Path $source 'assets/uinjectforge-icon.png') -Destination (Join-Path $app 'icon.png')
    Copy-Item -LiteralPath (Join-Path $source 'packaging/config.example.json') -Destination $app
    [xml]$metadata = [IO.File]::ReadAllText((Join-Path $source 'packaging/meta.xml'))
    $metadata.app.SelectSingleNode('version').InnerText = $Version
    $date = $metadata.CreateElement('release_date')
    $date.InnerText = [DateTime]::UtcNow.ToString('yyyyMMddHHmmss')
    $null = $metadata.app.AppendChild($date)
    $metadata.Save((Join-Path $app 'meta.xml'))
    foreach ($name in @('LICENSE', 'README.md', 'BUILDING.md', 'PUBLICATION.md', 'REFERENCE.md', 'RELEASE-NOTES.md', 'THIRD-PARTY-NOTICES.md', 'licenses')) {
        Copy-Item -LiteralPath (Join-Path $source $name) -Destination $package -Recurse
    }
    $provenance = [ordered]@{
        component = 'UInjectForge Assistant'; version = $Version; buildId = $buildId
        sourceCommit = $commit; sourceUrl = "https://github.com/ZestyTS/UInjectForge-Assistant/tree/$commit"
        capturedUtc = [DateTimeOffset]::UtcNow.ToString('O')
        sourceArchiveSha256 = (Get-FileHash -LiteralPath $sourceArchive -Algorithm SHA256).Hash
        dockerfile = [IO.File]::ReadAllText((Join-Path $source 'Dockerfile'))
        builderImage = $builderId; hostTestImage = $testerId
        buildCommand = "make -j2 UIF_COMPONENT_VERSION=$Version UIF_COMPONENT_BUILD_ID=$buildId"
        checks = @('make host-test', 'make ui-host-test', 'RPX/WUHB headers', 'embedded version/build ID', 'package SHA-256 verification')
        hardwareAcceptance = 'Pending for this exact package'
        outputs = @(@{ name = "$stem.rpx"; sha256 = (Get-FileHash -LiteralPath $rpx).Hash }, @{ name = "$stem.wuhb"; sha256 = (Get-FileHash -LiteralPath $wuhb).Hash })
    }
    [IO.File]::WriteAllText((Join-Path $package 'BUILD-INFO.json'), ($provenance | ConvertTo-Json -Depth 6))
    $sums = foreach ($file in Get-ChildItem -LiteralPath $package -Recurse -File | Sort-Object FullName) {
        $relative = [IO.Path]::GetRelativePath($package, $file.FullName).Replace('\', '/')
        if ($file.Name -eq 'config.json' -or $file.Extension -in @('.elf','.map','.c','.cpp','.h','.pdb','.key')) { throw "Unexpected release file: $relative" }
        "$((Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash)  $relative"
    }
    [IO.File]::WriteAllLines((Join-Path $package 'CHECKSUMS-SHA256.txt'), [string[]]$sums)
    $zipName = "UInjectForge-Assistant-$Version-WiiU.zip"
    $zip = Join-Path $output $zipName
    [IO.Compression.ZipFile]::CreateFromDirectory($package, $zip, [IO.Compression.CompressionLevel]::Optimal, $false)
    $archive = [IO.Compression.ZipFile]::OpenRead($zip)
    try {
        if ($archive.Entries.Count -ne $sums.Count + 1) { throw 'Unexpected ZIP contents.' }
        foreach ($line in $sums) {
            $hash, $relative = $line -split '  ', 2
            $entry = $archive.GetEntry($relative)
            if (!$entry) { throw "Missing ZIP entry: $relative" }
            $stream = $entry.Open()
            try { $actual = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($stream)) } finally { $stream.Dispose() }
            if ($actual -cne $hash) { throw "ZIP checksum mismatch: $relative" }
        }
    } finally { $archive.Dispose() }
    $zipHash = (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash
    [IO.File]::WriteAllText((Join-Path $output 'SHA256SUMS.txt'), "$zipHash  $zipName`n")
    Write-Host "Verified package: $zip"
    Write-Host "Source: $commit; build ID: $buildId; SHA-256: $zipHash"
    Write-Host 'No upload performed. Exact-package Wii U hardware acceptance remains pending.'
} finally { Pop-Location }
