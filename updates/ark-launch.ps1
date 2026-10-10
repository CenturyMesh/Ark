param(
    [string]$InstallRoot,
    [string]$RuntimeDirectory,
    [string]$Document,
    [string]$ArgumentsBase64,
    [string]$CallerWorkingDirectory,
    [switch]$NoDialogs,
    [Parameter(ValueFromRemainingArguments=$true)][string[]]$StudioArguments
)
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'Ark.Update.psm1') -Force
function Show-ArkLaunchMessage([string]$Message, [string]$Title, [switch]$Failure) {
    if ($NoDialogs) { Write-Warning "$Title`: $Message"; return }
    Add-Type -AssemblyName System.Windows.Forms
    if ($Failure) { [Windows.Forms.MessageBox]::Show($Message, $Title, 'OK', 'Error') | Out-Null }
    else { [Windows.Forms.MessageBox]::Show($Message, $Title) | Out-Null }
}
if (-not $InstallRoot) { $InstallRoot = Get-ArkInstallRoot }
if ($Document) { $Document = (Resolve-Path -LiteralPath $Document).Path }
$lock = $null
$ack = $null
try {
    $lock = Enter-ArkUpdateLock $InstallRoot
    if ($RuntimeDirectory) {
        Initialize-ArkInstall $RuntimeDirectory $InstallRoot | Out-Null
    } elseif (-not (Test-Path -LiteralPath (Join-Path $InstallRoot 'current.json'))) {
        if (-not $RuntimeDirectory) { throw 'ARK is not installed. Supply a packaged RuntimeDirectory.' }
        Initialize-ArkInstall $RuntimeDirectory $InstallRoot | Out-Null
    }
    $state = Start-ArkActivation $InstallRoot
    $runtime = Assert-ArkInstalledRuntime $InstallRoot $state.current
    $workspace = Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'ARK'
    if ($ArgumentsBase64) {
        $decoded = ConvertFrom-ArkLaunchPayload $ArgumentsBase64
        $StudioArguments = @($decoded.arguments)
        $CallerWorkingDirectory = $decoded.workingDirectory
    }
    if ($CallerWorkingDirectory) {
        $caller = (Resolve-Path -LiteralPath $CallerWorkingDirectory).Path
        $versions = [IO.Path]::GetFullPath((Join-Path $InstallRoot 'versions')).TrimEnd('\', '/') + '\'
        $callerRuntime = $caller.StartsWith($versions, [StringComparison]::OrdinalIgnoreCase) -or (Test-Path -LiteralPath (Join-Path $caller 'app-version.json'))
        if ($callerRuntime) {
            # Keep relative file arguments meaningful while new drawings use the persistent workspace.
            for ($index=0; $index -lt $StudioArguments.Count-1; $index++) {
                if ($StudioArguments[$index] -in @('--load','--script','--import-step','--settings-file','--live-endpoint-file')) {
                    $index++
                    if (-not [IO.Path]::IsPathRooted($StudioArguments[$index])) { $StudioArguments[$index] = [IO.Path]::GetFullPath((Join-Path $caller $StudioArguments[$index])) }
                }
            }
        } else { $workspace = $caller }
    }
    [IO.Directory]::CreateDirectory($workspace) | Out-Null
    $arguments = @()
    if ($Document) { $arguments += @('--load', ('"' + $Document + '"')) }
    if ($null -ne $StudioArguments -and $StudioArguments.Count -gt 0) {
        foreach ($argument in $StudioArguments) {
            # Preserve Windows command-line quoting without invoking a shell.
            $arguments += ConvertTo-ArkWindowsArgument $argument
        }
    }
    if ($state.activating) {
        $nonce = [guid]::NewGuid().ToString('N')
        $ack = Assert-ArkChildPath $InstallRoot (Join-Path $InstallRoot ("ack-$nonce.tmp"))
        $savedAck = $env:ARK_UPDATE_ACK_FILE; $savedNonce = $env:ARK_UPDATE_ACK_NONCE; $savedLaunch = $env:ARK_UPDATE_LAUNCHED
        try {
            $env:ARK_UPDATE_ACK_FILE = $ack; $env:ARK_UPDATE_ACK_NONCE = $nonce
            $env:ARK_UPDATE_LAUNCHED = '1'
            $launch = @{ FilePath=(Join-Path $runtime 'ark-studio.exe'); WorkingDirectory=$workspace; WindowStyle='Normal'; PassThru=$true }
            if ($arguments.Count) { $launch.ArgumentList = $arguments }
            $process = Start-Process @launch
        } catch { Complete-ArkActivation $InstallRoot $false | Out-Null; throw }
        finally { $env:ARK_UPDATE_ACK_FILE = $savedAck; $env:ARK_UPDATE_ACK_NONCE = $savedNonce; $env:ARK_UPDATE_LAUNCHED = $savedLaunch }
        $watch = [Diagnostics.Stopwatch]::StartNew()
        $healthy = $false
        while ($watch.Elapsed.TotalSeconds -lt 30) {
            if (Test-Path -LiteralPath $ack) {
                try { if ([IO.File]::ReadAllText($ack) -ceq $nonce) { $healthy = $true; break } }
                catch {
                    # The application can still be writing or closing the acknowledgement.
                    if ($_.Exception -isnot [IO.IOException] -and $_.Exception.InnerException -isnot [IO.IOException]) { throw }
                }
            }
            if ($process.HasExited) { break }
            Start-Sleep -Milliseconds 100
        }
        Complete-ArkActivation $InstallRoot $healthy | Out-Null
        if (-not $healthy) {
            # Never terminate an application which might have opened a user's document.
            if (-not $process.HasExited) { throw 'The new ARK did not confirm startup. The previous version is restored for the next launch. Close the unconfirmed window before relaunching.' }
            $state = Get-ArkState $InstallRoot
            $runtime = Assert-ArkInstalledRuntime $InstallRoot $state.current
            $launch.FilePath = Join-Path $runtime 'ark-studio.exe'
            $savedLaunch = $env:ARK_UPDATE_LAUNCHED
            try { $env:ARK_UPDATE_LAUNCHED = '1'; Start-Process @launch | Out-Null }
            finally { $env:ARK_UPDATE_LAUNCHED = $savedLaunch }
            Show-ArkLaunchMessage 'The update could not start. ARK opened the previous version; your documents were not changed.' 'ARK update recovery'
        }
    } else {
        $launch = @{ FilePath=(Join-Path $runtime 'ark-studio.exe'); WorkingDirectory=$workspace; WindowStyle='Normal' }
        if ($arguments.Count) { $launch.ArgumentList = $arguments }
        $savedLaunch = $env:ARK_UPDATE_LAUNCHED
        try { $env:ARK_UPDATE_LAUNCHED = '1'; Start-Process @launch }
        finally { $env:ARK_UPDATE_LAUNCHED = $savedLaunch }
    }
} catch {
    Show-ArkLaunchMessage $_.Exception.Message 'ARK could not launch' -Failure
    exit 1
} finally {
    if ($ack -and (Test-Path -LiteralPath $ack)) { Remove-Item -LiteralPath $ack -Force }
    if ($lock) { $lock.Dispose() }
}
