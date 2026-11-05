param(
    [Parameter(Mandatory=$true)][string]$PackagePath,
    [Parameter(Mandatory=$true)][string]$InstallerPath
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$repository = Split-Path $PSScriptRoot -Parent
Import-Module (Join-Path $repository 'updates/Ark.Update.psm1') -Force
$temporary = Join-Path ([IO.Path]::GetTempPath()) ('ark-distribution-' + [guid]::NewGuid().ToString('N'))
$oldPath = $env:PATH
$oldWorker = $env:ARK_OCCT_WORKER
$process = $null
$started = $false
$registration = $null
$oldInputEncoding = $null
try {
    [IO.Directory]::CreateDirectory($temporary) | Out-Null
    $runtime = Join-Path $temporary 'portable with spaces'
    Expand-ArkPackage ([IO.Path]::GetFullPath($PackagePath)) $runtime
    $version = Get-ArkRuntimeVersion $runtime
    # This process can see Windows platform DLLs and the package, never the build SDK.
    $env:PATH = "$env:SystemRoot\System32;$env:SystemRoot"
    $env:ARK_OCCT_WORKER = $null
    $start = New-Object Diagnostics.ProcessStartInfo
    $start.FileName = Join-Path $runtime 'ark-mcp.exe'
    $start.WorkingDirectory = $temporary
    $start.UseShellExecute = $false
    $start.RedirectStandardInput = $true
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $start.CreateNoWindow = $true
    $process = New-Object Diagnostics.Process
    $process.StartInfo = $start
    # Windows PowerShell writes redirected stdin with the console input encoding,
    # which adds a UTF-8 BOM on code page 65001 (as in scripts/project-memory.ps1).
    try { $oldInputEncoding = [Console]::InputEncoding; [Console]::InputEncoding = New-Object Text.UTF8Encoding($false) } catch { $oldInputEncoding = $null }
    if (-not $process.Start()) { throw 'Packaged MCP could not start.' }
    $started = $true
    $output = $process.StandardOutput.ReadToEndAsync()
    $errors = $process.StandardError.ReadToEndAsync()
    $messages = @(
        @{jsonrpc='2.0'; id=1; method='initialize'; params=@{protocolVersion='2025-11-25'}},
        @{jsonrpc='2.0'; method='notifications/initialized'},
        @{jsonrpc='2.0'; id=2; method='tools/call'; params=@{name='kernel_info'; arguments=@{}}},
        @{jsonrpc='2.0'; id=3; method='tools/call'; params=@{name='doc_apply'; arguments=@{expected_revision=0; commands=@(@{op='feature.add'; type='native_solid'; name='Package box'; p=@{op='box'; width='10'; depth='10'; height='10'}})}}},
        @{jsonrpc='2.0'; id=4; method='tools/call'; params=@{name='export_step'; arguments=@{id=1; path=(Join-Path $temporary 'package-box.step')}}}
    )
    foreach ($message in $messages) { $process.StandardInput.WriteLine(($message | ConvertTo-Json -Depth 12 -Compress)) }
    $process.StandardInput.Close()
    if (-not $process.WaitForExit(60000)) { $process.Kill(); throw 'Packaged native geometry smoke test timed out.' }
    if ($process.ExitCode -ne 0) { throw "Packaged MCP failed: $($errors.GetAwaiter().GetResult())" }
    $replies = @($output.GetAwaiter().GetResult() -split '\r?\n' | Where-Object { $_ } | ForEach-Object { $_ | ConvertFrom-Json })
    if ($replies.Count -ne 4 -or $replies[0].result.serverInfo.version -cne $version) { throw 'Packaged MCP identity or reply count differs.' }
    foreach ($reply in $replies | Select-Object -Skip 1) { if ($reply.PSObject.Properties['error'] -or $reply.result.isError) { throw "Packaged native operation failed: $($reply | ConvertTo-Json -Depth 12)" } }
    if (-not (Test-Path -LiteralPath (Join-Path $temporary 'package-box.step'))) { throw 'Packaged worker did not produce STEP geometry.' }

    $installRoot = Join-Path $temporary 'installed with spaces'
    # --test-isolated keeps this run off the user's real ARK registration and shortcuts, even with a public setup.
    $isolation = 'distribution-' + [guid]::NewGuid().ToString('N').Substring(0, 12)
    $registration = 'HKCU:/Software/Microsoft/Windows/CurrentVersion/Uninstall/MeshSphere.ARK.Test.' + $isolation
    $setup = Start-Process -FilePath ([IO.Path]::GetFullPath($InstallerPath)) -ArgumentList @('--silent', '--dir', ('"' + $installRoot + '"'), '--test-isolated', $isolation) -PassThru -Wait
    if ($setup.ExitCode -ne 0) { throw "Installer failed with exit code $($setup.ExitCode); see $env:TEMP\ARK-Setup.log." }
    $state = Get-ArkState $installRoot
    if ($state.current -cne $version) { throw 'Installer did not select the packaged runtime.' }
    Assert-ArkInstalledRuntime $installRoot $version | Out-Null
    if (-not (Test-Path -LiteralPath (Join-Path $installRoot 'ARK.cmd'))) { throw 'Installer did not prepare the launcher.' }
    $uninstaller = Join-Path $installRoot 'uninstall.exe'
    if (-not (Test-Path -LiteralPath $uninstaller -PathType Leaf) -or -not (Test-Path -LiteralPath $registration)) { throw 'Installer did not register an uninstaller.' }
    $uninstall = Start-Process -FilePath $uninstaller -ArgumentList @('--uninstall', '--quiet', '--test-isolated', $isolation) -PassThru -Wait
    if ($uninstall.ExitCode -ne 0 -or (Test-Path -LiteralPath (Join-Path $installRoot 'versions')) -or (Test-Path -LiteralPath $registration)) { throw 'Uninstaller did not remove the application runtimes and registration.' }
    Start-Sleep -Seconds 2 # uninstall.exe removes itself after it exits
    Write-Output 'Distribution smoke passed: isolated native STEP export, per-user install, receipts, launcher and uninstall.'
} finally {
    if ($process) { if ($started -and -not $process.HasExited) { $process.Kill() }; $process.Dispose() }
    $env:PATH = $oldPath
    $env:ARK_OCCT_WORKER = $oldWorker
    if ($registration -and (Test-Path -LiteralPath $registration)) { Remove-Item -LiteralPath $registration -Recurse -Force }
    if ($oldInputEncoding) { try { [Console]::InputEncoding = $oldInputEncoding } catch { } }
    if (Test-Path -LiteralPath $temporary) { Remove-Item -LiteralPath (Assert-ArkChildPath ([IO.Path]::GetTempPath()) $temporary) -Recurse -Force }
}
