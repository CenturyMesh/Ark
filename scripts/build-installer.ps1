param(
    [Parameter(Mandatory=$true)][string]$PackagePath,
    [string]$OutputDirectory = 'out/releases',
    [string]$InnoCompiler
)
$ErrorActionPreference = 'Stop'
$repository = Split-Path $PSScriptRoot -Parent
foreach ($variable in @('PackagePath', 'OutputDirectory')) {
    $value = Get-Variable -Name $variable -ValueOnly
    if (-not [IO.Path]::IsPathRooted($value)) { Set-Variable -Name $variable -Value (Join-Path $repository $value) }
}
if (-not $InnoCompiler) {
    $command = Get-Command ISCC.exe -ErrorAction SilentlyContinue
    if ($command) { $InnoCompiler = $command.Source }
    else { $InnoCompiler = Join-Path ${env:ProgramFiles(x86)} 'Inno Setup 6/ISCC.exe' }
}
if (-not (Test-Path -LiteralPath $InnoCompiler -PathType Leaf)) { throw 'Install Inno Setup 6.3+ or supply -InnoCompiler with the path to ISCC.exe.' }
Import-Module (Join-Path $repository 'updates/Ark.Update.psm1') -Force
[IO.Directory]::CreateDirectory($OutputDirectory) | Out-Null
$staging = Join-Path $OutputDirectory ('installer-' + [guid]::NewGuid().ToString('N'))
try {
    Expand-ArkPackage $PackagePath $staging
    $version = Get-ArkRuntimeVersion $staging
    $identity = Read-ArkJson (Join-Path $staging 'app-version.json') 4KB
    $release = Read-ArkJson (Join-Path $repository 'release.json') 4KB
    if ($version -cne $release.version -or $identity.iteration_code -cne $release.code) { throw 'Package and source release identities differ. Build the current iteration first.' }
    if ($release.code -cnotmatch '^[0-9]{3}[A-G]$' -or $release.code -cne ('{0:D3}{1}' -f [int]$release.sequence, $release.phase)) { throw 'Invalid release iteration code.' }
    foreach ($name in @('Install-ARK.ps1', 'Install-ARK.cmd', 'GETTING_STARTED.txt', 'licenses/LICENSE_LGPL_21.txt', 'licenses/OCCT_LGPL_EXCEPTION.txt')) {
        if (-not (Test-Path -LiteralPath (Join-Path $staging $name) -PathType Leaf)) { throw "Installer payload is missing $name." }
    }
    $installer = Join-Path $OutputDirectory "ARK-$($release.code)-windows-x64-setup.exe"
    if (Test-Path -LiteralPath $installer) { throw 'Installer already exists. Use a new output directory or release identity.' }
    & $InnoCompiler "/DRuntimeDirectory=$staging" "/DOutputDirectory=$OutputDirectory" "/DAppVersion=$version" "/DIterationCode=$($release.code)" (Join-Path $repository 'packaging/ark.iss')
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $installer)) { throw 'Inno Setup compilation failed.' }
    $lines = @($PackagePath, $installer) | ForEach-Object { (Get-FileHash -LiteralPath $_ -Algorithm SHA256).Hash.ToLowerInvariant() + '  ' + [IO.Path]::GetFileName($_) }
    [IO.File]::WriteAllLines((Join-Path $OutputDirectory 'SHA256SUMS.txt'), [string[]]$lines, (New-Object Text.UTF8Encoding($false)))
    Write-Output "Prepared installer: $installer"
} finally {
    if (Test-Path -LiteralPath $staging) { Remove-Item -LiteralPath (Assert-ArkChildPath $OutputDirectory $staging) -Recurse -Force }
}
