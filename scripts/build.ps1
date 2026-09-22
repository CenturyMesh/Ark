param([switch]$Test)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
Push-Location $root
try {
    cmake -S . -B out/build -G Ninja -DCMAKE_BUILD_TYPE=Release
    if ($LASTEXITCODE) { throw 'CMake configuration failed' }
    cmake --build out/build --parallel 4
    if ($LASTEXITCODE) { throw 'Build failed' }
    if ($Test) {
        ctest --test-dir out/build --output-on-failure
        if ($LASTEXITCODE) { throw 'Tests failed' }
    }
} finally { Pop-Location }
