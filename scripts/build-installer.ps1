<#
Builds ARK's Windows setup (installer/ArkSetup, WPF on .NET 10) around a reviewed portable package:
publishes ArkSetup self-contained single-file with the package's SHA-256 stamped in, then appends the unchanged
package ZIP and a 40-byte footer (scripts/Ark.SetupPayload.psm1, installer/ArkSetup/Core/Payload.cs).
Output: ARK-<code>-windows-x64-setup.exe.

-SignScript <path> signs with Authenticode in a sign-friendly order: the published stub first (it becomes the signed
uninstall.exe), then the whole setup after the append, so the signature also covers the package. The script is
called as & <path> -Path <file> and must sign that file in place. See docs/WINDOWS_INSTALLER.md.

A public setup is a distributable artifact: it needs a clean source tree and a package whose release evidence says it
was built from a clean tree. -AllowDirtyTree builds one anyway, for local tests only.
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
    [string]$SignScript,
    [switch]$AllowDirtyTree,
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
if ($SignScript) {
    $SignScript = [IO.Path]::GetFullPath($(if ([IO.Path]::IsPathRooted($SignScript)) { $SignScript } else { Join-Path (Get-Location) $SignScript }))
    if (-not (Test-Path -LiteralPath $SignScript -PathType Leaf)) { throw "Signing script not found: $SignScript" }
}
$sdk = & $Dotnet --version 2>$null
if ($LASTEXITCODE -ne 0 -or -not $sdk -or [int]($sdk -split '\.')[0] -lt 10) { throw 'Install the .NET 10 SDK (or supply -Dotnet) to build the ARK setup.' }
Import-Module (Join-Path $repository 'updates/Ark.Update.psm1') -Force
Import-Module (Join-Path $PSScriptRoot 'Ark.SetupPayload.psm1') -Force
[IO.Directory]::CreateDirectory($OutputDirectory) | Out-Null
$staging = Join-Path $OutputDirectory ('installer-' + [guid]::NewGuid().ToString('N'))
$publishRoot = Join-Path $repository 'out/installer-build'
$publish = Join-Path $publishRoot ('publish-' + [guid]::NewGuid().ToString('N'))
$partial = $null

