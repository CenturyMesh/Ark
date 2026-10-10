param(
    [ValidateSet('Interactive','Check','Rollback')][string]$Action = 'Interactive',
    [string]$RuntimeDirectory = (Split-Path $PSScriptRoot -Parent),
    [string]$InstallRoot
)
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'Ark.Update.psm1') -Force
if (-not $InstallRoot) { $InstallRoot = Get-ArkInstallRoot }
$version = Get-ArkRuntimeVersion $RuntimeDirectory
$runtimeMetadata = Read-ArkJson (Join-Path $RuntimeDirectory 'app-version.json') 4KB
$iterationCode = $runtimeMetadata.PSObject.Properties['iteration_code']
$displayVersion = if ($iterationCode -and $iterationCode.Value -cmatch '^[0-9]{3}[A-G]$') { [string]$iterationCode.Value } else { $version }
if ($Action -eq 'Check') { Get-ArkAvailableUpdate $version | ConvertTo-Json -Depth 8; exit 0 }
if ($Action -eq 'Rollback') { Undo-ArkUpdate $InstallRoot; exit 0 }
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
[Windows.Forms.Application]::EnableVisualStyles()
$form = New-Object Windows.Forms.Form
$form.Text = 'ARK updates'
$form.Size = New-Object Drawing.Size(620, 340)
$form.StartPosition = 'CenterScreen'
$form.FormBorderStyle = 'FixedDialog'
$form.MaximizeBox = $false
$form.Font = New-Object Drawing.Font('Segoe UI', 10)
$heading = New-Object Windows.Forms.Label
$heading.Text = "ARK $displayVersion  |  Windows x64  |  Stable"
$heading.Location = New-Object Drawing.Point(24, 24)
$heading.Size = New-Object Drawing.Size(565, 28)
$heading.Font = New-Object Drawing.Font('Segoe UI', 12, [Drawing.FontStyle]::Bold)
$status = New-Object Windows.Forms.Label
$status.Text = 'Check whether an ARK update is available.'
$status.Location = New-Object Drawing.Point(24, 70)
$status.Size = New-Object Drawing.Size(565, 80)
$detail = New-Object Windows.Forms.Label
$detail.Text = 'Updates download over HTTPS and are verified before staging. Your current window stays open. Save your work and relaunch ARK to activate a prepared update.'
$detail.Location = New-Object Drawing.Point(24, 155)
$detail.Size = New-Object Drawing.Size(565, 65)
$check = New-Object Windows.Forms.Button
$check.Text = 'Check for updates'
$check.Location = New-Object Drawing.Point(24, 235)
$check.Size = New-Object Drawing.Size(155, 36)
$download = New-Object Windows.Forms.Button
$download.Text = 'Download update'
$download.Location = New-Object Drawing.Point(189, 235)
$download.Size = New-Object Drawing.Size(155, 36)
$download.Enabled = $false
$rollback = New-Object Windows.Forms.Button
$rollback.Text = 'Previous version'
$rollback.Location = New-Object Drawing.Point(354, 235)
$rollback.Size = New-Object Drawing.Size(150, 36)
$rollback.Enabled = $false
$script:offer = $null
function Set-ArkDialogBusy([bool]$Busy) {
    $check.Enabled = -not $Busy
    $download.Enabled = -not $Busy -and $null -ne $script:offer
    $rollback.Enabled = $false
    if (-not $Busy -and (Test-Path -LiteralPath (Join-Path $InstallRoot 'current.json'))) {
        $state = Get-ArkState $InstallRoot
        $rollback.Enabled = [bool]$state.previous -and -not [bool]$state.activating
    }
    $form.UseWaitCursor = $Busy
    $form.Refresh()
}
$check.Add_Click({
    Set-ArkDialogBusy $true
    $script:offer = $null
    $status.Text = 'Checking for an ARK update...'; $form.Refresh()
    try {
        $result = Get-ArkAvailableUpdate $version
        $status.Text = $result.message
        if ($result.status -eq 'available') { $script:offer = $result.manifest; $status.Text += "`r`nDownload size: $([math]::Round($result.manifest.package.size / 1MB, 1)) MB." }
    } catch { $status.Text = 'Could not check for updates: ' + $_.Exception.Message }
    finally { Set-ArkDialogBusy $false }
})
$download.Add_Click({
    Set-ArkDialogBusy $true
    $status.Text = 'Downloading and verifying the update. This can take a few minutes...'; $form.Refresh()
    try {
        $prepared = Save-ArkUpdate $script:offer $RuntimeDirectory $InstallRoot
        $script:offer = $null
        $status.Text = "ARK $prepared is ready for the next launch.`r`nSave your work, close ARK and reopen it using your ARK shortcut."
    } catch { $status.Text = 'The update was not prepared: ' + $_.Exception.Message }
    finally { Set-ArkDialogBusy $false }
})
$rollback.Add_Click({
    try { $previous = Undo-ArkUpdate $InstallRoot; $status.Text = "ARK $previous will be used at the next launch. Save and close your current window first." }
    catch { $status.Text = $_.Exception.Message }
    Set-ArkDialogBusy $false
})
$form.Controls.AddRange(@($heading, $status, $detail, $check, $download, $rollback))
Set-ArkDialogBusy $false
[void]$form.ShowDialog()
