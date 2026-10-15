param(
    [string]$RuntimeDirectory = 'out/native-release/Release',
    [string]$OutputDirectory = 'out/releases',
    [string]$MsvcRuntimeDirectory,
    [string]$CompilerDirectory,
    [string]$OcctSourceDirectory = 'out/deps/occt/cache/OCCT-8_0_1',
    [switch]$SkipUpdateManifest
)
$ErrorActionPreference = 'Stop'
$repository = Split-Path $PSScriptRoot -Parent
foreach ($variable in @('RuntimeDirectory', 'OutputDirectory', 'OcctSourceDirectory')) {
    $value = Get-Variable -Name $variable -ValueOnly
    if (-not [IO.Path]::IsPathRooted($value)) { Set-Variable -Name $variable -Value (Join-Path $repository $value) }
}
Import-Module (Join-Path $repository 'updates/Ark.Update.psm1') -Force
Import-Module (Join-Path $PSScriptRoot 'Ark.ReleaseEvidence.psm1') -Force
$version = Get-ArkRuntimeVersion $RuntimeDirectory
$llvmRuntime = Test-Path -LiteralPath (Join-Path $RuntimeDirectory 'libc++.dll') -PathType Leaf
if ($llvmRuntime) {
    if (-not $CompilerDirectory) { throw 'Supply -CompilerDirectory for this LLVM-MinGW runtime so its matching redistribution notices are preserved.' }
    if (-not [IO.Path]::IsPathRooted($CompilerDirectory)) { $CompilerDirectory = Join-Path $repository $CompilerDirectory }
    if (-not (Test-Path -LiteralPath (Join-Path $CompilerDirectory 'LICENSE.TXT') -PathType Leaf)) { throw 'LLVM-MinGW compiler license is missing.' }
    $mingwNotices = @(Get-ChildItem -LiteralPath (Join-Path $CompilerDirectory 'x86_64-w64-mingw32/share/mingw32') -Filter 'COPYING*' -File)
    if (-not $mingwNotices.Count) { throw 'The x64 MinGW runtime redistribution notices are missing.' }
} elseif (-not $MsvcRuntimeDirectory) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    if (Test-Path -LiteralPath $vswhere) {
        $visualStudio = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        $redistributables = @(Get-ChildItem -Path (Join-Path $visualStudio 'VC/Redist/MSVC/*/x64/Microsoft.VC*.CRT') -Directory | Sort-Object FullName -Descending)
        if ($redistributables.Count) { $MsvcRuntimeDirectory = $redistributables[0].FullName }
    }
}
if (-not $llvmRuntime -and -not $MsvcRuntimeDirectory) { throw 'Supply -MsvcRuntimeDirectory with the licensed x64 Visual C++ app-local runtime from the build toolchain.' }
[IO.Directory]::CreateDirectory($OutputDirectory) | Out-Null
$staging = Assert-ArkChildPath $OutputDirectory (Join-Path $OutputDirectory ('package-' + [guid]::NewGuid().ToString('N')))
[IO.Directory]::CreateDirectory($staging) | Out-Null
try {
    foreach ($name in @('ark-studio.exe', 'ark-cli.exe', 'ark-mcp.exe', 'ark-occt-worker.exe', 'app-version.json')) { Copy-Item -LiteralPath (Join-Path $RuntimeDirectory $name) -Destination $staging }
    if (Test-Path -LiteralPath (Join-Path $RuntimeDirectory 'ark-benchmark.exe')) {
        Copy-Item -LiteralPath (Join-Path $RuntimeDirectory 'ark-benchmark.exe') -Destination $staging
        $benchmarkResources = Join-Path $staging 'benchmarks'
        $syntheticExamples = Join-Path $staging 'examples'
        [IO.Directory]::CreateDirectory($benchmarkResources) | Out-Null
        [IO.Directory]::CreateDirectory($syntheticExamples) | Out-Null
        foreach ($name in @('agent_briefs_v1.json','offline.json','ollama-qwen-q3.json','ollama-qwen-27b.json')) {
            Copy-Item -LiteralPath (Join-Path $repository "benchmarks/$name") -Destination $benchmarkResources
        }
        foreach ($name in @('pneumatic-cylinder.arkops','fourbar-suspension.arkops')) {
            Copy-Item -LiteralPath (Join-Path $repository "examples/$name") -Destination $syntheticExamples
        }
        Copy-Item -LiteralPath (Join-Path $repository 'docs/AGENT_BENCHMARK.md') -Destination (Join-Path $staging 'AGENT_BENCHMARK.md')
    }
    Get-ChildItem -LiteralPath $RuntimeDirectory -Filter '*.dll' -File | ForEach-Object { Assert-ArkNoReparsePoint $RuntimeDirectory $_.FullName; Copy-Item -LiteralPath $_.FullName -Destination $staging }
    if (-not $llvmRuntime) {
        foreach ($name in @('vcruntime140.dll', 'vcruntime140_1.dll', 'msvcp140.dll', 'concrt140.dll')) {
            if (-not (Test-Path -LiteralPath (Join-Path $MsvcRuntimeDirectory $name))) { throw "Visual C++ runtime is missing $name." }
            Copy-Item -LiteralPath (Join-Path $MsvcRuntimeDirectory $name) -Destination $staging -Force
        }
        Get-ChildItem -LiteralPath $MsvcRuntimeDirectory -Filter '*.dll' -File | ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination $staging -Force }
    }
    foreach ($directory in @('assets', 'updates')) {
        $sourceRoot = if ($directory -eq 'updates') { $repository } else { $RuntimeDirectory }
        $source = Join-Path $sourceRoot $directory
        Get-ChildItem -LiteralPath $source -Recurse -Force | ForEach-Object { Assert-ArkNoReparsePoint $sourceRoot $_.FullName }
        Copy-Item -LiteralPath $source -Destination $staging -Recurse
    }
    Copy-Item -LiteralPath (Join-Path $repository 'updates/ARK.cmd') -Destination $staging
    Copy-Item -LiteralPath (Join-Path $repository 'scripts/install-portable.ps1') -Destination (Join-Path $staging 'Install-ARK.ps1')
    Copy-Item -LiteralPath (Join-Path $repository 'packaging/Install-ARK.cmd') -Destination $staging
    Copy-Item -LiteralPath (Join-Path $repository 'packaging/GETTING_STARTED.txt') -Destination $staging
    Copy-Item -LiteralPath (Join-Path $repository 'docs/UPDATES.md') -Destination (Join-Path $staging 'UPDATES.md')
    $contracts = Join-Path $staging 'assets/contracts'
    [IO.Directory]::CreateDirectory($contracts) | Out-Null
    Copy-Item -LiteralPath (Join-Path $repository 'docs/BOM.md') -Destination (Join-Path $contracts 'BOM.md')
    Copy-Item -LiteralPath (Join-Path $repository 'docs/AGENT_ACTIVITY.md') -Destination (Join-Path $contracts 'AGENT_ACTIVITY.md')
    Copy-Item -LiteralPath (Join-Path $repository 'docs/MEASUREMENT.md') -Destination (Join-Path $contracts 'MEASUREMENT.md')
    Copy-Item -LiteralPath (Join-Path $repository 'docs/schemas/ark-bom-v1.schema.json') -Destination $contracts
    $licenses = Join-Path $staging 'licenses'
    [IO.Directory]::CreateDirectory($licenses) | Out-Null
    if ($llvmRuntime) {
        Copy-Item -LiteralPath (Join-Path $CompilerDirectory 'LICENSE.TXT') -Destination (Join-Path $licenses 'LLVM-Apache-exception.txt')
        foreach ($notice in $mingwNotices) { $noticeName = if ($notice.Extension -eq '.txt') { $notice.Name } else { $notice.Name + '.txt' }; Copy-Item -LiteralPath $notice.FullName -Destination (Join-Path $licenses $noticeName) }
    }
    Copy-Item -LiteralPath (Join-Path $repository 'LICENSE') -Destination $staging
    Copy-Item -LiteralPath (Join-Path $repository 'NOTICE') -Destination $staging
    # The versioned local installer retains licenses/ as a unit. Keep ARK's
    # own grant and attribution there as well as at the archive root.
    Copy-Item -LiteralPath (Join-Path $repository 'LICENSE') -Destination (Join-Path $licenses 'ARK-Apache-2.0.txt')
    Copy-Item -LiteralPath (Join-Path $repository 'NOTICE') -Destination (Join-Path $licenses 'ARK-NOTICE.txt')
    $packageReadme = @"