function Invoke-ArkSigning([string]$Path, [string]$What) {
    # Authenticode in place by the caller's script; a test build may use an untrusted test certificate, a public one may not.
    & $SignScript -Path $Path
    $signature = Get-AuthenticodeSignature -LiteralPath $Path
    if (-not $signature.SignerCertificate -or [string]$signature.Status -in @('NotSigned', 'HashMismatch', 'NotSupportedFileFormat', 'Incompatible')) { throw "$What is not signed after $SignScript ($($signature.Status))." }
    if (-not $TestIsolationId -and [string]$signature.Status -cne 'Valid') { throw "$What is signed but not trusted here ($($signature.Status): $($signature.StatusMessage)). A public setup needs a trusted code-signing certificate." }
    return $signature.SignerCertificate.Thumbprint
}

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
    if (-not $TestIsolationId) {
        # A public setup is distributable: its installer sources and its package must both come from clean trees.
        $changes = @(& git -C $repository status --porcelain --untracked-files=normal)
        if ($LASTEXITCODE -ne 0) { throw 'Cannot inspect the source tree (git status failed); a public setup needs a clean checkout.' }
        $evidence = Join-Path $staging 'release-evidence/source-inventory.json'
        $packageClean = (Test-Path -LiteralPath $evidence -PathType Leaf) -and ((Read-ArkJson $evidence 64MB).dirty -eq $false)
        $problems = @()
        if ($changes.Count) { $problems += "the source tree has $($changes.Count) uncommitted change(s)" }
        if (-not $packageClean) { $problems += 'the package release evidence does not record a clean source tree' }
        if ($problems.Count -and -not $AllowDirtyTree) { throw ('Refusing a public setup: ' + ($problems -join '; ') + '. Commit and repackage, or pass -AllowDirtyTree for a local test that will not be distributed.') }
        if ($problems.Count) { Write-Warning ('Local test setup only, not for distribution: ' + ($problems -join '; ') + '.') }
    }
    $filename = "ARK-$($release.code)-windows-x64-setup"
    if ($TestIsolationId) {
        # Private smoke tests must not share installed-app registration with ARK.
        $filename += '-test-' + $TestIsolationId
    }
    $installer = Join-Path $OutputDirectory ($filename + '.exe')
    if (Test-Path -LiteralPath $installer) { throw 'Installer already exists. Use a new output directory or release identity.' }
    $packageHash = (Get-FileHash -LiteralPath $PackagePath -Algorithm SHA256).Hash.ToLowerInvariant()

    $properties = @("-p:ArkVersion=$version", "-p:ArkCode=$($release.code)", "-p:ArkPayloadSha256=$packageHash")
    if ($TestIsolationId) { $properties += "-p:ArkTestIsolationId=$TestIsolationId" }
    if ($SelfTest) { $properties += '-p:ArkSetupSelfTest=true' } else { $properties += '-p:ArkSetupSelfTest=false' }
    # Public and self-test builds share intermediates; clear them so a public build never
    # reuses a compile made with other properties (for example the self-test screenshot mode).
    foreach ($stale in 'obj', 'bin') { $path = Join-Path $publishRoot $stale; if (Test-Path -LiteralPath $path) { Remove-Item -LiteralPath $path -Recurse -Force } }
    & $Dotnet publish (Join-Path $repository 'installer/ArkSetup/ArkSetup.csproj') -c Release -o $publish --nologo @properties
    if ($LASTEXITCODE -ne 0) { throw "dotnet publish failed ($LASTEXITCODE)." }
    $stub = Join-Path $publish 'ARK-Setup.exe'
    if (-not (Test-Path -LiteralPath $stub -PathType Leaf)) { throw "dotnet publish did not produce $stub." }

    # 1. the stub, signed on its own: Setup writes exactly these bytes as uninstall.exe
    $signer = $null
    if ($SignScript) { $signer = Invoke-ArkSigning $stub 'The uninstaller stub' }
    $stubLayout = Get-ArkPeLayout $stub
    if (-not $stubLayout) { throw 'The published setup is not a PE file.' }
    $stubHash = (Get-FileHash -LiteralPath $stub -Algorithm SHA256).Hash.ToLowerInvariant()

    # 2. stub + package + footer, written beside the target and moved into place only when complete; the setup's own
    #    security directory starts empty (the stub's certificate table is now just data), so signing appends a new one
    $partial = $installer + '.partial'
    Copy-Item -LiteralPath $stub -Destination $partial -Force
    if ($stubLayout.CertSize -ne 0) { Set-ArkPeField $partial $stubLayout.SecurityDirPos 0; Set-ArkPeField $partial ($stubLayout.SecurityDirPos + 4) 0 }
    $payloadLength = (Get-Item -LiteralPath $PackagePath).Length
    $output = [IO.File]::Open($partial, [IO.FileMode]::Append, [IO.FileAccess]::Write)
    try {
        $offset = [int64]$output.Length
        $source = [IO.File]::OpenRead($PackagePath)
        try { $source.CopyTo($output, 1MB) } finally { $source.Dispose() }
        $footer = New-ArkSetupFooter $offset $payloadLength $stubLayout
        $output.Write($footer, 0, $footer.Length)
    } finally { $output.Dispose() }

    # 3. the whole setup, signed after the append
    if ($SignScript) {
        if ((Invoke-ArkSigning $partial 'The setup') -cne $signer) { throw 'The setup and its uninstaller were signed with different certificates.' }
    }

    # 4. read it back the way Setup will
    $found = Find-ArkSetupPayload $partial
    if (-not $found -or $found.Offset -ne $offset -or $found.Length -ne $payloadLength) { throw 'Setup payload footer verification failed.' }
    if ((Get-ArkSetupPayloadHash $partial $found) -cne $packageHash) { throw 'The package inside the setup differs from the reviewed package.' }
    if ((Get-ArkSetupStubHash $partial $found) -cne $stubHash) { throw 'The uninstall.exe this setup writes would differ from the built (and signed) stub.' }
    Move-Item -LiteralPath $partial -Destination $installer
    $partial = $null

    $lines = @($PackagePath, $installer) | ForEach-Object { (Get-FileHash -LiteralPath $_ -Algorithm SHA256).Hash.ToLowerInvariant() + '  ' + [IO.Path]::GetFileName($_) }
    [IO.File]::WriteAllLines((Join-Path $OutputDirectory 'SHA256SUMS.txt'), [string[]]$lines, (New-Object Text.UTF8Encoding($false)))
    $signed = if ($SignScript) { ', setup and uninstaller signed' } else { ', unsigned' }
    Write-Output ("Prepared installer: {0} (setup {1:N1} MB + package {2:N1} MB{3})" -f $installer, ($offset / 1MB), ($payloadLength / 1MB), $signed)
} finally {
    if ($partial -and (Test-Path -LiteralPath $partial)) { Remove-Item -LiteralPath $partial -Force }
    if (Test-Path -LiteralPath $publish) { Remove-Item -LiteralPath (Assert-ArkChildPath $publishRoot $publish) -Recurse -Force }
    if (Test-Path -LiteralPath $staging) { Remove-Item -LiteralPath (Assert-ArkChildPath $OutputDirectory $staging) -Recurse -Force }
}
