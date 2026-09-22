param([switch]$Rebuild, [string]$Document)
$ErrorActionPreference = 'Stop'
$exe = Join-Path $PSScriptRoot 'out/build/ark-studio.exe'
if ($Rebuild -or !(Test-Path -LiteralPath $exe)) { & (Join-Path $PSScriptRoot 'scripts/build.ps1') -Test }
$launch = @{ FilePath = $exe; WorkingDirectory = $PSScriptRoot }
if ($Document) { $launch.ArgumentList = @('--load', ('"' + $Document + '"')) }
# This is the interactive application the user explicitly requested to see.
Start-Process @launch
