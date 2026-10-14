param(
    [string]$RuntimeDirectory = 'out/native-release/Release',
    [string]$OutputDirectory,
    [string]$MsvcRuntimeDirectory,
    [string]$OcctSourceDirectory = 'out/deps/occt/cache/OCCT-8_0_1',
    [string]$Repository = 'CenturyMesh/Ark',
    [switch]$Installer,
    [string]$InnoCompiler,
    [switch]$Publish
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$root = Split-Path $PSScriptRoot -Parent
if (-not [IO.Path]::IsPathRooted($RuntimeDirectory)) { $RuntimeDirectory = Join-Path $root $RuntimeDirectory }
Import-Module (Join-Path $root 'updates/Ark.Update.psm1') -Force
$version = Get-ArkRuntimeVersion $RuntimeDirectory
$identity = Read-ArkJson (Join-Path $RuntimeDirectory 'app-version.json') 4KB
$code = $identity.iteration_code
if ($code -cnotmatch '^[0-9]{3}[A-G]$') { throw 'The compiled runtime must contain its iteration code in app-version.json.' }
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $root ('out/test-releases/' + $code + '-' + [guid]::NewGuid().ToString('N')) }
elseif (-not [IO.Path]::IsPathRooted($OutputDirectory)) { $OutputDirectory = Join-Path $root $OutputDirectory }

# Copy the existing binaries, never build, rewrite their identity, or run tests.
$packageArguments = @{RuntimeDirectory=$RuntimeDirectory; OutputDirectory=$OutputDirectory; OcctSourceDirectory=$OcctSourceDirectory; SkipUpdateManifest=$true}
if ($MsvcRuntimeDirectory) { $packageArguments.MsvcRuntimeDirectory = $MsvcRuntimeDirectory }
& (Join-Path $PSScriptRoot 'package-release.ps1') @packageArguments
$archive = Join-Path $OutputDirectory "ark-windows-x64-$version.zip"
$assets = @($archive)
if ($Installer) {
    $installerArguments = @{PackagePath=$archive; OutputDirectory=$OutputDirectory; UseRuntimeIdentity=$true}
    if ($InnoCompiler) { $installerArguments.InnoCompiler = $InnoCompiler }
    & (Join-Path $PSScriptRoot 'build-installer.ps1') @installerArguments
    $assets += Join-Path $OutputDirectory "ARK-$code-windows-x64-setup.exe"
}
$checksums = Join-Path $OutputDirectory 'SHA256SUMS.txt'
$lines = $assets | ForEach-Object { (Get-FileHash -LiteralPath $_ -Algorithm SHA256).Hash.ToLowerInvariant() + '  ' + [IO.Path]::GetFileName($_) }
[IO.File]::WriteAllLines($checksums, [string[]]$lines, (New-Object Text.UTF8Encoding($false)))
$assets += $checksums
Write-Output "Personal test package ready: $archive"
if (-not $Publish) { Write-Output 'No upload requested. Add -Publish to attach these files to a GitHub test Release.'; return }

if (-not (Get-Command gh -ErrorAction SilentlyContinue)) { throw 'Install GitHub CLI (gh), run gh auth login, then rerun this command with -Publish.' }
& gh auth status
if ($LASTEXITCODE -ne 0) { throw 'Sign in with gh auth login, then rerun this command.' }
$tag = "personal-v$version"
$existingJson = & gh release list --repo $Repository --limit 100 --json tagName
if ($LASTEXITCODE -ne 0) { throw 'Could not inspect repository releases.' }
$existing = $existingJson | ConvertFrom-Json
if (@($existing | Where-Object tagName -CEQ $tag).Count) { throw "Release $tag already exists. Existing binaries are never replaced; use a new runtime version for another build." }
$notes = @"
ARK $code / runtime $($version): personal test build.

Packaged from existing local executables. This command did not rebuild ARK,
run CTest, or validate production readiness. The runtime identity was preserved.

Extract the complete ZIP and run ARK.cmd, or use Install-ARK.cmd for a shortcut.
If a setup EXE is attached, it provides the same per-user installation.
Runtime DLLs, UI assets and dependency notices are included.
Windows x64 with an OpenGL 3.3 graphics driver is required.
The in-app Internet update feed remains unpublished.
"@
$notesPath = Join-Path $OutputDirectory 'test-release-notes.md'
[IO.File]::WriteAllText($notesPath, $notes, (New-Object Text.UTF8Encoding($false)))
Push-Location $root
try {
    $commit = & git rev-parse HEAD
    if ($LASTEXITCODE -ne 0) { throw 'Run from a Git checkout so the release tag can target its packaging commit.' }
    $arguments = @('release','create',$tag) + $assets + @('--repo',$Repository,'--target',$commit,'--title',"ARK $code - personal test build",'--notes-file',$notesPath,'--prerelease')
    & gh @arguments
    if ($LASTEXITCODE -ne 0) { throw 'Uploading the personal test Release failed.' }
} finally { Pop-Location }
