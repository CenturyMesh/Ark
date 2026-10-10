param(
    [switch]$Rebuild,
    [string]$Document,
    [string]$BuildDirectory = 'out/native-release'
)

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
if (-not [System.IO.Path]::IsPathRooted($BuildDirectory)) {
    $BuildDirectory = Join-Path $root $BuildDirectory
}
$exe = Join-Path $BuildDirectory 'Release/ark-studio.exe'
$documentPath = $null
if ($Document) {
    if (-not [System.IO.Path]::IsPathRooted($Document)) { $Document = Join-Path $root $Document }
    $documentPath = (Resolve-Path -LiteralPath $Document).Path
}

if ($Rebuild -or -not (Test-Path -LiteralPath $exe)) {
    & (Join-Path $root 'scripts/build.ps1') -Test -BuildDirectory $BuildDirectory
    if ($LASTEXITCODE -ne 0) { throw 'Native build or tests failed; ARK was not launched.' }
}
if (-not (Test-Path -LiteralPath $exe)) { throw "ARK executable missing: $exe" }

# Stable launcher selects a verified staged update on the next app start.
$updateLauncher = Join-Path (Split-Path $exe -Parent) 'updates/ark-launch.ps1'
if (Test-Path -LiteralPath $updateLauncher) {
    $arguments = @{ RuntimeDirectory = (Split-Path $exe -Parent) }
    if ($documentPath) { $arguments.Document = $documentPath }
    & $updateLauncher @arguments
    return
}

$launch = @{ FilePath = $exe; WorkingDirectory = $root; WindowStyle = 'Normal' }
if ($documentPath) {
    $launch.ArgumentList = @('--load', ('"' + $documentPath + '"'))
}
# ARK is an interactive window requested by the operator.
Start-Process @launch
