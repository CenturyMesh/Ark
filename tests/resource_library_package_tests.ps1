param([Parameter(Mandatory=$true)][string]$OutputDirectory)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$repository = Split-Path $PSScriptRoot -Parent
$source = Join-Path $repository 'assets/ui'
[IO.Directory]::CreateDirectory($OutputDirectory) | Out-Null
$first = Join-Path $OutputDirectory 'first.arklib'
$second = Join-Path $OutputDirectory 'second.arklib'
$expected = @('ark-app-mark.png','ark-authoring-atlas-v1.png','ark-detail-atlas-v1.png','ark-domain-atlas-v1.png','ark-tool-atlas-v1.png','ark-wordmark.png')
& (Join-Path $repository 'scripts/build-resource-library.ps1') -SourceDirectory $source -OutputPath $first
$hash = (Get-FileHash -LiteralPath $first -Algorithm SHA256).Hash
$written = (Get-Item -LiteralPath $first).LastWriteTimeUtc
& (Join-Path $repository 'scripts/build-resource-library.ps1') -SourceDirectory $source -OutputPath $first
if ((Get-FileHash -LiteralPath $first -Algorithm SHA256).Hash -cne $hash -or (Get-Item -LiteralPath $first).LastWriteTimeUtc -ne $written) { throw 'An identical resource build rewrote or changed its output.' }
& (Join-Path $repository 'scripts/build-resource-library.ps1') -SourceDirectory $source -OutputPath $second
if ([Convert]::ToBase64String([IO.File]::ReadAllBytes($first)) -cne [Convert]::ToBase64String([IO.File]::ReadAllBytes($second))) { throw 'Independent resource builds are not byte-identical.' }
$stream = [IO.File]::OpenRead($first)
$reader = New-Object IO.BinaryReader $stream
$inventory = @()
try {
    if ([Text.Encoding]::ASCII.GetString($reader.ReadBytes(8)) -cne 'ARKLIB01') { throw 'Unexpected binary resource signature.' }
    $indexSize = $reader.ReadUInt32()
    if ($indexSize -gt 1MB) { throw 'Resource index is unexpectedly large.' }
    $index = [Text.Encoding]::UTF8.GetString($reader.ReadBytes($indexSize)) | ConvertFrom-Json
    if ($index.format -cne 'ark.resource-library.v1' -or $index.entries.Count -ne $expected.Count) { throw 'Resource index does not contain the six reviewed UI assets.' }
    [int64]$offset = 0
    for ($i=0; $i -lt $expected.Count; $i++) {
        $entry = $index.entries[$i]
        if ($entry.name -cne $expected[$i] -or $entry.offset -ne $offset -or $entry.media_type -cne 'image/png') { throw 'Unexpected resource order, offset, name or media type.' }
        $originalPath = Join-Path $source $expected[$i]
        $original = [IO.File]::ReadAllBytes($originalPath)
        $payload = $reader.ReadBytes($entry.size)
        if ($payload.Length -ne $entry.size -or [Convert]::ToBase64String($payload) -cne [Convert]::ToBase64String($original)) { throw "Resource bytes changed: $($entry.name)" }
        $originalHash = (Get-FileHash -LiteralPath $originalPath -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($entry.sha256 -cne $originalHash) { throw "Resource digest changed: $($entry.name)" }
        $inventory += [ordered]@{name=$entry.name; bytes=$payload.Length; sha256=$originalHash; identical=$true}
        $offset += $entry.size
    }
    if ($stream.Position -ne $stream.Length) { throw 'Resource library has undeclared trailing bytes.' }
} finally { $reader.Dispose() }
$result = [ordered]@{format='ark.resource-library-package-test.v1';passed=$true;deterministic=$true;identical_rebuild_preserved_mtime=$true;container_sha256=$hash.ToLowerInvariant();entries=$inventory}
[IO.File]::WriteAllText((Join-Path $OutputDirectory 'resource-library-result.json'), ($result | ConvertTo-Json -Depth 6), (New-Object Text.UTF8Encoding($false)))
Write-Output 'Resource package: six byte-identical PNGs; deterministic independent output; identical rebuild did not rewrite output.'
