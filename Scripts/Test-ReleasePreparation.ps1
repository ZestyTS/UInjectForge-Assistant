#requires -Version 7.4
# Synthetic packaging integration only: no Docker, SDK, native tests or hardware.
[CmdletBinding()]
param([Parameter(Mandatory)][string]$TempRoot)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$git = (Get-Command git -CommandType Application -ErrorAction Stop).Source
$oldDocker = Get-Item Function:global:docker -ErrorAction SilentlyContinue
$savedEnvironment = @{}
foreach ($name in @('PATH', 'GIT_CONFIG_NOSYSTEM', 'GIT_CONFIG_GLOBAL', 'GIT_TERMINAL_PROMPT')) {
    $savedEnvironment[$name] = [Environment]::GetEnvironmentVariable($name)
}
if (![IO.Path]::IsPathRooted($TempRoot)) { throw 'Use a fresh absolute -TempRoot for the synthetic fixture.' }
$fixture = [IO.Path]::GetFullPath($TempRoot).TrimEnd([char[]]@('\', '/'))
if (Test-Path -LiteralPath $fixture) { throw 'Synthetic fixture TempRoot must not already exist.' }
$parent = [IO.Path]::GetDirectoryName($fixture)
if ([string]::IsNullOrWhiteSpace($parent)) { throw 'Synthetic fixture requires an ordinary existing parent.' }
for ($cursor = $parent; $cursor; $cursor = [IO.Path]::GetDirectoryName($cursor)) {
    if (([IO.File]::GetAttributes($cursor) -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw 'Synthetic fixture parents must not be symbolic links or reparse points.'
    }
}
$ownsFixture = $false
$version = '1.0.6'
$upstreamCommit = '1111111111111111111111111111111111111111'
$state = @{ Calls = [Collections.Generic.List[object]]::new(); Root = $fixture; Version = $version; BuildId = '' }

function Assert-Preparation([bool]$Condition, [string]$Message) {
    if (!$Condition) { throw "Synthetic packaging fixture: $Message" }
}
function Write-FixtureText([string]$Relative, [string]$Text) {
    $path = Join-Path $fixture $Relative
    [void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($path))
    [IO.File]::WriteAllText($path, $Text, [Text.UTF8Encoding]::new($false))
}
function Get-BodyHash([byte[]]$Body) {
    [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($Body)).ToLowerInvariant()
}
function Read-ZipBody($Archive, [string]$Name) {
    $entry = $Archive.GetEntry($Name)
    Assert-Preparation ($null -ne $entry) "missing ZIP member $Name"
    $stream = $entry.Open()
    $memory = [IO.MemoryStream]::new()
    try { $stream.CopyTo($memory); return ,$memory.ToArray() }
    finally { $memory.Dispose(); $stream.Dispose() }
}
function Invoke-FixtureGit([string[]]$Arguments) {
    $value = & $git -C $fixture @Arguments
    if ($LASTEXITCODE -ne 0) { throw 'Synthetic fixture Git command failed.' }
    return $value
}
function Assert-RejectedBeforeDocker([scriptblock]$Action, [string]$ExpectedMessage) {
    $before = $state.Calls.Count
    $failure = $null
    try { & $Action } catch { $failure = $_ }
    Assert-Preparation ($null -ne $failure) 'negative case unexpectedly succeeded'
    Assert-Preparation ($failure.Exception.Message -like $ExpectedMessage) 'negative case failed for an unexpected reason'
    Assert-Preparation ($state.Calls.Count -eq $before) 'negative case reached the Docker stub'
}

try {
    [void][IO.Directory]::CreateDirectory($fixture)
    $ownsFixture = $true
    # Restrict native command lookup to Git's directory and reject an executable
    # Docker there. Only the in-process function below can resolve as docker.
    $env:PATH = [IO.Path]::GetDirectoryName($git)
    Assert-Preparation (-not (Get-Command docker -CommandType Application -ErrorAction SilentlyContinue)) 'a real Docker executable is on the fixture PATH'
    $env:GIT_CONFIG_NOSYSTEM = '1'
    $env:GIT_CONFIG_GLOBAL = Join-Path $fixture 'absent-global-git-config'
    $env:GIT_TERMINAL_PROMPT = '0'
    [void][IO.Directory]::CreateDirectory((Join-Path $fixture 'Scripts'))
    foreach ($name in @('Build-Release.ps1', 'NativeSource.ps1')) {
        Copy-Item -LiteralPath (Join-Path $PSScriptRoot $name) -Destination (Join-Path $fixture "Scripts/$name")
    }
    Write-FixtureText '.gitignore' "artifacts/`n"
    Write-FixtureText 'Makefile' "# SYNTHETIC: never executed`n"
    Write-FixtureText 'Dockerfile' "# SYNTHETIC: never built`nFROM scratch`n"
    Write-FixtureText 'tests/Dockerfile.host' "# SYNTHETIC: never built`nFROM scratch`n"
    Write-FixtureText 'source/main.c' "/* SYNTHETIC source, not native acceptance. */`n"
    Write-FixtureText 'include/fixture.h' "/* SYNTHETIC header. */`n"
    Write-FixtureText 'assets/uinjectforge-icon.png' 'SYNTHETIC icon bytes; not a decoded image'
    Write-FixtureText 'packaging/meta.xml' '<app version="1"><name>UInjectForge Assistant</name><coder>ZestyTS</coder><version>fixture</version></app>'
    Write-FixtureText 'packaging/config.example.json' '{"autoInstallEnabled":false}'
    foreach ($name in @('LICENSE', 'README.md', 'BUILDING.md', 'PUBLICATION.md', 'REFERENCE.md', 'RELEASE-NOTES.md', 'THIRD-PARTY-NOTICES.md', 'licenses/fixture.txt')) {
        Write-FixtureText $name "SYNTHETIC packaging fixture: $name`n"
    }
    $nativeNames = @('Dockerfile', 'Makefile', 'assets/uinjectforge-icon.png', 'include/fixture.h', 'source/main.c')
    $rows = @($nativeNames | ForEach-Object {
        $body = [IO.File]::ReadAllBytes((Join-Path $fixture $_))
        [ordered]@{ path = $_; gitMode = '100644'; bytes = $body.Length; sha256 = Get-BodyHash $body }
    })
    $payload = [Text.Encoding]::UTF8.GetBytes((@($rows | ForEach-Object { "$($_.path)`t$($_.sha256)" } | Sort-Object) -join "`n"))
    $fingerprint = Get-BodyHash $payload
    $state.BuildId = 'src-' + $fingerprint.Substring(0, 12)
    $mapping = [ordered]@{
        schemaVersion = 1; upstreamRepository = 'Synthetic/Fixture'; upstreamCommit = $upstreamCommit
        upstreamComponent = 'synthetic-fixture'; componentVersion = $version
        nativeFingerprintInputCount = $rows.Count; nativeFingerprintInputs = $rows
    }
    Write-FixtureText 'UPSTREAM-SOURCE.json' ($mapping | ConvertTo-Json -Depth 6)
    & $git -c init.defaultBranch=fixture init --quiet $fixture
    Assert-Preparation ($LASTEXITCODE -eq 0) 'Git initialization failed'
    Invoke-FixtureGit @('config', 'user.name', 'Synthetic Fixture') | Out-Null
    Invoke-FixtureGit @('config', 'user.email', 'fixture@test.invalid') | Out-Null
    Invoke-FixtureGit @('config', 'core.hooksPath', (Join-Path $fixture 'absent-hooks')) | Out-Null
    Invoke-FixtureGit @('add', '--all') | Out-Null
    Invoke-FixtureGit @('commit', '--quiet', '-m', 'Synthetic release preparation fixture') | Out-Null
    $commit = (Invoke-FixtureGit @('rev-parse', 'HEAD')).Trim()
    $tracked = @(Invoke-FixtureGit @('ls-files'))
    $before = @{}
    foreach ($name in $tracked) { $before[$name] = Get-BodyHash ([IO.File]::ReadAllBytes((Join-Path $fixture $name))) }

    function global:docker {
        $values = @($args | ForEach-Object { [string]$_ })
        $state.Calls.Add($values)
        $global:LASTEXITCODE = 0
        switch ($values[0]) {
            'build' {
                Assert-Preparation ($values[-1].StartsWith((Join-Path $state.Root 'artifacts') + [IO.Path]::DirectorySeparatorChar, [StringComparison]::Ordinal)) 'Docker build escaped the authored fixture'
                return
            }
            'image' {
                Assert-Preparation (($values[1..3] -join ' ') -ceq 'inspect --format {{.Id}}') 'unexpected Docker image invocation'
                if ($values[-1] -ceq 'uinjectforge-assistant-release-builder') { return 'sha256:' + ('a' * 64) }
                Assert-Preparation ($values[-1] -ceq 'uinjectforge-assistant-release-tests') 'unexpected synthetic image name'
                return 'sha256:' + ('b' * 64)
            }
            'run' {
                $mountIndex = [array]::IndexOf($values, '-v')
                $workIndex = [array]::IndexOf($values, '-w')
                $networkIndex = [array]::IndexOf($values, '--network')
                Assert-Preparation ($mountIndex -ge 0 -and $workIndex -ge 0 -and $networkIndex -ge 0) 'missing container arguments'
                Assert-Preparation ($values[$networkIndex + 1] -ceq 'none' -and $values[$workIndex + 1] -ceq '/src') 'container network/path contract differs'
                $mount = $values[$mountIndex + 1]
                Assert-Preparation ($mount.EndsWith(':/src', [StringComparison]::Ordinal)) 'unexpected source mount'
                $source = $mount.Substring(0, $mount.Length - 5)
                Assert-Preparation ($source.StartsWith((Join-Path $state.Root 'artifacts') + [IO.Path]::DirectorySeparatorChar, [StringComparison]::Ordinal)) 'source mount escaped the fixture'
                if ($values[-1] -ceq 'ui-host-test') {
                    Assert-Preparation (($values[-3..-1] -join ' ') -ceq 'make host-test ui-host-test') 'unexpected synthetic host-test command'
                    return
                }
                Assert-Preparation (($values[-4..-1] -join ' ') -ceq "make -j2 UIF_COMPONENT_VERSION=$($state.Version) UIF_COMPONENT_BUILD_ID=$($state.BuildId)") 'compiled version/build identity differs'
                # Deliberately fake header-only binaries. These cannot establish
                # native compilation, host-test results, launch or hardware use.
                $rpx = [byte[]]::new(64)
                ([byte[]]@(0x7f, 0x45, 0x4c, 0x46)).CopyTo($rpx, 0)
                $rpx[4] = 1; $rpx[5] = 2; $rpx[19] = 20
                $wuhb = [byte[]]::new(64)
                [Text.Encoding]::ASCII.GetBytes('WUHB').CopyTo($wuhb, 0)
                [IO.File]::WriteAllBytes((Join-Path $source 'uinjectforge-assistant.rpx'), $rpx)
                [IO.File]::WriteAllBytes((Join-Path $source 'uinjectforge-assistant.wuhb'), $wuhb)
                [IO.File]::WriteAllText((Join-Path $source 'uinjectforge-assistant.elf'), "SYNTHETIC ONLY $($state.Version) $($state.BuildId)")
                return
            }
            default { throw 'Unexpected Docker stub invocation; no native fallback is allowed.' }
        }
    }
    Assert-Preparation ((Get-Command docker).CommandType -eq [Management.Automation.CommandTypes]::Function) 'Docker stub is shadowed by another command'
    Write-Host 'SYNTHETIC packaging fixture: Docker and every native/test action are stubbed.'
    & (Join-Path $fixture 'Scripts/Build-Release.ps1') -Version $version
    Assert-Preparation ($state.Calls.Count -eq 6) 'expected two image builds, two inspections and two synthetic runs'
    $outputs = @(Get-ChildItem -LiteralPath (Join-Path $fixture 'artifacts') -Directory)
    Assert-Preparation ($outputs.Count -eq 1) 'expected one positive output directory'
    $output = $outputs[0].FullName
    $binaryName = "UInjectForge-Assistant-$version-WiiU.zip"
    $sourceName = "UInjectForge-Assistant-$version-Source.zip"
    $binaryPath = Join-Path $output $binaryName
    $sourcePath = Join-Path $output $sourceName
    $sums = [IO.File]::ReadAllLines((Join-Path $output 'SHA256SUMS.txt'))
    Assert-Preparation ($sums.Count -eq 2) 'publication checksum file must contain exactly two lines'
    foreach ($name in @($binaryName, $sourceName)) {
        $hash = Get-BodyHash ([IO.File]::ReadAllBytes((Join-Path $output $name)))
        Assert-Preparation (@($sums | Where-Object { $_ -imatch "^$hash  $([regex]::Escape($name))$" }).Count -eq 1) "publication hash/name differs for $name"
    }
    $binary = [IO.Compression.ZipFile]::OpenRead($binaryPath)
    $source = [IO.Compression.ZipFile]::OpenRead($sourcePath)
    try {
        $info = [Text.Encoding]::UTF8.GetString((Read-ZipBody $binary 'BUILD-INFO.json')) | ConvertFrom-Json
        Assert-Preparation ($info.component -ceq 'UInjectForge Assistant' -and $info.sourceUrl -ceq "https://github.com/ZestyTS/UInjectForge-Assistant/tree/$commit") 'public component name/source URL differs'
        Assert-Preparation ($info.version -ceq $version -and $info.sourceCommit -ceq $commit -and $info.buildId -ceq $state.BuildId) 'public standalone identity differs'
        Assert-Preparation ($info.nativeSourceFingerprint -ceq $fingerprint -and $info.upstreamSourceCommit -ceq $upstreamCommit) 'public source provenance differs'
        Assert-Preparation ($info.sourceArchive -ceq $sourceName -and $info.sourceArchiveSha256.ToLowerInvariant() -ceq (Get-BodyHash ([IO.File]::ReadAllBytes($sourcePath)))) 'public source archive descriptor differs'
        $app = 'sd/wiiu/apps/uinjectforge-assistant/'
        foreach ($name in @('uinjectforge-assistant.rpx', 'uinjectforge-assistant.wuhb', 'icon.png', 'meta.xml', 'config.example.json')) { [void](Read-ZipBody $binary ($app + $name)) }
        Assert-Preparation ((Get-BodyHash (Read-ZipBody $binary ($app + 'icon.png'))) -ceq $before['assets/uinjectforge-icon.png']) 'packaged icon bytes differ'
        Assert-Preparation ((Get-BodyHash (Read-ZipBody $binary ($app + 'config.example.json'))) -ceq $before['packaging/config.example.json']) 'packaged example configuration differs'
        foreach ($name in @('LICENSE', 'README.md', 'BUILDING.md', 'PUBLICATION.md', 'REFERENCE.md', 'RELEASE-NOTES.md', 'THIRD-PARTY-NOTICES.md', 'licenses/fixture.txt')) {
            Assert-Preparation ((Get-BodyHash (Read-ZipBody $binary $name)) -ceq $before[$name]) "packaged notice/document differs for $name"
        }
        Assert-Preparation (@($binary.Entries | Where-Object { [IO.Path]::GetFileName($_.FullName) -ceq 'config.json' }).Count -eq 0) 'package contains an owner config.json'
        [xml]$meta = [Text.Encoding]::UTF8.GetString((Read-ZipBody $binary ($app + 'meta.xml')))
        Assert-Preparation ($meta.app.SelectSingleNode('version').InnerText -ceq $version -and $meta.app.name -ceq 'UInjectForge Assistant' -and $meta.app.coder -ceq 'ZestyTS') 'launcher metadata differs'
        Assert-Preparation (@($info.outputs).Count -eq 2 -and (@($info.outputs.name | Sort-Object) -join ',') -ceq 'uinjectforge-assistant.rpx,uinjectforge-assistant.wuhb') 'expected exactly the two public output descriptors'
        foreach ($row in $info.outputs) {
            Assert-Preparation ($row.name -cin @('uinjectforge-assistant.rpx', 'uinjectforge-assistant.wuhb')) 'unexpected public output name'
            $body = Read-ZipBody $binary ($app + $row.name)
            Assert-Preparation ($body.Length -eq 64 -and $row.sha256.ToLowerInvariant() -ceq (Get-BodyHash $body)) 'synthetic binary readback differs'
        }
        $internalSums = [Text.Encoding]::UTF8.GetString((Read-ZipBody $binary 'CHECKSUMS-SHA256.txt')).TrimEnd("`r", "`n") -split '\r?\n'
        Assert-Preparation ($binary.Entries.Count -eq $internalSums.Count + 1) 'binary archive membership differs from checksums'
        foreach ($line in $internalSums) {
            Assert-Preparation ($line -match '^([0-9A-Fa-f]{64})  (.+)$') 'malformed internal checksum line'
            $hash = $Matches[1].ToLowerInvariant(); $name = $Matches[2]
            Assert-Preparation ((Get-BodyHash (Read-ZipBody $binary $name)) -ceq $hash) 'internal ZIP checksum differs'
        }
        $sourceFiles = @($source.Entries | Where-Object { !$_.FullName.EndsWith('/') })
        Assert-Preparation ($sourceFiles.Count -eq $tracked.Count) 'source archive file membership differs from committed fixture'
        foreach ($name in $tracked) {
            Assert-Preparation ((Get-BodyHash (Read-ZipBody $source $name)) -ceq $before[$name]) "source archive differs for $name"
            Assert-Preparation ((Get-BodyHash ([IO.File]::ReadAllBytes((Join-Path $fixture $name)))) -ceq $before[$name]) "actual source changed for $name"
        }
    } finally { $source.Dispose(); $binary.Dispose() }
    Assert-Preparation (@(Invoke-FixtureGit @('status', '--porcelain')).Count -eq 0) 'positive build dirtied committed source'
    Assert-RejectedBeforeDocker { & (Join-Path $fixture 'Scripts/Build-Release.ps1') -Version '1.0.7' } '*Requested component version differs from the reviewed source mapping.*'
    Write-FixtureText 'README.md' 'SYNTHETIC dirty source'
    Assert-RejectedBeforeDocker { & (Join-Path $fixture 'Scripts/Build-Release.ps1') -Version $version } '*checkout must be clean.*'
    $summary = [ordered]@{ passed = $true; scope = 'Synthetic Git/archive/package integration only'; dockerStubCalls = $state.Calls.Count; negativeCases = @('version mismatch before Docker', 'dirty checkout before Docker'); realDockerExecuted = $false; nativeOrHostTestsExecuted = $false; hardwareAcceptance = $false; fixturesRemoved = $false }
} finally {
    Remove-Item Function:global:docker -ErrorAction SilentlyContinue
    if ($null -ne $oldDocker) { Set-Item Function:global:docker -Value $oldDocker.ScriptBlock }
    foreach ($name in $savedEnvironment.Keys) { [Environment]::SetEnvironmentVariable($name, $savedEnvironment[$name]) }
    if ($ownsFixture -and [IO.Directory]::Exists($fixture)) { Remove-Item -LiteralPath $fixture -Recurse -Force }
}
$summary.fixturesRemoved = ![IO.Directory]::Exists($fixture)
$summary | ConvertTo-Json -Depth 4
