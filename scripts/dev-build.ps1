param(
    [ValidateSet('Studio','MCP','CLI','Both','Tests')][string]$Target = 'Both',
    [string]$BuildDirectory = 'out/native-release',
    [ValidateRange(1,16)][int]$Jobs = 4,
    [ValidateRange(1,16)][int]$CompilerJobs = 4,
    [switch]$Verify,
    [string]$TestRegex,
    [switch]$AllowCandidatePatchset
)
$ErrorActionPreference = 'Stop'
$repository = Split-Path $PSScriptRoot -Parent
Import-Module (Join-Path $PSScriptRoot 'Ark.DevBuild.psm1') -Force
if ($Target -eq 'Tests' -and -not $TestRegex) { throw '-Target Tests requires an explicit -TestRegex.' }
if ($TestRegex) { $Verify = $true }
if ($Verify -and -not $TestRegex) { $TestRegex = '^product-tools-030d$' }
if (-not [IO.Path]::IsPathRooted($BuildDirectory)) { $BuildDirectory = Join-Path $repository $BuildDirectory }
$BuildDirectory = [IO.Path]::GetFullPath($BuildDirectory)
[IO.Directory]::CreateDirectory($BuildDirectory) | Out-Null
$stamp = [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ')
$receiptPath = Join-Path $BuildDirectory "dev-build-$stamp.json"
$logPath = Join-Path $BuildDirectory "dev-build-$stamp.log"
$watch = [Diagnostics.Stopwatch]::StartNew()
$receipt = [ordered]@{ schema='ark.dev-build.v2'; started_utc=[DateTime]::UtcNow.ToString('o'); status='building'; stages=@(); artifacts=@(); tests_requested=[bool]$Verify; test_regex=$TestRegex; build_directory=$BuildDirectory; target=$Target; jobs=$Jobs; distribution_qualified=$false }
function Source-Digest {
    # Only code/build inputs. Local designs, profiles and credentials are excluded.
    $paths = @(& git -c core.quotepath=false ls-files --cached --others --exclude-standard -- src include third_party asm cmake assets scripts tests CMakeLists.txt CMakePresets.json release.json)
    if ($LASTEXITCODE) { throw 'Cannot inventory source inputs' }
    $manifest = foreach ($path in ($paths | Sort-Object -Unique)) {
        if (Test-Path -LiteralPath $path -PathType Leaf) { "$path $((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant())" }
    }
    $hasher = [Security.Cryptography.SHA256]::Create()
    try { ([BitConverter]::ToString($hasher.ComputeHash([Text.Encoding]::UTF8.GetBytes(($manifest -join "`n"))))).Replace('-','').ToLowerInvariant() } finally { $hasher.Dispose() }
}
function Stage([string]$Name,[scriptblock]$Command) {
    $timer = [Diagnostics.Stopwatch]::StartNew()
    $code = 0
    try {
        $global:LASTEXITCODE = 0
        & $Command *>> $logPath
        $code = $LASTEXITCODE
    } catch { $code = 1; throw }
    finally {
        $receipt.stages += [ordered]@{name=$Name;milliseconds=$timer.ElapsedMilliseconds;exit_code=$code}
        Write-Host "$Name / $($timer.ElapsedMilliseconds) ms / exit $code"
    }
    if ($code) { throw "$Name failed; inspect $logPath" }
}
Push-Location $repository
try {
    Stage 'pinned-memory-assets' { & (Join-Path $PSScriptRoot 'prepare-memory-embeddings.ps1') }
    $receipt.commit = (& git rev-parse HEAD).Trim()
    $receipt.branch = (& git branch --show-current).Trim()
    $receipt.source_digest = Source-Digest
    $release = Get-Content -LiteralPath release.json -Raw | ConvertFrom-Json
    $receipt.iteration_code = $release.code; $receipt.version = $release.version
    $sdk = Join-Path $repository 'out/deps/occt/install-msvc-ark-offset-001/cmake'
    Stage 'verify-native-sdk' { & cmake -DARK_OCCT_MODE=VERIFY_INSTALLED "-DARK_OCCT_ALLOW_CANDIDATE=$([bool]$AllowCandidatePatchset)" "-DARK_OCCT_INSTALL=$(Split-Path $sdk -Parent)" -P (Join-Path $PSScriptRoot 'ApplyOcctPatch.cmake') }
    $configureFingerprint = Get-ArkDevConfigureFingerprint -Repository $repository
    $configurePlan = Get-ArkDevConfigurePlan -Repository $repository -BuildDirectory $BuildDirectory -SdkDirectory $sdk -Fingerprint $configureFingerprint
    $receipt.configure_reasons = @($configurePlan.reasons)
    if ($configurePlan.required) {
        if (-not (Test-Path -LiteralPath (Join-Path $sdk 'OpenCASCADEConfig.cmake'))) { throw 'Native OCCT SDK is missing. Run scripts/build.ps1 once to prepare it.' }
        Stage 'configure' { & cmake -S $repository -B $BuildDirectory -G 'Visual Studio 17 2022' -A x64 "-DOpenCASCADE_DIR=$sdk" -DARK_WITH_OCCT=ON }
        [IO.File]::WriteAllText($configurePlan.stamp_path, $configureFingerprint + "`n", [Text.UTF8Encoding]::new($false))
    }
    $targets = @(switch ($Target) { Studio {'ark-studio'} MCP {'ark-mcp'} CLI {'ark-cli'} Both {'ark-studio';'ark-mcp'} Tests {} })
    if ($Verify) {
        $timer = [Diagnostics.Stopwatch]::StartNew()
        $discoveryText = & ctest --test-dir $BuildDirectory -C Release -N --show-only=json-v1 -R $TestRegex 2>> $logPath
        $code = $LASTEXITCODE
        $receipt.stages += [ordered]@{name='discover-selected-tests';milliseconds=$timer.ElapsedMilliseconds;exit_code=$code}
        if ($code) { throw "Cannot discover selected tests; inspect $logPath" }
        $discovery = ($discoveryText -join "`n") | ConvertFrom-Json
        $targets += @(Resolve-ArkDevTestTargets -Discovery $discovery -BuildDirectory $BuildDirectory)
        $receipt.selected_tests = @($discovery.tests | ForEach-Object {$_.name})
        Write-Host "Selected $($receipt.selected_tests.Count) tests; building their executable targets and dependencies."
    }
    $targets = @($targets | Sort-Object -Unique)
    $receipt.build_targets = $targets
    $compilerPolicy = Get-ArkCompilerPolicy -CompilerJobs $CompilerJobs -CompilerOptions $env:CL
    $receipt.compiler_parallelism = [ordered]@{ requested_jobs=$compilerPolicy.requested_jobs; effective_jobs=$compilerPolicy.effective_jobs; supplied_override=$compilerPolicy.supplied_override; injected=$compilerPolicy.injected }
    Stage 'incremental-build' { Invoke-ArkCompilerEnvironment -Policy $compilerPolicy -Action { & cmake --build $BuildDirectory --config Release --target @targets --parallel $Jobs } }
    if ($Verify) { Stage 'focused-verification' { & ctest --test-dir $BuildDirectory -C Release -R $TestRegex --no-tests=error --output-on-failure --output-junit (Join-Path $BuildDirectory "dev-tests-$stamp.xml") } }
    if ($Target -ne 'Tests') {
        $identity = Get-Content -LiteralPath (Join-Path $BuildDirectory 'Release/app-version.json') -Raw | ConvertFrom-Json
        if ($identity.iteration_code -cne $release.code -or $identity.version -cne $release.version) { throw 'Built runtime identity differs from release.json' }
    }
    $after = Source-Digest
    if ($after -cne $receipt.source_digest) { throw 'Source changed during build; this attempt cannot identify a stable source/binary pair' }
    foreach ($targetName in $targets) {
        $artifact = Join-Path $BuildDirectory "Release/$targetName.exe"
        if (-not (Test-Path -LiteralPath $artifact -PathType Leaf)) { throw "Built target missing: $targetName" }
        $receipt.artifacts += [ordered]@{name="$targetName.exe";sha256=(Get-FileHash -LiteralPath $artifact -Algorithm SHA256).Hash.ToLowerInvariant();bytes=(Get-Item -LiteralPath $artifact).Length}
    }
    $worker = Join-Path $BuildDirectory 'Release/ark-occt-worker.exe'
    if (Test-Path -LiteralPath $worker -PathType Leaf) { $receipt.artifacts += [ordered]@{name='ark-occt-worker.exe';sha256=(Get-FileHash -LiteralPath $worker -Algorithm SHA256).Hash.ToLowerInvariant();bytes=(Get-Item -LiteralPath $worker).Length} }
    $receipt.status = 'verified-in-scope'
    if (-not $Verify) { $receipt.status = 'built-without-tests' }
} catch { $receipt.status='failed';$receipt.error=$_.Exception.Message;throw
} finally {
    $receipt.elapsed_ms=$watch.ElapsedMilliseconds;$receipt.finished_utc=[DateTime]::UtcNow.ToString('o')
    $receipt | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $receiptPath -Encoding UTF8
    Write-Host "Receipt: $receiptPath"
    Pop-Location
}