ARK $version for Windows x64

Launch ARK.cmd to open Studio through the verified per-user launcher.
Save drawings outside immutable application version directories.

Source and release notes: https://github.com/CenturyMesh/Ark
Handbook: https://github.com/CenturyMesh/Ark/blob/main/docs/HANDBOOK.md
Agents and accounts: https://github.com/CenturyMesh/Ark/blob/main/docs/CONNECTING.md
Update details: https://github.com/CenturyMesh/Ark/blob/main/docs/UPDATES.md

ARK source is Apache-2.0; see LICENSE and NOTICE. Dependencies retain their
individual terms in licenses/. SHA-256 verifies archive integrity; it is not
an independent publisher signature. This package has no claimed Authenticode
signature or manufacturing qualification. If marked as a prerelease on GitHub,
it is for manual evaluation and is excluded from the stable automatic updater.
"@
    [IO.File]::WriteAllText((Join-Path $staging 'README.txt'), $packageReadme, (New-Object Text.UTF8Encoding($false)))
    Copy-Item -LiteralPath (Join-Path $repository 'third_party/mapbox/LICENSE') -Destination (Join-Path $licenses 'mapbox-earcut-ISC.txt')
    Copy-Item -LiteralPath (Join-Path $repository 'third_party/stb/LICENSE') -Destination (Join-Path $licenses 'stb-image-LICENSE.txt')
    Copy-Item -LiteralPath (Join-Path $repository 'third_party/stb/README.md') -Destination (Join-Path $licenses 'stb-image-provenance.md')
    Copy-Item -LiteralPath (Join-Path $repository 'third_party/sqlite/README.md') -Destination (Join-Path $licenses 'sqlite-provenance.txt')
    Copy-Item -LiteralPath (Join-Path $repository 'third_party/sqlite/sqlite3.h') -Destination (Join-Path $licenses 'sqlite3.h')
    Copy-Item -LiteralPath (Join-Path $repository 'assets/ui/README.md') -Destination (Join-Path $licenses 'ark-ui-assets.txt')
    Copy-Item -LiteralPath (Join-Path $repository 'docs/THIRD_PARTY_NOTICES.md') -Destination (Join-Path $licenses 'THIRD_PARTY_NOTICES.md')
    foreach ($name in @('LICENSE', 'LICENSE.md', 'LICENSE.txt')) { if (Test-Path -LiteralPath (Join-Path $repository $name)) { Copy-Item -LiteralPath (Join-Path $repository $name) -Destination $licenses } }
    foreach ($file in @('LICENSE_LGPL_21.txt', 'OCCT_LGPL_EXCEPTION.txt')) {
        $sourceNotice = Join-Path $OcctSourceDirectory $file
        if (-not (Test-Path -LiteralPath $sourceNotice -PathType Leaf)) {
            throw "Required OCCT notice is missing: $sourceNotice. Supply the corresponding pinned source directory with -OcctSourceDirectory."
        }
        Copy-Item -LiteralPath $sourceNotice -Destination $licenses
    }
    Get-ArkRuntimeVersion $staging | Out-Null
    $name = "ark-windows-x64-$version.zip"
    $archive = Join-Path $OutputDirectory $name
    if (Test-Path -LiteralPath $archive) { throw "Package already exists: $archive. Use a new output directory or a new version." }
    $evidence = Join-Path $staging 'release-evidence'
    New-ArkReleaseEvidence $repository $staging $evidence -UseRuntimeIdentity:$SkipUpdateManifest | Out-Null
    Compress-ArkPackage $staging $archive
    New-ArkArtifactReceipt $archive $evidence ($archive + '.provenance.json')
    $info = Get-Item -LiteralPath $archive
    if (-not $SkipUpdateManifest) {
        $manifest = @{ schema=1; product='ARK'; version=$version; platform='windows-x64'; package=@{ name=$name; url="https://github.com/CenturyMesh/Ark/releases/download/v$version/$name"; size=$info.Length; sha256=(Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant() } }
        Assert-ArkManifest $manifest $version
        Write-ArkAtomicJson (Join-Path $OutputDirectory 'ark-update.json') $manifest
    }
    Write-Output "Prepared portable package: $archive"
} finally { if (Test-Path -LiteralPath $staging) { Remove-Item -LiteralPath (Assert-ArkChildPath $OutputDirectory $staging) -Recurse -Force } }
