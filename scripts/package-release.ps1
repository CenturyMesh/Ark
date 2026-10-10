param(
    [string]$RuntimeDirectory = 'out/native-release/Release',
    [string]$OutputDirectory = 'out/releases',
    [string]$MsvcRuntimeDirectory
)
$ErrorActionPreference = 'Stop'
$repository = Split-Path $PSScriptRoot -Parent
foreach ($variable in @('RuntimeDirectory', 'OutputDirectory')) {
    $value = Get-Variable -Name $variable -ValueOnly
    if (-not [IO.Path]::IsPathRooted($value)) { Set-Variable -Name $variable -Value (Join-Path $repository $value) }
}
Import-Module (Join-Path $repository 'updates/Ark.Update.psm1') -Force
$version = Get-ArkRuntimeVersion $RuntimeDirectory
if (-not $MsvcRuntimeDirectory) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    if (Test-Path -LiteralPath $vswhere) {
        $visualStudio = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        $redistributables = @(Get-ChildItem -Path (Join-Path $visualStudio 'VC/Redist/MSVC/*/x64/Microsoft.VC*.CRT') -Directory | Sort-Object FullName -Descending)
        if ($redistributables.Count) { $MsvcRuntimeDirectory = $redistributables[0].FullName }
    }
}
if (-not $MsvcRuntimeDirectory) { throw 'Supply -MsvcRuntimeDirectory with the licensed x64 Visual C++ app-local runtime from the build toolchain.' }
[IO.Directory]::CreateDirectory($OutputDirectory) | Out-Null
$staging = Assert-ArkChildPath $OutputDirectory (Join-Path $OutputDirectory ('package-' + [guid]::NewGuid().ToString('N')))
[IO.Directory]::CreateDirectory($staging) | Out-Null
try {
    foreach ($name in @('ark-studio.exe', 'ark-cli.exe', 'ark-mcp.exe', 'ark-occt-worker.exe', 'app-version.json')) { Copy-Item -LiteralPath (Join-Path $RuntimeDirectory $name) -Destination $staging }
    Get-ChildItem -LiteralPath $RuntimeDirectory -Filter '*.dll' -File | ForEach-Object { Assert-ArkNoReparsePoint $RuntimeDirectory $_.FullName; Copy-Item -LiteralPath $_.FullName -Destination $staging }
    foreach ($name in @('vcruntime140.dll', 'vcruntime140_1.dll', 'msvcp140.dll', 'concrt140.dll')) {
        if (-not (Test-Path -LiteralPath (Join-Path $MsvcRuntimeDirectory $name))) { throw "Visual C++ runtime is missing $name." }
        Copy-Item -LiteralPath (Join-Path $MsvcRuntimeDirectory $name) -Destination $staging -Force
    }
    Get-ChildItem -LiteralPath $MsvcRuntimeDirectory -Filter '*.dll' -File | ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination $staging -Force }
    foreach ($directory in @('assets', 'updates')) {
        $source = Join-Path $RuntimeDirectory $directory
        Get-ChildItem -LiteralPath $source -Recurse -Force | ForEach-Object { Assert-ArkNoReparsePoint $RuntimeDirectory $_.FullName }
        Copy-Item -LiteralPath $source -Destination $staging -Recurse
    }
    Copy-Item -LiteralPath (Join-Path $repository 'updates/ARK.cmd') -Destination $staging
    Copy-Item -LiteralPath (Join-Path $repository 'docs/UPDATES.md') -Destination (Join-Path $staging 'UPDATES.md')
    $licenses = Join-Path $staging 'licenses'
    [IO.Directory]::CreateDirectory($licenses) | Out-Null
    Copy-Item -LiteralPath (Join-Path $repository 'third_party/mapbox/LICENSE') -Destination (Join-Path $licenses 'mapbox-earcut-ISC.txt')
    foreach ($name in @('LICENSE', 'LICENSE.md', 'LICENSE.txt')) { if (Test-Path -LiteralPath (Join-Path $repository $name)) { Copy-Item -LiteralPath (Join-Path $repository $name) -Destination $licenses } }
    foreach ($file in @('LICENSE_LGPL_21.txt', 'OCCT_LGPL_EXCEPTION.txt')) {
        $hits = @(Get-ChildItem -Path (Join-Path $repository 'out/deps/occt/*') -Directory | ForEach-Object { $candidate=Join-Path $_.FullName $file; if (Test-Path -LiteralPath $candidate) { $candidate } })
        if ($hits.Count) { Copy-Item -LiteralPath $hits[0] -Destination $licenses }
    }
    Get-ArkRuntimeVersion $staging | Out-Null
    $name = "ark-windows-x64-$version.zip"
    $archive = Join-Path $OutputDirectory $name
    if (Test-Path -LiteralPath $archive) { throw "Package already exists: $archive. Use a new output directory or a new version." }
    Compress-ArkPackage $staging $archive
    $info = Get-Item -LiteralPath $archive
    $manifest = @{ schema=1; product='ARK'; version=$version; platform='windows-x64'; package=@{ name=$name; url="https://github.com/CenturyMesh/Ark-Releases/releases/download/v$version/$name"; size=$info.Length; sha256=(Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant() } }
    Assert-ArkManifest $manifest $version
    Write-ArkAtomicJson (Join-Path $OutputDirectory 'ark-update.json') $manifest
    Write-Output "Prepared release assets: $archive and $(Join-Path $OutputDirectory 'ark-update.json')"
} finally { if (Test-Path -LiteralPath $staging) { Remove-Item -LiteralPath (Assert-ArkChildPath $OutputDirectory $staging) -Recurse -Force } }
