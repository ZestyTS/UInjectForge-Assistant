#requires -Version 7.4
[CmdletBinding()]
param(
    [string]$ProjectRoot = (Split-Path -Parent $PSScriptRoot),
    [Parameter(Mandatory)][string]$TempRoot
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot 'NativeSource.ps1')
$script:activeCase = 'setup'
$script:passed = 0
$script:skipped = 0
$created = $false

function New-AuthoredFixture([string]$Name) {
    $directory = Join-Path $TempRoot $Name
    $null = [IO.Directory]::CreateDirectory((Join-Path $directory 'source'))
    [IO.File]::WriteAllBytes((Join-Path $directory 'Makefile'), [Text.Encoding]::UTF8.GetBytes("all:`n`t@true`n"))
    [IO.File]::WriteAllBytes((Join-Path $directory 'source/main.c'), [Text.Encoding]::UTF8.GetBytes("int fixture(void) { return 1; }`n"))
    $map = [ordered]@{
        schemaVersion = 1
        upstreamRepository = 'ZestyTS/UInjectForge'
        upstreamCommit = ('a' * 40)
        upstreamComponent = 'tools/UInjectForge.Assistant.WiiU'
        componentVersion = '1.0.6'
        nativeFingerprintInputCount = 2
        nativeFingerprintInputs = @(
            [ordered]@{ path = 'Makefile'; gitMode = '100644'; bytes = 12; sha256 = '8e9d2d32d6d1845adc673c5ca7a7a5846c135698c20f967648ee4cdadbb6d297' },
            [ordered]@{ path = 'source/main.c'; gitMode = '100644'; bytes = 32; sha256 = '957d0a5425293f2d7dd4ef64db4e8f7769380dc53f8ae93c4eb1d9b3431bd4c8' }
        )
    }
    [IO.File]::WriteAllText((Join-Path $directory 'UPSTREAM-SOURCE.json'), ($map | ConvertTo-Json -Depth 8))
    return $directory
}

function Edit-AuthoredMap([string]$Directory, [scriptblock]$Change) {
    $path = Join-Path $Directory 'UPSTREAM-SOURCE.json'
    $map = [IO.File]::ReadAllText($path) | ConvertFrom-Json -AsHashtable
    & $Change $map
    [IO.File]::WriteAllText($path, ($map | ConvertTo-Json -Depth 8))
}

function Expect-Rejected([string]$Name, [string]$Directory) {
    $script:activeCase = $Name
    $rejected = $false
    try { $null = Assert-UifNativeSourceMapping -ProjectRoot $Directory } catch { $rejected = $true }
    if (!$rejected) { throw 'An invalid authored fixture was accepted.' }
    $script:passed++
}

function Try-AuthoredLink([string]$Name, [string]$Link, [string]$Target, [bool]$Directory) {
    $script:activeCase = $Name
    try {
        if ($Directory) { $null = [IO.Directory]::CreateSymbolicLink($Link, $Target) }
        else { $null = [IO.File]::CreateSymbolicLink($Link, $Target) }
        return $true
    } catch [PlatformNotSupportedException] { $script:skipped++; return $false }
    catch [UnauthorizedAccessException] { $script:skipped++; return $false }
    catch [System.ComponentModel.Win32Exception] { $script:skipped++; return $false }
}

