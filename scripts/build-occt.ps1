param(
    [ValidateRange(1, 16)][int]$Jobs = 4,
    [switch]$ConfigureOnly,
    [switch]$AllowCandidatePatchset,
    [ValidateSet('MSVC', 'LLVMMinGW')][string]$Toolchain = 'MSVC',
    [string]$CompilerDirectory
)

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$deps = Join-Path $root 'out/deps/occt'
$patchset = 'ark-offset-001'
$patchHelper = Join-Path $PSScriptRoot 'ApplyOcctPatch.cmake'
$cache = Join-Path $deps 'cache'
$archive = Join-Path $cache 'OCCT-V8_0_1.zip'
$source = Join-Path $deps "source-$patchset"
$toolchainSuffix = if ($Toolchain -eq 'LLVMMinGW') { 'mingw' } else { 'msvc' }
$build = Join-Path $deps "build-$toolchainSuffix-$patchset"
$install = Join-Path $deps "install-$toolchainSuffix-$patchset"
$sourceStamp = Join-Path $source '.ark-source-sha256'
$expectedSha256 = '7C033D917EE8F040C0512D289DCC5F02C148889D5BAC17C3E25639ACCB44F0DA'
$archiveUrl = 'https://github.com/Open-Cascade-SAS/OCCT/archive/refs/tags/V8_0_1.zip'
& cmake -DARK_OCCT_MODE=BUILD_POLICY "-DARK_OCCT_EXPECTED_PATCHSET=$patchset" "-DARK_OCCT_EXPECTED_ARCHIVE_SHA256=$($expectedSha256.ToLowerInvariant())" "-DARK_OCCT_ALLOW_CANDIDATE=$([bool]$AllowCandidatePatchset)" -P $patchHelper
if($LASTEXITCODE -ne 0){throw 'OCCT patchset build policy rejected this preparation'}

