param(
    [string]$RuntimeDirectory,
    [string]$InstallRoot,
    [switch]$DesktopShortcut
)
$ErrorActionPreference = 'Stop'
$repository = Split-Path $PSScriptRoot -Parent
if (-not $RuntimeDirectory) {
    $RuntimeDirectory = if (Test-Path -LiteralPath (Join-Path $PSScriptRoot 'app-version.json')) { $PSScriptRoot } else { Join-Path $repository 'out/native-release/Release' }
}
if (-not [IO.Path]::IsPathRooted($RuntimeDirectory)) { $RuntimeDirectory = Join-Path $repository $RuntimeDirectory }
Import-Module (Join-Path $RuntimeDirectory 'updates/Ark.Update.psm1') -Force
if (-not $InstallRoot) { $InstallRoot = Get-ArkInstallRoot }
$lock = Enter-ArkUpdateLock $InstallRoot
try { Initialize-ArkInstall $RuntimeDirectory $InstallRoot | Out-Null }
finally { $lock.Dispose() }
if ($DesktopShortcut) {
    $shell = New-Object -ComObject WScript.Shell
    $shortcut = $shell.CreateShortcut((Join-Path ([Environment]::GetFolderPath('Desktop')) 'ARK.lnk'))
    $shortcut.TargetPath = Join-Path $env:SystemRoot 'System32/WindowsPowerShell/v1.0/powershell.exe'
    $shortcut.Arguments = '-NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File "' + (Join-Path $InstallRoot 'launcher/ark-launch.ps1') + '" -InstallRoot "' + $InstallRoot + '"'
    $shortcut.WorkingDirectory = $InstallRoot
    $shortcut.WindowStyle = 7
    $state = Get-ArkState $InstallRoot
    $shortcut.IconLocation = (Join-Path $InstallRoot "versions/$($state.current)/ark-studio.exe") + ',0'
    $shortcut.Save()
}
Write-Output "ARK portable launcher: $(Join-Path $InstallRoot 'ARK.cmd')"

