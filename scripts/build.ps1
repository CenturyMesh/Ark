param([switch]$Test, [string]$BuildDirectory = "out/sketch")
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
Push-Location $root
try {
    cmake -S . -B $BuildDirectory -G Ninja -DCMAKE_BUILD_TYPE=Release
    if ($LASTEXITCODE) { throw 'CMake configuration failed' }
    cmake --build $BuildDirectory --parallel 4
    if ($LASTEXITCODE) { throw 'Build failed' }
    if ($Test) {
        ctest --test-dir $BuildDirectory --output-on-failure
        if ($LASTEXITCODE) { throw 'Tests failed' }
    }
} finally { Pop-Location }
