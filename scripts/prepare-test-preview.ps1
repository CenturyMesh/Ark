param(
    [string]$BuildDirectory = 'out/native',
    [string]$Name = 'r2',
    [string]$OcctSourceDirectory = 'out/deps/occt/cache/OCCT-8_0_1',
    [string]$CompilerDirectory = 'out/tools/llvm-mingw-20261006-ucrt-x86_64'
)
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
if ($Name -notmatch '^[A-Za-z0-9_-]+$') { throw 'Preview name must contain only letters, numbers, underscore or hyphen.' }
if (-not [IO.Path]::IsPathRooted($BuildDirectory)) { $BuildDirectory = Join-Path $repoRoot $BuildDirectory }
$binaryRoot = (Resolve-Path -LiteralPath $BuildDirectory).Path
if (Test-Path -LiteralPath (Join-Path $binaryRoot 'Release/ark-studio.exe')) { $binaryRoot = Join-Path $binaryRoot 'Release' }
if (-not (Test-Path -LiteralPath (Join-Path $binaryRoot 'ark-studio.exe'))) { throw "Build Studio first: $binaryRoot" }
$previewRoot = Join-Path $repoRoot "out/test-preview/$Name"
if (Test-Path -LiteralPath $previewRoot) { throw "Preview already exists; choose a new name: $previewRoot" }
New-Item -ItemType Directory -Path $previewRoot -Force | Out-Null
foreach ($fileName in @('ark-studio.exe','ark-mcp.exe','ark-cli.exe','ark-occt-worker.exe')) {
    $source = Join-Path $binaryRoot $fileName
    if (Test-Path -LiteralPath $source) { Copy-Item -LiteralPath $source -Destination $previewRoot }
}
Get-ChildItem -LiteralPath $binaryRoot -Filter '*.dll' -File | ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination $previewRoot }
$metadata = Join-Path $BuildDirectory 'app-version.json'
if (Test-Path -LiteralPath $metadata) { Copy-Item -LiteralPath $metadata -Destination $previewRoot }
foreach ($fileName in @('LICENSE','NOTICE','release.json')) {
    $source = Join-Path $repoRoot $fileName
    if (Test-Path -LiteralPath $source) { Copy-Item -LiteralPath $source -Destination $previewRoot }
}
foreach ($folderName in @('assets')) {
    $source = Join-Path $repoRoot $folderName
    if (Test-Path -LiteralPath $source) { Copy-Item -LiteralPath $source -Destination $previewRoot -Recurse }
}
$licensesRoot = Join-Path $previewRoot 'licenses'
New-Item -ItemType Directory -Path $licensesRoot | Out-Null
foreach ($noticeFile in @('third_party/mapbox/LICENSE','third_party/sqlite/README.md','third_party/stb/LICENSE','third_party/stb/README.md','third_party/zstd/LICENSE','third_party/zstd/README.md','docs/THIRD_PARTY_NOTICES.md')) {
    $source = Join-Path $repoRoot $noticeFile
    Copy-Item -LiteralPath $source -Destination (Join-Path $licensesRoot ($noticeFile -replace '/', '_'))
}
if (Test-Path -LiteralPath (Join-Path $binaryRoot 'ark-occt-worker.exe')) {
    if (-not [IO.Path]::IsPathRooted($OcctSourceDirectory)) { $OcctSourceDirectory = Join-Path $repoRoot $OcctSourceDirectory }
    foreach ($noticeFile in @('LICENSE_LGPL_21.txt','OCCT_LGPL_EXCEPTION.txt')) {
        Copy-Item -LiteralPath (Join-Path $OcctSourceDirectory $noticeFile) -Destination $licensesRoot
    }
}
if (Test-Path -LiteralPath (Join-Path $binaryRoot 'libc++.dll')) {
    if (-not [IO.Path]::IsPathRooted($CompilerDirectory)) { $CompilerDirectory = Join-Path $repoRoot $CompilerDirectory }
    Copy-Item -LiteralPath (Join-Path $CompilerDirectory 'LICENSE.TXT') -Destination (Join-Path $licensesRoot 'LLVM-Apache-exception.txt')
    Get-ChildItem -LiteralPath (Join-Path $CompilerDirectory 'aarch64-w64-mingw32/share/mingw32') -Filter 'COPYING*' -File | ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination $licensesRoot }
}
$exePath = Join-Path $previewRoot 'ark-studio.exe'
Write-Output "Preview prepared: $exePath"
Write-Output "Run: & '$exePath'"
