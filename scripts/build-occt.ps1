param(
    [ValidateRange(1, 16)][int]$Jobs = 4,
    [switch]$ConfigureOnly
)

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$deps = Join-Path $root 'out/deps/occt'
$cache = Join-Path $deps 'cache'
$archive = Join-Path $cache 'OCCT-V8_0_1.zip'
$source = Join-Path $cache 'OCCT-8_0_1'
$build = Join-Path $deps 'build-msvc'
$install = Join-Path $deps 'install-msvc'
$sourceStamp = Join-Path $source '.ark-source-sha256'
$expectedSha256 = '7C033D917EE8F040C0512D289DCC5F02C148889D5BAC17C3E25639ACCB44F0DA'
$archiveUrl = 'https://github.com/Open-Cascade-SAS/OCCT/archive/refs/tags/V8_0_1.zip'

New-Item -ItemType Directory -Path $cache -Force | Out-Null
if (-not (Test-Path -LiteralPath $archive)) {
    & curl.exe -fL --retry 3 -o $archive $archiveUrl
    if ($LASTEXITCODE -ne 0) { throw 'OCCT source download failed' }
}
$sha256 = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash
if ($sha256 -ne $expectedSha256) {
    throw "OCCT archive SHA-256 mismatch: $sha256"
}
if (-not (Test-Path -LiteralPath $sourceStamp) -or
    (Get-Content -LiteralPath $sourceStamp -Raw).Trim() -ne $sha256) {
    & tar.exe -xf $archive -C $cache
    if ($LASTEXITCODE -ne 0) { throw 'OCCT source extraction failed' }
    if (-not (Test-Path -LiteralPath (Join-Path $source 'src/MODULES.cmake'))) {
        throw 'OCCT source tree is incomplete'
    }
    Set-Content -LiteralPath $sourceStamp -Value $sha256 -NoNewline
}

$configure = @(
    '-S', $source, '-B', $build, '-G', 'Visual Studio 17 2022', '-A', 'x64',
    "-DINSTALL_DIR=$install",
    '-DBUILD_MODULE_FoundationClasses=ON',
    '-DBUILD_MODULE_ModelingData=ON',
    '-DBUILD_MODULE_ModelingAlgorithms=ON',
    '-DBUILD_MODULE_ApplicationFramework=OFF',
    '-DBUILD_MODULE_Visualization=OFF',
    '-DBUILD_MODULE_DataExchange=OFF',
    '-DBUILD_MODULE_Draw=OFF',
    '-DBUILD_ADDITIONAL_TOOLKITS=TKDESTEP',
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
& cmake @configure
if ($LASTEXITCODE -ne 0) { throw 'OCCT configuration failed' }
if ($ConfigureOnly) { return }

& cmake --build $build --config Release --parallel $Jobs
if ($LASTEXITCODE -ne 0) { throw 'OCCT build failed' }
& cmake --install $build --config Release
if ($LASTEXITCODE -ne 0) { throw 'OCCT install failed' }

$config = Get-ChildItem -LiteralPath $install -Filter OpenCASCADEConfig.cmake -File -Recurse |
    Select-Object -First 1
$stepLib = Get-ChildItem -LiteralPath $install -Filter TKDESTEP.lib -File -Recurse |
    Select-Object -First 1
if (-not $config -or -not $stepLib) { throw 'Installed OCCT SDK lacks CMake config or STEP library' }
Write-Host "OCCT_DIR=$($config.DirectoryName)"
Write-Host "STEP_LIBRARY=$($stepLib.FullName)"
