param(
    [Parameter(Mandatory=$true)][string]$InstallerPath,
    [string]$PreviousInstallerPath,
    [Parameter(Mandatory=$true)][string]$OutputDirectory,
    [switch]$CaptureWizard,
    [switch]$LayoutOnly
)
# Exercises a private test setup (scripts/build-installer.ps1 -TestIsolationId <id>) with the real payload:
# remembered folder with spaces, replacement of an Inno Setup registration, runtime and licence payload,
# repeat install, optional upgrade/rollback state, uninstall, and preservation of synthetic drawings/settings plus
# the user's real launcher pointer, shortcuts and registrations. -CaptureWizard renders every page through the
# setup's own -SelfTest page renderer and checks its layout report.
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$repository = Split-Path $PSScriptRoot -Parent
if ($LayoutOnly -and -not $CaptureWizard) { throw '-LayoutOnly requires -CaptureWizard for the private page renderer.' }
Import-Module (Join-Path $repository 'updates/Ark.Update.psm1') -Force
Import-Module (Join-Path $repository 'scripts/Ark.SetupPayload.psm1') -Force
$InstallerPath = [IO.Path]::GetFullPath($InstallerPath)
$match = [regex]::Match([IO.Path]::GetFileName($InstallerPath), '-test-([a-zA-Z0-9-]{1,48})\.exe$')
if (-not $match.Success) { throw 'Smoke tests require an installer built with -TestIsolationId.' }
$testId = $match.Groups[1].Value
if ($PreviousInstallerPath -and [IO.Path]::GetFileName($PreviousInstallerPath) -notmatch ('-test-' + [regex]::Escape($testId) + '\.exe$')) { throw 'Both installers must use the same isolated test identity.' }

# The setup under test carries the current payload footer (scripts/Ark.SetupPayload.psm1).
if (-not (Find-ArkSetupPayload $InstallerPath)) { throw 'The installer under test is not an ARK setup with a payload.' }

$fixture = [IO.Path]::GetFullPath((Join-Path $OutputDirectory ('smoke-' + [guid]::NewGuid().ToString('N'))))
$installRoot = Join-Path $fixture 'Chosen folder with spaces'
[IO.Directory]::CreateDirectory($installRoot) | Out-Null
$drawing = Join-Path $installRoot 'my-drawing.ark'
$settings = Join-Path $fixture 'user-settings.json'
# Setup refuses a non-empty folder that is not an ARK install: a previous WPF setup gets the folder empty, and the
# sentinel drawing is restored after it (its bytes, and so its hash, are the same).
[IO.File]::WriteAllText($drawing, 'Synthetic drawing preservation sentinel - never a user document')
[IO.File]::WriteAllText($settings, '{"synthetic":"preserve"}')
$sentinels = @($drawing, $settings)
$desktopShortcut = Join-Path ([Environment]::GetFolderPath('Desktop')) 'ARK.lnk'
$startShortcut = Join-Path ([Environment]::GetFolderPath('Programs')) 'ARK/ARK.lnk'
$livePointer = Join-Path $env:LOCALAPPDATA 'ARK/app/current.json'
$protected = @($desktopShortcut, $startShortcut, $livePointer) | Where-Object { Test-Path -LiteralPath $_ }
$before = @{}
foreach ($path in @($sentinels) + @($protected)) { $before[$path] = (Get-FileHash -LiteralPath $path).Hash }
$uninstallRoot = 'HKCU:/Software/Microsoft/Windows/CurrentVersion/Uninstall'
function Get-Registration([string]$Name) {
    $path = "$uninstallRoot/$Name"
    if (-not (Test-Path -LiteralPath $path)) { return '' }
    return ((Get-ItemProperty -LiteralPath $path).PSObject.Properties | Where-Object { $_.Name -notlike 'PS*' } | Sort-Object Name | ForEach-Object { "$($_.Name)=$($_.Value)" }) -join ';'
}
$realRegistrations = @{}
foreach ($name in @('MeshSphere.ARK', 'MeshSphere.ARK_is1')) { $realRegistrations[$name] = Get-Registration $name }
$registry = "$uninstallRoot/MeshSphere.ARK.Test.$testId"
$legacy = $registry + '_is1'

