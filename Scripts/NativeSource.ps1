#requires -Version 7.4
# Standalone metadata checks; no parent repository, Git or SDK dependency.
Set-StrictMode -Version Latest

function Get-UifNativeSourceFingerprint {
    param(
        [Parameter(Mandatory = $true)]
        [string]$ProjectRoot
    )

    $root = [System.IO.Path]::GetFullPath($ProjectRoot).TrimEnd([char[]]@('\', '/'))
    $inputs = [System.Collections.Generic.List[System.IO.FileInfo]]::new()
    foreach ($name in @('Makefile', 'Dockerfile')) {
        $path = Join-Path $root $name
        if (Test-Path -LiteralPath $path -PathType Leaf) {
            $inputs.Add((Get-Item -LiteralPath $path))
        }
    }
    foreach ($file in Get-ChildItem -LiteralPath $root -File -Force) {
        if ($file.Extension -in @('.c', '.cc', '.cpp', '.h', '.hpp', '.s', '.S', '.ld', '.lds', '.py')) {
            $inputs.Add($file)
        }
    }
    foreach ($directoryName in @('source', 'include', 'assets', 'app_booter_clean')) {
        $directory = Join-Path $root $directoryName
        if (Test-Path -LiteralPath $directory -PathType Container) {
            foreach ($file in Get-ChildItem -LiteralPath $directory -File -Recurse -Force) {
                if (($file.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
                    throw "Native source fingerprint cannot traverse a reparse point: $($file.FullName)"
                }
                $inputs.Add($file)
            }
        }
    }
    if ($inputs.Count -eq 0 -or $inputs.Count -gt 50000) {
        throw "Native source fingerprint expected between 1 and 50000 files under $root; found $($inputs.Count)."
    }

    $lines = [System.Collections.Generic.List[string]]::new()
    foreach ($file in $inputs) {
        $relative = $file.FullName.Substring($root.Length).TrimStart([char[]]@('\', '/')).Replace('\', '/')
        $hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $file.FullName).Hash.ToLowerInvariant()
        $lines.Add("$relative`t$hash")
    }
    $payload = [System.Text.Encoding]::UTF8.GetBytes((($lines | Sort-Object) -join "`n"))
    $sha256 = [System.Security.Cryptography.SHA256]::Create()
    try {
        return ([System.BitConverter]::ToString($sha256.ComputeHash($payload))).Replace('-', '').ToLowerInvariant()
    }
    finally {
        $sha256.Dispose()
    }
}

function Assert-UifNativePathNotLinked {
    param([Parameter(Mandatory)][string]$LiteralPath)
    $path = [IO.Path]::GetFullPath($LiteralPath)
    while ($path) {
        $attributes = [IO.File]::GetAttributes($path)
        if (($attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw 'Native source paths must not traverse symbolic links or reparse points.'
        }
        $parent = [IO.Path]::GetDirectoryName($path)
        if ($parent -eq $path) { break }
        $path = $parent
    }
}

function Assert-UifNativeJsonShape {
    param([System.Text.Json.JsonElement]$Element, [ref]$Nodes)
    $Nodes.Value++
    if ($Nodes.Value -gt 50000) { throw 'Native source mapping has too many JSON values.' }
    if ($Element.ValueKind -eq [System.Text.Json.JsonValueKind]::Object) {
        $names = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
        foreach ($property in $Element.EnumerateObject()) {
            if (!$names.Add($property.Name)) { throw 'Native source mapping has duplicate JSON properties.' }
            Assert-UifNativeJsonShape -Element $property.Value -Nodes $Nodes
        }
    } elseif ($Element.ValueKind -eq [System.Text.Json.JsonValueKind]::Array) {
        foreach ($value in $Element.EnumerateArray()) { Assert-UifNativeJsonShape -Element $value -Nodes $Nodes }
    }
}

function Get-UifNativeJsonProperty {
    param([System.Text.Json.JsonElement]$Element, [string]$Name, [System.Text.Json.JsonValueKind]$Kind)
    $value = [System.Text.Json.JsonElement]::new()
    if (!$Element.TryGetProperty($Name, [ref]$value) -or $value.ValueKind -ne $Kind) {
        throw 'Native source mapping is missing a required property or has an invalid type.'
    }
    return $value
}

function Assert-UifNativeSourceMapping {
    [CmdletBinding()]
    param([Parameter(Mandatory)][string]$ProjectRoot)

    # These bound metadata validation, not the canonical fingerprint algorithm.
    $maximumDocumentBytes = 1024 * 1024
    $maximumFileBytes = 256L * 1024 * 1024
    $maximumTotalBytes = 1024L * 1024 * 1024
    $root = [IO.Path]::GetFullPath($ProjectRoot).TrimEnd([char[]]@('\', '/'))
    if (![IO.Directory]::Exists($root)) { throw 'Native source project root is not a directory.' }
    Assert-UifNativePathNotLinked -LiteralPath $root
    $mapPath = Join-Path $root 'UPSTREAM-SOURCE.json'
    Assert-UifNativePathNotLinked -LiteralPath $mapPath
    $stream = [IO.File]::Open($mapPath, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
    try {
        $length = $stream.Length
        if ($length -lt 2 -or $length -gt $maximumDocumentBytes) { throw 'Native source mapping exceeds its document limit.' }
        $body = [byte[]]::new([int]$length)
        $offset = 0
        while ($offset -lt $body.Length) {
            $read = $stream.Read($body, $offset, $body.Length - $offset)
            if ($read -eq 0) { throw 'Native source mapping is truncated.' }
            $offset += $read
        }
        if ($stream.ReadByte() -ne -1 -or $stream.Length -ne $length) { throw 'Native source mapping changed length.' }
    } finally { $stream.Dispose() }
    $options = [System.Text.Json.JsonDocumentOptions]::new()
    $options.MaxDepth = 16
    $document = [System.Text.Json.JsonDocument]::Parse([ReadOnlyMemory[byte]]$body, $options)
    try {
        $map = $document.RootElement
        if ($map.ValueKind -ne [System.Text.Json.JsonValueKind]::Object) { throw 'Native source mapping must be an object.' }
        $nodes = 0
        Assert-UifNativeJsonShape -Element $map -Nodes ([ref]$nodes)
        $schema = Get-UifNativeJsonProperty $map 'schemaVersion' Number
        $schemaNumber = 0
        if (!$schema.TryGetInt32([ref]$schemaNumber) -or $schemaNumber -ne 1) { throw 'Unsupported native source mapping schema.' }
        foreach ($name in @('upstreamRepository', 'upstreamCommit', 'upstreamComponent', 'componentVersion')) {
            $value = (Get-UifNativeJsonProperty $map $name String).GetString()
            if ([string]::IsNullOrWhiteSpace($value) -or $value.Length -gt 256 -or $value -match '[\x00-\x1f\x7f]') { throw 'Invalid native source mapping identity.' }
            if ($name -eq 'upstreamCommit' -and $value -cnotmatch '^[0-9a-fA-F]{40}$') { throw 'Invalid upstream source commit.' }
        }
        $countElement = Get-UifNativeJsonProperty $map 'nativeFingerprintInputCount' Number
        $count = 0
        if (!$countElement.TryGetInt32([ref]$count) -or $count -lt 1 -or $count -gt 50000) { throw 'Invalid native source input count.' }
        $entries = Get-UifNativeJsonProperty $map 'nativeFingerprintInputs' Array
        if ($entries.GetArrayLength() -ne $count) { throw 'Native source input count does not match its entries.' }
        $expected = [Collections.Generic.Dictionary[string, object]]::new([StringComparer]::OrdinalIgnoreCase)
        $totalBytes = 0L
        foreach ($entry in $entries.EnumerateArray()) {
            if ($entry.ValueKind -ne [System.Text.Json.JsonValueKind]::Object -or @($entry.EnumerateObject()).Count -ne 4) { throw 'Malformed native source entry.' }
            $relative = (Get-UifNativeJsonProperty $entry 'path' String).GetString()
            if ([string]::IsNullOrEmpty($relative) -or $relative.Length -gt 1024 -or [IO.Path]::IsPathRooted($relative) -or $relative.Contains('\') -or $relative.Contains(':') -or $relative -match '[\x00-\x1f\x7f]') { throw 'Unsafe native source relative path.' }
            foreach ($part in $relative.Split('/')) {
                if ($part -in @('', '.', '..') -or $part.EndsWith('.') -or $part.EndsWith(' ')) { throw 'Unsafe native source path segment.' }
            }
            $mode = (Get-UifNativeJsonProperty $entry 'gitMode' String).GetString()
            $hash = (Get-UifNativeJsonProperty $entry 'sha256' String).GetString()
            $sizeElement = Get-UifNativeJsonProperty $entry 'bytes' Number
            $size = 0L
            if ($mode -cnotin @('100644', '100755') -or $hash -cnotmatch '^[0-9a-fA-F]{64}$' -or !$sizeElement.TryGetInt64([ref]$size) -or $size -lt 0 -or $size -gt $maximumFileBytes) { throw 'Invalid native source mode, hash or size.' }
            $totalBytes += $size
            if ($totalBytes -gt $maximumTotalBytes) { throw 'Native source mapping exceeds its total byte limit.' }
            if ($expected.ContainsKey($relative)) { throw 'Native source mapping has duplicate or case-colliding paths.' }
            $expected.Add($relative, @{ Bytes = $size; Hash = $hash.ToLowerInvariant(); Mode = $mode })
        }

        # Enumerate only the canonical native-input universe, without following links.
        $actual = [Collections.Generic.Dictionary[string, string]]::new([StringComparer]::OrdinalIgnoreCase)
        $pending = [Collections.Generic.Queue[string]]::new()
        $visited = 0
        foreach ($path in [IO.Directory]::EnumerateFileSystemEntries($root)) {
            $visited++
            if ($visited -gt 100000) { throw 'Native source enumeration exceeds its entry limit.' }
            $name = [IO.Path]::GetFileName($path)
            $attributes = [IO.File]::GetAttributes($path)
            if ($name -in @('source', 'include', 'assets', 'app_booter_clean')) {
                if (($attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0 -or ($attributes -band [IO.FileAttributes]::Directory) -eq 0) { throw 'Native source directory is linked or invalid.' }
                $pending.Enqueue($path)
            } elseif ($name -in @('Makefile', 'Dockerfile') -or [IO.Path]::GetExtension($name) -in @('.c', '.cc', '.cpp', '.h', '.hpp', '.s', '.S', '.ld', '.lds', '.py')) {
                if (($attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0 -or ($attributes -band [IO.FileAttributes]::Directory) -ne 0) { throw 'Native source input is linked or invalid.' }
                if ($actual.ContainsKey($name)) { throw 'Native source inputs have case-colliding paths.' }
                $actual.Add($name, $path)
            }
        }
        while ($pending.Count -gt 0) {
            $directory = $pending.Dequeue()
            foreach ($path in [IO.Directory]::EnumerateFileSystemEntries($directory)) {
                $visited++
                if ($visited -gt 100000) { throw 'Native source enumeration exceeds its entry limit.' }
                $attributes = [IO.File]::GetAttributes($path)
                if (($attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw 'Native source inputs must not contain symbolic links or reparse points.' }
                if (($attributes -band [IO.FileAttributes]::Directory) -ne 0) { $pending.Enqueue($path); continue }
                $relative = [IO.Path]::GetRelativePath($root, $path).Replace('\', '/')
                if ($actual.ContainsKey($relative)) { throw 'Native source inputs have case-colliding paths.' }
                $actual.Add($relative, $path)
                if ($actual.Count -gt 50000) { throw 'Native source input count exceeds its limit.' }
            }
        }
        if ($actual.Count -ne $expected.Count) { throw 'Native source inputs are missing or contain extra files.' }
        foreach ($relative in $expected.Keys) {
            if (!$actual.ContainsKey($relative) -or $relative -cne [IO.Path]::GetRelativePath($root, $actual[$relative]).Replace('\', '/')) { throw 'Native source inputs do not match the mapped paths.' }
            $path = $actual[$relative]
            $row = $expected[$relative]
            Assert-UifNativePathNotLinked -LiteralPath $path
            # gitMode records the source archive's intended Git mode. Extraction
            # permissions depend on the host and umask; they are not fingerprinted.
            $sourceStream = [IO.File]::Open($path, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
            $digest = [Security.Cryptography.IncrementalHash]::CreateHash([Security.Cryptography.HashAlgorithmName]::SHA256)
            try {
                if ($sourceStream.Length -ne $row.Bytes) { throw 'Native source size does not match its mapping.' }
                $buffer = [byte[]]::new(65536)
                $remaining = $row.Bytes
                while ($remaining -gt 0) {
                    $read = $sourceStream.Read($buffer, 0, [int][Math]::Min($remaining, $buffer.Length))
                    if ($read -eq 0) { throw 'Native source input is truncated.' }
                    $digest.AppendData($buffer, 0, $read)
                    $remaining -= $read
                }
                if ($sourceStream.ReadByte() -ne -1 -or $sourceStream.Length -ne $row.Bytes) { throw 'Native source size changed while reading.' }
                if ([Convert]::ToHexString($digest.GetHashAndReset()).ToLowerInvariant() -cne $row.Hash) { throw 'Native source hash does not match its mapping.' }
            } finally { $digest.Dispose(); $sourceStream.Dispose() }
        }
        # The expected payload detects a change between bounded validation and the
        # canonical function, whose implementation and output are kept verbatim.
        $lines = foreach ($relative in $expected.Keys) { "$relative`t$($expected[$relative].Hash)" }
        $payload = [Text.Encoding]::UTF8.GetBytes((($lines | Sort-Object) -join "`n"))
        $expectedFingerprint = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($payload)).ToLowerInvariant()
        $fingerprint = Get-UifNativeSourceFingerprint -ProjectRoot $root
        if ($fingerprint -cne $expectedFingerprint) { throw 'Native source fingerprint changed during validation.' }
        return $fingerprint
    } finally { $document.Dispose() }
}
