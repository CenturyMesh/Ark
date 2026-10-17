param(
    [Parameter(Mandatory=$true)][string]$InstallerPath,
    [string]$PreviousInstallerPath,
    [Parameter(Mandatory=$true)][string]$OutputDirectory,
    [switch]$CaptureWizard,
    [switch]$LayoutOnly
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$repository = Split-Path $PSScriptRoot -Parent
if ($LayoutOnly -and -not $CaptureWizard) { throw '-LayoutOnly requires -CaptureWizard for the private control harness.' }
Import-Module (Join-Path $repository 'updates/Ark.Update.psm1') -Force
$match = [regex]::Match([IO.Path]::GetFileName($InstallerPath), '-test-([a-zA-Z0-9-]{1,48})\.exe$')
if (-not $match.Success) { throw 'Smoke tests require an installer built with -TestIsolationId.' }
$testId = $match.Groups[1].Value
if ($PreviousInstallerPath -and [IO.Path]::GetFileName($PreviousInstallerPath) -notmatch ('-test-' + [regex]::Escape($testId) + '\.exe$')) { throw 'Both installers must use the same isolated test identity.' }
$fixture = [IO.Path]::GetFullPath((Join-Path $OutputDirectory ('smoke-' + [guid]::NewGuid().ToString('N'))))
$installRoot = Join-Path $fixture 'Chosen folder with spaces'
[IO.Directory]::CreateDirectory($installRoot) | Out-Null
$drawing = Join-Path $installRoot 'my-drawing.ark'
$settings = Join-Path $fixture 'user-settings.json'
[IO.File]::WriteAllText($drawing, 'Synthetic drawing preservation sentinel - never a user document')
[IO.File]::WriteAllText($settings, '{"synthetic":"preserve"}')
$sentinels = @($drawing, $settings)
$desktopShortcut = Join-Path ([Environment]::GetFolderPath('Desktop')) 'ARK.lnk'
$livePointer = Join-Path $env:LOCALAPPDATA 'ARK/app/current.json'
$protected = @($desktopShortcut, $livePointer) | Where-Object { Test-Path -LiteralPath $_ }
$before = @{}
foreach ($path in @($sentinels) + @($protected)) { $before[$path] = (Get-FileHash -LiteralPath $path).Hash }
function Invoke-PrivateSetup([string]$Executable, [string[]]$Options) {
    $process = Start-Process -FilePath $Executable -ArgumentList $Options -WindowStyle Hidden -PassThru
    if (-not $process.WaitForExit(120000)) {
        & taskkill.exe /PID $process.Id /T /F | Out-Null
        throw 'Private installer exceeded its 120-second test timeout.'
    }
    if ($process.ExitCode -ne 0) { throw "Private installer returned $($process.ExitCode)." }
}
$common = @('/SUPPRESSMSGBOXES', '/NORESTART')
$registry = 'HKCU:/Software/Microsoft/Windows/CurrentVersion/Uninstall/MeshSphere.ARK.Test.' + $testId + '_is1'
try {
    $oldState = $null
    if ($PreviousInstallerPath) {
        Invoke-PrivateSetup $PreviousInstallerPath ($common + @('/VERYSILENT', ('/DIR="' + $installRoot + '"'), ('/LOG="' + $fixture + '/previous.log"')))
        $oldState = Get-ArkState $installRoot
    }
    $options = $common + @(('/SAVEINF="' + $fixture + '/choices.inf"'), ('/LOG="' + $fixture + '/install.log"'))
    if (-not $oldState) { $options += '/DIR="' + $installRoot + '"' }
    if ($CaptureWizard) { $options += '/ARKTESTCAPTURE="' + $fixture + '/captures"' }
    else { $options += '/VERYSILENT' }
    if ($LayoutOnly) { $options += '/ARKTESTLAYOUTONLY=1' }
    Invoke-PrivateSetup $InstallerPath $options
    $registered = Get-ItemProperty -LiteralPath $registry
    if ([IO.Path]::GetFullPath($registered.InstallLocation).TrimEnd('\') -ine $installRoot.TrimEnd('\')) { throw 'The chosen/remembered folder was not used.' }
    $state = Get-ArkState $installRoot
    $version = if ($state.pending) { $state.pending } else { $state.current }
    foreach ($name in @('ark-studio.exe','ark-cli.exe','ark-mcp.exe','ark-occt-worker.exe','licenses/ARK-Apache-2.0.txt','licenses/ARK-NOTICE.txt','licenses/LICENSE_LGPL_21.txt','licenses/OCCT_LGPL_EXCEPTION.txt')) {
        if (-not (Test-Path -LiteralPath (Join-Path $installRoot "versions/$version/$name") -PathType Leaf)) { throw "Installed payload is missing $name." }
    }
    $snapshot = [IO.File]::ReadAllText((Join-Path $installRoot 'current.json'))
    Invoke-PrivateSetup $InstallerPath ($common + @('/VERYSILENT', ('/LOG="' + $fixture + '/reinstall.log"')))
    if ([IO.File]::ReadAllText((Join-Path $installRoot 'current.json')) -cne $snapshot) { throw 'Reinstall changed the version selection.' }
    if ($oldState) {
        if ($state.current -cne $oldState.current -or $state.pending -cne $version -or $version -ceq $oldState.current) { throw 'Upgrade did not retain the old runtime and stage the newer one.' }
        $activated = Start-ArkActivation $installRoot
        if ($activated.current -cne $version -or $activated.previous -cne $oldState.current) { throw 'Prepared runtime activation lost rollback custody.' }
        Complete-ArkActivation $installRoot $true | Out-Null
        if ((Undo-ArkUpdate $installRoot) -cne $oldState.current) { throw 'Manual rollback failed.' }
    }
    if ($CaptureWizard) {
        $controls = [IO.File]::ReadAllText((Join-Path $fixture 'captures/controls.txt'))
        if ($controls -notmatch 'welcome: next=&Next' -or $controls -notmatch 'directory: next=&Install' -or $controls -notmatch 'finished: next=&Finish') { throw 'Wizard controls did not follow the welcome/folder/install/finish flow.' }
        foreach ($page in @('welcome','directory','finished') | Where-Object { -not $LayoutOnly }) {
            if (-not (Test-Path -LiteralPath (Join-Path $fixture "captures/$page.bmp"))) { throw "Missing $page capture." }
        }
        $layout = [IO.File]::ReadAllText((Join-Path $fixture 'captures/layout.txt'))
        if (($layout | Select-String -Pattern 'centered=1; no-overlap=1' -AllMatches).Matches.Count -ne 2) { throw 'Welcome/finish mark centering or control bounds failed.' }
    }
} finally {
    $uninstaller = Join-Path $installRoot 'unins000.exe'
    if (Test-Path -LiteralPath $uninstaller) { Invoke-PrivateSetup $uninstaller ($common + @('/VERYSILENT', ('/LOG="' + $fixture + '/uninstall.log"'))) }
}
foreach ($path in @($sentinels) + @($protected)) {
    if (-not (Test-Path -LiteralPath $path) -or (Get-FileHash -LiteralPath $path).Hash -cne $before[$path]) { throw "Preservation check failed: $path" }
}
if (Test-Path -LiteralPath (Join-Path $installRoot 'versions')) { throw 'Uninstall left application runtimes behind.' }
if (Test-Path -LiteralPath $registry) { throw 'Uninstall left the isolated registration behind.' }
[IO.File]::WriteAllText((Join-Path $fixture 'result.txt'), "PASS: custom/remembered path with spaces; payload/licenses; reinstall; uninstall; preserved drawings/settings/live pointer/desktop shortcut.`r`nUpgrade/rollback exercised: $([bool]$PreviousInstallerPath)`r`nWizard control/layout harness exercised: $([bool]$CaptureWizard)`r`nCapture generation skipped (layout only): $([bool]$LayoutOnly)`r`nCapture generation, when enabled, is an attempt; inspect capture-results.txt and images before declaring visual acceptance.`r`n")
Write-Output "Installer smoke passed: $fixture"
