param(
    [switch]$Test,
    [switch]$CoreOnly,
    [string]$BuildDirectory,
    [ValidateRange(1, 16)][int]$Jobs = 4,
    [switch]$AllowCandidatePatchset
)

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
if (-not $BuildDirectory) { $BuildDirectory = if ($CoreOnly) { 'out/core' } else { 'out/native-release' } }
if (-not [System.IO.Path]::IsPathRooted($BuildDirectory)) {
    $BuildDirectory = Join-Path $root $BuildDirectory
}

Push-Location $root
try {
    if ($env:OS -eq 'Windows_NT') {
        & (Join-Path $PSScriptRoot 'prepare-memory-embeddings.ps1')
    }
    if ($CoreOnly) {
        if ($env:OS -eq 'Windows_NT') { & cmake -S $root -B $BuildDirectory -G 'Visual Studio 17 2022' -A x64 -DARK_WITH_OCCT=OFF }
        else { & cmake -S $root -B $BuildDirectory -DARK_WITH_OCCT=OFF -DCMAKE_BUILD_TYPE=Release }
    } else {
        if ($env:OS -ne 'Windows_NT') { throw 'The default native build requires Windows and Visual Studio 2022; use -CoreOnly for the portable core.' }
        $sdk = Join-Path $root 'out/deps/occt/install-msvc-ark-offset-001'
        $occt = Join-Path $sdk 'cmake'
        $iges = Join-Path $sdk 'win64/vc14/lib/TKDEIGES.lib'
        if (-not (Test-Path -LiteralPath (Join-Path $occt 'OpenCASCADEConfig.cmake')) -or -not (Test-Path -LiteralPath $iges)) {
            & (Join-Path $PSScriptRoot 'build-occt.ps1') -Jobs $Jobs -AllowCandidatePatchset:$AllowCandidatePatchset
            if ($LASTEXITCODE -ne 0) { throw 'OCCT SDK build failed' }
        }
        & cmake -DARK_OCCT_MODE=VERIFY_INSTALLED "-DARK_OCCT_ALLOW_CANDIDATE=$([bool]$AllowCandidatePatchset)" "-DARK_OCCT_INSTALL=$sdk" -P (Join-Path $PSScriptRoot 'ApplyOcctPatch.cmake')
        if ($LASTEXITCODE -ne 0) { throw 'Patched OCCT SDK stamp differs; no original-SDK fallback is permitted.' }
        & cmake -S $root -B $BuildDirectory -G 'Visual Studio 17 2022' -A x64 "-DOpenCASCADE_DIR=$occt" -DARK_WITH_OCCT=ON
    }
    if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed' }
    & cmake --build $BuildDirectory --config Release --parallel $Jobs
    if ($LASTEXITCODE -ne 0) { throw 'Build failed' }
    if ($Test) {
        & ctest --test-dir $BuildDirectory -C Release --output-on-failure --output-junit (Join-Path $BuildDirectory 'regression-results.xml')
        if ($LASTEXITCODE -ne 0) { throw 'Tests failed' }
    }
} finally {
    Pop-Location
}