function Invoke-PrivateSetup([string]$Executable, [string[]]$Options) {
    $process = Start-Process -FilePath $Executable -ArgumentList $Options -WindowStyle Hidden -PassThru
    if (-not $process.WaitForExit(300000)) {
        & taskkill.exe /PID $process.Id /T /F | Out-Null
        throw 'Private installer exceeded its 300-second test timeout.'
    }
    if ($process.ExitCode -ne 0) { throw "Private setup $([IO.Path]::GetFileName($Executable)) $($Options -join ' ') returned $($process.ExitCode); see $env:TEMP\ARK-Setup.log." }
}
$quotedRoot = '"' + $installRoot + '"'
try {
    $oldState = $null
    if ($PreviousInstallerPath) {
        $PreviousInstallerPath = [IO.Path]::GetFullPath($PreviousInstallerPath)
        if (Test-ArkSetupFile $PreviousInstallerPath) {
            $saved = [IO.File]::ReadAllBytes($drawing); Remove-Item -LiteralPath $drawing
            try { Invoke-PrivateSetup $PreviousInstallerPath @('--silent', '--dir', $quotedRoot) } finally { [IO.File]::WriteAllBytes($drawing, $saved) }
        }
        else { Invoke-PrivateSetup $PreviousInstallerPath @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', ('/DIR="' + $installRoot + '"'), ('/LOG="' + $fixture + '/previous.log"')) }
        $oldState = Get-ArkState $installRoot
    } else {
        # A synthetic isolated Inno Setup registration: the new setup must remember its folder and replace it.
        New-Item -Path $legacy -Force | Out-Null
        Set-ItemProperty -LiteralPath $legacy -Name InstallLocation -Value ($installRoot + '\')
        Set-ItemProperty -LiteralPath $legacy -Name UninstallString -Value ('"' + $installRoot + '\unins000.exe"')
        foreach ($name in @('unins000.exe', 'unins000.dat')) { [IO.File]::WriteAllText((Join-Path $installRoot $name), 'Synthetic Inno Setup uninstaller file') }
    }
    # No --dir: the remembered (Inno or previous) folder must be used.
    Invoke-PrivateSetup $InstallerPath @('--silent')
    $registered = Get-ItemProperty -LiteralPath $registry
    if ([IO.Path]::GetFullPath($registered.InstallLocation).TrimEnd('\') -ine $installRoot.TrimEnd('\')) { throw 'The remembered folder was not used.' }
    if ($registered.Publisher -cne 'MeshSphere Software' -or $registered.QuietUninstallString -notmatch [regex]::Escape($installRoot + '\uninstall.exe')) { throw 'The installed-app registration is incomplete.' }
    if (Test-Path -LiteralPath $legacy) { throw 'The Inno Setup registration was not replaced.' }
    foreach ($name in @('unins000.exe', 'unins000.dat')) { if (Test-Path -LiteralPath (Join-Path $installRoot $name)) { throw "The Inno Setup uninstaller file was not replaced: $name" } }
    $state = Get-ArkState $installRoot
    $version = if ($state.pending) { $state.pending } else { $state.current }
    Assert-ArkInstalledRuntime $installRoot $version | Out-Null
    foreach ($name in @('ark-studio.exe','ark-cli.exe','ark-mcp.exe','ark-occt-worker.exe','licenses/ARK-Apache-2.0.txt','licenses/ARK-NOTICE.txt','licenses/LICENSE_LGPL_21.txt','licenses/OCCT_LGPL_EXCEPTION.txt')) {
        if (-not (Test-Path -LiteralPath (Join-Path $installRoot "versions/$version/$name") -PathType Leaf)) { throw "Installed payload is missing $name." }
    }
    foreach ($name in @('ARK.cmd', 'launcher/ark-launch.ps1', 'uninstall.exe', 'ark-setup.json', 'uninstall-notices.txt')) {
        if (-not (Test-Path -LiteralPath (Join-Path $installRoot $name) -PathType Leaf)) { throw "Installation is missing $name." }
    }
    if ((Get-Content -LiteralPath (Join-Path $installRoot 'uninstall-notices.txt') -Raw) -notmatch 'Microsoft\.NETCore\.App LICENSE') { throw 'The bundled .NET runtime notices are missing.' }
    if ((Read-ArkJson (Join-Path $installRoot 'ark-setup.json') 64KB).shortcuts.Count -ne 0) { throw 'An isolated test setup must not create shortcuts.' }
    $snapshot = [IO.File]::ReadAllText((Join-Path $installRoot 'current.json'))
    Invoke-PrivateSetup $InstallerPath @('--silent')
    if ([IO.File]::ReadAllText((Join-Path $installRoot 'current.json')) -cne $snapshot) { throw 'Reinstall changed the version selection.' }
    if ($oldState) {
        if ($state.current -cne $oldState.current -or $state.pending -cne $version -or $version -ceq $oldState.current) { throw 'Upgrade did not retain the old runtime and stage the newer one.' }
        $activated = Start-ArkActivation $installRoot
        if ($activated.current -cne $version -or $activated.previous -cne $oldState.current) { throw 'Prepared runtime activation lost rollback custody.' }
        Complete-ArkActivation $installRoot $true | Out-Null
        if ((Undo-ArkUpdate $installRoot) -cne $oldState.current) { throw 'Manual rollback failed.' }
    }
    if ($CaptureWizard) {
        $captures = Join-Path $fixture 'captures'
        $options = @('--screenshot', ('"' + $captures + '"'))
        if ($LayoutOnly) { $options += '--layout-only' }
        try { Invoke-PrivateSetup $InstallerPath $options }
        catch { throw "The page renderer failed (the setup must be built with -SelfTest): $($_.Exception.Message)" }
        $layout = [IO.File]::ReadAllText((Join-Path $captures 'layout.txt'))
        foreach ($page in @('welcome','options','installing','done','error','uninstall')) {
            if ($layout -notmatch "(?m)^${page}: elements=[1-9][0-9]*; outside=0; overlap=0; clipped=0;") { throw "Page $page failed its layout checks: $layout" }
        }
        # The wandering sphere stays in the right half, and (unless Windows animations are off) moves and eases.
        if ($layout -notmatch '(?m)^motion: .*; inside=1') { throw "The sphere's wandering range leaves the right half: $layout" }
        if ($layout -notmatch '(?m)^motion-live: (.*; ok=1|Windows animations are off)') { throw "The sphere animation did not run or ease: $layout" }
        if (-not $LayoutOnly -and @(Get-ChildItem -LiteralPath $captures -Filter '*.png').Count -ne 6) { throw 'Missing page captures.' }
    }
} finally {
    $uninstaller = Join-Path $installRoot 'uninstall.exe'
    if (Test-Path -LiteralPath $uninstaller) { Invoke-PrivateSetup $uninstaller @('--uninstall', '--quiet') }
    elseif (Test-Path -LiteralPath (Join-Path $installRoot 'unins000.exe') -PathType Leaf) {
        if ((Get-Item -LiteralPath (Join-Path $installRoot 'unins000.exe')).Length -gt 1KB) { Invoke-PrivateSetup (Join-Path $installRoot 'unins000.exe') @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART') }
    }
    if (Test-Path -LiteralPath $legacy) { Remove-Item -LiteralPath $legacy -Recurse -Force }
}
# uninstall.exe removes itself from a helper that runs after it exits: wait for that, not for a fixed time
$watch = [Diagnostics.Stopwatch]::StartNew()
while ((Test-Path -LiteralPath (Join-Path $installRoot 'uninstall.exe')) -and $watch.Elapsed.TotalSeconds -lt 60) { Start-Sleep -Milliseconds 250 }
foreach ($path in @($sentinels) + @($protected)) {
    if (-not (Test-Path -LiteralPath $path) -or (Get-FileHash -LiteralPath $path).Hash -cne $before[$path]) { throw "Preservation check failed: $path" }
}
foreach ($name in $realRegistrations.Keys) { if ((Get-Registration $name) -cne $realRegistrations[$name]) { throw "The real registration $name changed." } }
foreach ($name in @('versions', 'launcher', 'ARK.cmd', 'current.json', 'update.lock', 'ark-setup.json', 'uninstall-notices.txt', 'uninstall.exe')) {
    if (Test-Path -LiteralPath (Join-Path $installRoot $name)) { throw "Uninstall left $name behind." }
}
if (Test-Path -LiteralPath $registry) { throw 'Uninstall left the isolated registration behind.' }
[IO.File]::WriteAllText((Join-Path $fixture 'result.txt'), "PASS: remembered path with spaces; Inno registration replaced; payload/licenses/.NET notices; reinstall; uninstall; preserved drawings/settings/live pointer/shortcuts/real registrations.`r`nUpgrade/rollback exercised: $([bool]$PreviousInstallerPath)`r`nPage renderer and layout checks exercised: $([bool]$CaptureWizard)`r`nPNG generation skipped (layout only): $([bool]$LayoutOnly)`r`nInspect the PNGs before declaring visual acceptance.`r`n")
Write-Output "Installer smoke passed: $fixture"