try {
    $TempRoot = [IO.Path]::GetFullPath($TempRoot)
    if ([IO.Directory]::Exists($TempRoot) -or [IO.File]::Exists($TempRoot)) { throw 'Fixture root must be fresh.' }
    Assert-UifNativePathNotLinked -LiteralPath ([IO.Path]::GetDirectoryName($TempRoot))
    $null = [IO.Directory]::CreateDirectory($TempRoot)
    $created = $true

    $script:activeCase = 'current-source-mapping'
    $outputs = @(Assert-UifNativeSourceMapping -ProjectRoot $ProjectRoot)
    $current = [IO.File]::ReadAllText((Join-Path $ProjectRoot 'UPSTREAM-SOURCE.json')) | ConvertFrom-Json
    if ($outputs.Count -ne 1 -or $outputs[0] -cnotmatch '^[0-9a-f]{64}$' -or $current.nativeFingerprintInputCount -ne 77) { throw 'Current native mapping did not return one fingerprint for 77 inputs.' }
    $script:passed++

    $script:activeCase = 'independent-authored-fingerprint'
    $fixture = New-AuthoredFixture 'valid'
    $outputs = @(Assert-UifNativeSourceMapping -ProjectRoot $fixture)
    if ($outputs.Count -ne 1 -or $outputs[0] -cne '084b9f92457c1decb1ff46ffa75bc7ee449e8c1ff6ab457c41c77191185a7fbb') { throw 'Authored fingerprint disagrees with its independent expected value.' }
    $script:passed++

    $script:activeCase = 'restrictive-extraction-permissions'
    if (!$IsWindows) {
        foreach ($relative in @('Makefile', 'source/main.c')) { [IO.File]::SetUnixFileMode((Join-Path $fixture $relative), ([IO.UnixFileMode]::UserRead -bor [IO.UnixFileMode]::UserWrite)) }
    }
    if ((Assert-UifNativeSourceMapping -ProjectRoot $fixture) -cne '084b9f92457c1decb1ff46ffa75bc7ee449e8c1ff6ab457c41c77191185a7fbb') { throw 'Restrictive extraction permissions changed fingerprint semantics.' }
    $script:passed++

    $fixture = New-AuthoredFixture 'changed-byte'
    [IO.File]::WriteAllText((Join-Path $fixture 'source/main.c'), "int fixture(void) { return 2; }`n")
    Expect-Rejected 'changed-byte' $fixture
    $fixture = New-AuthoredFixture 'missing-input'
    [IO.File]::Delete((Join-Path $fixture 'source/main.c'))
    Expect-Rejected 'missing-input' $fixture
    $fixture = New-AuthoredFixture 'extra-recursive-input'
    [IO.File]::WriteAllText((Join-Path $fixture 'source/extra.bin'), 'authored extra input')
    Expect-Rejected 'extra-recursive-input' $fixture
    $fixture = New-AuthoredFixture 'extra-top-level-input'
    [IO.File]::WriteAllText((Join-Path $fixture 'extra.py'), 'authored extra input')
    Expect-Rejected 'extra-top-level-input' $fixture

    foreach ($case in @(
        @{ Name = 'duplicate-path'; Change = { param($m) $m.nativeFingerprintInputs[1].path = 'Makefile' } },
        @{ Name = 'case-colliding-path'; Change = { param($m) $m.nativeFingerprintInputs[1].path = 'makefile' } },
        @{ Name = 'path-escape'; Change = { param($m) $m.nativeFingerprintInputs[1].path = '../outside.c' } },
        @{ Name = 'absolute-path'; Change = { param($m) $m.nativeFingerprintInputs[1].path = '/outside.c' } },
        @{ Name = 'backslash-path'; Change = { param($m) $m.nativeFingerprintInputs[1].path = 'source\main.c' } },
        @{ Name = 'empty-segment'; Change = { param($m) $m.nativeFingerprintInputs[1].path = 'source//main.c' } },
        @{ Name = 'invalid-hash'; Change = { param($m) $m.nativeFingerprintInputs[0].sha256 = ('z' * 64) } },
        @{ Name = 'invalid-mode'; Change = { param($m) $m.nativeFingerprintInputs[0].gitMode = '120000' } },
        @{ Name = 'invalid-size'; Change = { param($m) $m.nativeFingerprintInputs[0].bytes = -1 } },
        @{ Name = 'oversize-entry'; Change = { param($m) $m.nativeFingerprintInputs[0].bytes = 268435457 } },
        @{ Name = 'wrong-size-type'; Change = { param($m) $m.nativeFingerprintInputs[0].bytes = '12' } },
        @{ Name = 'count-mismatch'; Change = { param($m) $m.nativeFingerprintInputCount = 3 } },
        @{ Name = 'count-limit'; Change = { param($m) $m.nativeFingerprintInputCount = 50001 } },
        @{ Name = 'invalid-entry-type'; Change = { param($m) $m.nativeFingerprintInputs[0] = 'not an entry' } }
    )) {
        $fixture = New-AuthoredFixture $case.Name
        Edit-AuthoredMap $fixture $case.Change
        Expect-Rejected $case.Name $fixture
    }
    $fixture = New-AuthoredFixture 'duplicate-json-property'
    $path = Join-Path $fixture 'UPSTREAM-SOURCE.json'
    $text = [IO.File]::ReadAllText($path).Replace('"schemaVersion": 1', '"schemaVersion": 1, "SchemaVersion": 1')
    [IO.File]::WriteAllText($path, $text)
    Expect-Rejected 'duplicate-json-property' $fixture
    $fixture = New-AuthoredFixture 'malformed-json'
    [IO.File]::WriteAllText((Join-Path $fixture 'UPSTREAM-SOURCE.json'), '{')
    Expect-Rejected 'malformed-json' $fixture

    $fixture = New-AuthoredFixture 'linked-file'
    $target = Join-Path $TempRoot 'authored-file-target.c'
    [IO.File]::Move((Join-Path $fixture 'source/main.c'), $target)
    if (Try-AuthoredLink 'linked-file' (Join-Path $fixture 'source/main.c') $target $false) { Expect-Rejected 'linked-file' $fixture }
    $fixture = New-AuthoredFixture 'linked-native-directory'
    $target = Join-Path $TempRoot 'authored-directory-target'
    [IO.Directory]::Move((Join-Path $fixture 'source'), $target)
    if (Try-AuthoredLink 'linked-native-directory' (Join-Path $fixture 'source') $target $true) { Expect-Rejected 'linked-native-directory' $fixture }
    $fixture = New-AuthoredFixture 'linked-root'
    $link = Join-Path $TempRoot 'authored-root-link'
    if (Try-AuthoredLink 'linked-root' $link $fixture $true) { Expect-Rejected 'linked-root' $link }
    $realParent = Join-Path $TempRoot 'real-parent'
    $null = [IO.Directory]::CreateDirectory($realParent)
    $fixture = New-AuthoredFixture 'parent-project'
    [IO.Directory]::Move($fixture, (Join-Path $realParent 'project'))
    $link = Join-Path $TempRoot 'authored-parent-link'
    if (Try-AuthoredLink 'linked-parent' $link $realParent $true) { Expect-Rejected 'linked-parent' (Join-Path $link 'project') }

    $script:activeCase = 'fixture-byte-cap'
    $fixtureBytes = (Get-ChildItem -LiteralPath $TempRoot -File -Recurse -Force | Measure-Object -Property Length -Sum).Sum
    if ($fixtureBytes -ge 1024 * 1024) { throw 'Authored fixtures exceeded their byte cap.' }
    Write-Output "Source mapping metadata tests passed: $($script:passed); native inputs: 77; symlink checks skipped: $($script:skipped); authored fixture bytes: $fixtureBytes."
} catch {
    Write-Output "Source mapping metadata tests failed: $($script:activeCase)."
    exit 1
} finally {
    if ($created) { Remove-Item -LiteralPath $TempRoot -Recurse -Force }
}