New-Item -ItemType Directory -Path $cache -Force | Out-Null
if (-not (Test-Path -LiteralPath $archive)) {
    & curl.exe -fL --retry 3 -o $archive $archiveUrl
    if ($LASTEXITCODE -ne 0) { throw 'OCCT source download failed' }
}
$sha256 = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
if ($sha256 -ne $expectedSha256) {
    throw "OCCT archive SHA-256 mismatch: $sha256"
}
if (-not (Test-Path -LiteralPath $source)) {
    $stage = Join-Path $deps ('.source-' + [Guid]::NewGuid().ToString('N'))
    [IO.Directory]::CreateDirectory($stage) | Out-Null
    & tar.exe -xf $archive -C $stage
    if ($LASTEXITCODE -ne 0) { throw 'OCCT source extraction failed' }
    $extracted = Join-Path $stage 'OCCT-8_0_1'
    if (-not (Test-Path -LiteralPath (Join-Path $extracted 'src/MODULES.cmake'))) {
        throw 'OCCT source tree is incomplete'
    }
    $ownedRoot=[IO.Path]::GetFullPath($deps).TrimEnd('\','/')+[IO.Path]::DirectorySeparatorChar
    foreach($owned in @($extracted,$source)){if(-not [IO.Path]::GetFullPath($owned).StartsWith($ownedRoot,[StringComparison]::OrdinalIgnoreCase)){throw 'OCCT source preparation escaped its dependency directory'}}
    Move-Item -LiteralPath $extracted -Destination $source
    Set-Content -LiteralPath $sourceStamp -Value $sha256 -NoNewline
}
if (-not (Test-Path -LiteralPath $sourceStamp) -or (Get-Content -LiteralPath $sourceStamp -Raw).Trim() -ne $sha256) { throw 'Separate OCCT source archive stamp differs; preserve it and choose a fresh dependency directory.' }
& cmake "-DARK_OCCT_SOURCE=$source" -P $patchHelper
if ($LASTEXITCODE -ne 0) { throw 'Exact OCCT patch preparation failed' }

$configure = @(
    '-S', $source, '-B', $build,
    "-DINSTALL_DIR=$install",
    '-DCMAKE_EXPORT_NO_PACKAGE_REGISTRY=ON',
    '-DBUILD_MODULE_FoundationClasses=ON',
    '-DBUILD_MODULE_ModelingData=ON',
    '-DBUILD_MODULE_ModelingAlgorithms=ON',
    '-DBUILD_MODULE_ApplicationFramework=OFF',
    '-DBUILD_MODULE_Visualization=OFF',
    '-DBUILD_MODULE_DataExchange=OFF',
    '-DBUILD_MODULE_Draw=OFF',
    '-DBUILD_ADDITIONAL_TOOLKITS=TKDESTEP;TKDEIGES',
    '-DBUILD_LIBRARY_TYPE=Shared',
    '-DBUILD_USE_PCH=ON',
    '-DBUILD_USE_VCPKG=OFF',
    '-DUSE_FREETYPE=OFF',
    '-DUSE_OPENGL=OFF',
    '-DUSE_TK=OFF',
    '-DUSE_TBB=OFF',
    '-DUSE_RAPIDJSON=OFF',
    '-DINSTALL_DOC_Overview=OFF',
    '-DINSTALL_TEST_CASES=OFF'
)
if ($Toolchain -eq 'LLVMMinGW') {
    if (-not $CompilerDirectory) { throw 'LLVMMinGW requires -CompilerDirectory pointing to the LLVM-MinGW bin directory' }
    foreach ($tool in @('clang.exe', 'clang++.exe', 'mingw32-make.exe')) {
        if (-not (Test-Path -LiteralPath (Join-Path $CompilerDirectory $tool) -PathType Leaf)) { throw "Missing LLVM-MinGW tool: $tool" }
    }
    $CompilerDirectory = (Resolve-Path -LiteralPath $CompilerDirectory).Path.Replace('\','/')
    $env:PATH = "$CompilerDirectory;$env:PATH"
    $configure += @('-G', 'MinGW Makefiles', '-DCMAKE_BUILD_TYPE=Release',
        "-DCMAKE_C_COMPILER=$CompilerDirectory/clang.exe",
        "-DCMAKE_CXX_COMPILER=$CompilerDirectory/clang++.exe",
        "-DCMAKE_MAKE_PROGRAM=$CompilerDirectory/mingw32-make.exe")
} else {
    $configure += @('-G', 'Visual Studio 17 2022', '-A', 'x64')
}
& cmake @configure
if ($LASTEXITCODE -ne 0) { throw 'OCCT configuration failed' }
if ($ConfigureOnly) { return }

& cmake --build $build --config Release --parallel $Jobs
if ($LASTEXITCODE -ne 0) { throw 'OCCT build failed' }
& cmake --install $build --config Release
if ($LASTEXITCODE -ne 0) { throw 'OCCT install failed' }

$config = Get-ChildItem -LiteralPath $install -Filter OpenCASCADEConfig.cmake -File -Recurse |
    Select-Object -First 1
$libraryPattern = if ($Toolchain -eq 'LLVMMinGW') { 'libTKDESTEP.dll.a' } else { 'TKDESTEP.lib' }
$stepLib = Get-ChildItem -LiteralPath $install -Filter $libraryPattern -File -Recurse |
    Select-Object -First 1
if (-not $config -or -not $stepLib) { throw 'Installed OCCT SDK lacks CMake config or STEP library' }
$igesPattern = if ($Toolchain -eq 'LLVMMinGW') { 'libTKDEIGES.dll.a' } else { 'TKDEIGES.lib' }
$igesLib = Get-ChildItem -LiteralPath $install -Filter $igesPattern -File -Recurse | Select-Object -First 1
if (-not $igesLib) { throw 'Installed OCCT SDK lacks IGES library' }
$licenses=Join-Path $install 'licenses';[IO.Directory]::CreateDirectory($licenses)|Out-Null
foreach($notice in @('LICENSE_LGPL_21.txt','OCCT_LGPL_EXCEPTION.txt')){Copy-Item -LiteralPath (Join-Path $source $notice) -Destination $licenses}
Copy-Item -LiteralPath (Join-Path $source '.ark-occt-patchset.json') -Destination $install
& cmake -DARK_OCCT_MODE=RECORD_BUILD -DARK_OCCT_BUILD_KIND=clean-source-rebuild "-DARK_OCCT_ALLOW_CANDIDATE=$([bool]$AllowCandidatePatchset)" "-DARK_OCCT_INSTALL=$install" -P $patchHelper
if($LASTEXITCODE -ne 0){throw 'OCCT complete shared-library build inventory failed'}
& cmake -DARK_OCCT_MODE=NOTICE "-DARK_OCCT_ALLOW_CANDIDATE=$([bool]$AllowCandidatePatchset)" "-DARK_OCCT_INSTALL=$install" "-DARK_OCCT_NOTICE=$(Join-Path $licenses 'OCCT-ARK-patches.txt')" -P $patchHelper
if($LASTEXITCODE -ne 0){throw 'OCCT patch notice generation failed'}
Write-Host "OCCT_DIR=$($config.DirectoryName)"
Write-Host "STEP_LIBRARY=$($stepLib.FullName)"
