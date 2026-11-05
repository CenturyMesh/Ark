<#
Builds ARK's Windows setup (installer/ArkSetup, WPF on .NET 10) around a reviewed portable package:
publishes ArkSetup self-contained single-file, then appends the unchanged package ZIP and a 24-byte footer
("ARKPAYLD" + int64 offset + int64 length, little endian). Output: ARK-<code>-windows-x64-setup.exe.

-TestIsolationId builds a private test setup (-test-<id> suffix, its own uninstall key, no shortcuts).
-SelfTest (private builds only) adds the hidden page renderer used for screenshots and layout checks.
Requires the .NET 10 SDK. The setup only installs and uninstalls; ARK updates itself in the app.
#>
param(
    [Parameter(Mandatory=$true)][Alias('PackageZip')][string]$PackagePath,
    [string]$OutputDirectory = 'out/releases',
    [switch]$UseRuntimeIdentity,
    [ValidatePattern('^[a-zA-Z0-9-]{1,48}$')][string]$TestIsolationId,
    [switch]$SelfTest,
    [string]$Dotnet = 'dotnet'
)
$ErrorActionPreference = 'Stop'
$repository = Split-Path $PSScriptRoot -Parent
if ($SelfTest -and -not $TestIsolationId) { throw '-SelfTest requires -TestIsolationId to protect the installed ARK registration.' }
foreach ($variable in @('PackagePath', 'OutputDirectory')) {
    $value = Get-Variable -Name $variable -ValueOnly
    if (-not [IO.Path]::IsPathRooted($value)) { Set-Variable -Name $variable -Value (Join-Path $repository $value) }
}
$PackagePath = [IO.Path]::GetFullPath($PackagePath)
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
if (-not (Test-Path -LiteralPath $PackagePath -PathType Leaf)) { throw "Package not found: $PackagePath" }
$sdk = & $Dotnet --version 2>$null
if ($LASTEXITCODE -ne 0 -or -not $sdk -or [int]($sdk -split '\.')[0] -lt 10) { throw 'Install the .NET 10 SDK (or supply -Dotnet) to build the ARK setup.' }
Import-Module (Join-Path $repository 'updates/Ark.Update.psm1') -Force
[IO.Directory]::CreateDirectory($OutputDirectory) | Out-Null
$staging = Join-Path $OutputDirectory ('installer-' + [guid]::NewGuid().ToString('N'))
$publishRoot = Join-Path $repository 'out/installer-build'
$publish = Join-Path $publishRoot ('publish-' + [guid]::NewGuid().ToString('N'))
$partial = $null
try {
    # The payload is validated exactly as ARK's updater would expand it.
    Expand-ArkPackage $PackagePath $staging
    $version = Get-ArkRuntimeVersion $staging
    $identity = Read-ArkJson (Join-Path $staging 'app-version.json') 4KB
    if ($UseRuntimeIdentity) {
        $release = [pscustomobject]@{version=$identity.version; code=$identity.iteration_code; sequence=$identity.sequence; phase=$identity.phase}
    } else {
        $release = Read-ArkJson (Join-Path $repository 'release.json') 4KB
        if ($version -cne $release.version -or $identity.iteration_code -cne $release.code) { throw 'Package and source release identities differ. Build the current iteration first, or use -UseRuntimeIdentity to package an existing personal test build.' }
    }
    if ($release.code -cnotmatch '^[0-9]{3}[A-G]$' -or $release.code -cne ('{0:D3}{1}' -f [int]$release.sequence, $release.phase)) { throw 'Invalid release iteration code.' }
    foreach ($name in @('Install-ARK.ps1', 'Install-ARK.cmd', 'GETTING_STARTED.txt', 'updates/Ark.Update.psm1', 'licenses/LICENSE_LGPL_21.txt', 'licenses/OCCT_LGPL_EXCEPTION.txt', 'licenses/THIRD_PARTY_NOTICES.md')) {
        if (-not (Test-Path -LiteralPath (Join-Path $staging $name) -PathType Leaf)) { throw "Installer payload is missing $name." }
    }
    $filename = "ARK-$($release.code)-windows-x64-setup"
    if ($TestIsolationId) {
        # Private smoke tests must not share installed-app registration with ARK.
        $filename += '-test-' + $TestIsolationId
    }
    $installer = Join-Path $OutputDirectory ($filename + '.exe')
    if (Test-Path -LiteralPath $installer) { throw 'Installer already exists. Use a new output directory or release identity.' }

    $properties = @("-p:ArkVersion=$version", "-p:ArkCode=$($release.code)")
    if ($TestIsolationId) { $properties += "-p:ArkTestIsolationId=$TestIsolationId" }
    if ($SelfTest) { $properties += '-p:ArkSetupSelfTest=true' } else { $properties += '-p:ArkSetupSelfTest=false' }
    # Public and self-test builds share intermediates; clear them so a public build never
# reuses a compile made with other properties (for example the self-test screenshot mode).
foreach ($stale in 'obj', 'bin') { $path = Join-Path $publishRoot $stale; if (Test-Path -LiteralPath $path) { Remove-Item -LiteralPath $path -Recurse -Force } }
& $Dotnet publish (Join-Path $repository 'installer/ArkSetup/ArkSetup.csproj') -c Release -o $publish --nologo @properties
    if ($LASTEXITCODE -ne 0) { throw "dotnet publish failed ($LASTEXITCODE)." }
    $stub = Join-Path $publish 'ARK-Setup.exe'
    if (-not (Test-Path -LiteralPath $stub -PathType Leaf)) { throw "dotnet publish did not produce $stub." }

    # stub + package + footer, written beside the target and moved into place only when complete
    $partial = $installer + '.partial'
    Copy-Item -LiteralPath $stub -Destination $partial -Force
    $payloadLength = (Get-Item -LiteralPath $PackagePath).Length
    $output = [IO.File]::Open($partial, [IO.FileMode]::Append, [IO.FileAccess]::Write)
    try {
        $offset = [int64]$output.Length
        $source = [IO.File]::OpenRead($PackagePath)
        try { $source.CopyTo($output, 1MB) } finally { $source.Dispose() }
        $footer = New-Object byte[] 24
        [Text.Encoding]::ASCII.GetBytes('ARKPAYLD').CopyTo($footer, 0)
        [BitConverter]::GetBytes([int64]$offset).CopyTo($footer, 8)
        [BitConverter]::GetBytes([int64]$payloadLength).CopyTo($footer, 16)
        $output.Write($footer, 0, 24)
    } finally { $output.Dispose() }
    $check = [IO.File]::OpenRead($partial)
    try {
        $tail = New-Object byte[] 24
        [void]$check.Seek(-24, [IO.SeekOrigin]::End)
        if ($check.Read($tail, 0, 24) -ne 24 -or [Text.Encoding]::ASCII.GetString($tail, 0, 8) -cne 'ARKPAYLD' -or [BitConverter]::ToInt64($tail, 8) -ne $offset -or [BitConverter]::ToInt64($tail, 16) -ne $payloadLength -or $offset + $payloadLength + 24 -ne $check.Length) { throw 'Setup payload footer verification failed.' }
    } finally { $check.Dispose() }
    Move-Item -LiteralPath $partial -Destination $installer
    $partial = $null

    $lines = @($PackagePath, $installer) | ForEach-Object { (Get-FileHash -LiteralPath $_ -Algorithm SHA256).Hash.ToLowerInvariant() + '  ' + [IO.Path]::GetFileName($_) }
    [IO.File]::WriteAllLines((Join-Path $OutputDirectory 'SHA256SUMS.txt'), [string[]]$lines, (New-Object Text.UTF8Encoding($false)))
    Write-Output ("Prepared installer: {0} (setup {1:N1} MB + package {2:N1} MB)" -f $installer, ($offset / 1MB), ($payloadLength / 1MB))
} finally {
    if ($partial -and (Test-Path -LiteralPath $partial)) { Remove-Item -LiteralPath $partial -Force }
    if (Test-Path -LiteralPath $publish) { Remove-Item -LiteralPath (Assert-ArkChildPath $publishRoot $publish) -Recurse -Force }
    if (Test-Path -LiteralPath $staging) { Remove-Item -LiteralPath (Assert-ArkChildPath $OutputDirectory $staging) -Recurse -Force }
}
