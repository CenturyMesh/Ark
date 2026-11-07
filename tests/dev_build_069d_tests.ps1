$ErrorActionPreference = 'Stop'
$repository = Split-Path $PSScriptRoot -Parent
Import-Module (Join-Path $repository 'scripts/Ark.DevBuild.psm1') -Force
$workspace = Join-Path ([IO.Path]::GetTempPath()) ('ark-dev-build-' + [guid]::NewGuid().ToString('N'))
$checks = 0
function Check([bool]$Value, [string]$Message) {
    if (-not $Value) { throw $Message }
    $script:checks++
}
function Reject([scriptblock]$Call, [string]$Message) {
    $rejected = $false
    try { & $Call | Out-Null } catch { $rejected = $true }
    Check $rejected $Message
}
try {
    $source = Join-Path $workspace 'source'
    $build = Join-Path $workspace 'build'
    $sdk = Join-Path $workspace 'sdk/cmake'
    foreach ($path in @($source,$build,$sdk,(Join-Path $source 'cmake'),(Join-Path $build 'Release'))) { [IO.Directory]::CreateDirectory($path) | Out-Null }
    foreach ($name in @('CMakeLists.txt','CMakePresets.json','release.json')) { [IO.File]::WriteAllText((Join-Path $source $name), $name) }
    $fingerprint = Get-ArkDevConfigureFingerprint -Repository $source
    $missing = Get-ArkDevConfigurePlan -Repository $source -BuildDirectory $build -SdkDirectory $sdk -Fingerprint $fingerprint
    Check ($missing.required -and $missing.reasons -contains 'missing-cache') 'Fresh build did not request configure.'
    $cache = @"
CMAKE_HOME_DIRECTORY:INTERNAL=$source
CMAKE_GENERATOR:INTERNAL=Visual Studio 17 2022
CMAKE_GENERATOR_PLATFORM:INTERNAL=x64
ARK_WITH_OCCT:BOOL=ON
OpenCASCADE_DIR:PATH=$sdk
"@
    $cachePath = Join-Path $build 'CMakeCache.txt'
    [IO.File]::WriteAllText($cachePath, $cache)
    [IO.File]::WriteAllText($missing.stamp_path, $fingerprint)
    $ready = Get-ArkDevConfigurePlan -Repository $source -BuildDirectory $build -SdkDirectory $sdk -Fingerprint $fingerprint
    Check (-not $ready.required) 'Stable compatible cache must remain incremental.'
    [IO.File]::WriteAllText((Join-Path $source 'cmake/input.cmake'), 'new configuration input')
    $changed = Get-ArkDevConfigureFingerprint -Repository $source
    Check ($changed -cne $fingerprint) 'CMake additions must invalidate configuration fingerprint.'
    Check ((Get-ArkDevConfigurePlan -Repository $source -BuildDirectory $build -SdkDirectory $sdk -Fingerprint $changed).required) 'Changed configuration must configure before discovering tests.'
    [IO.File]::WriteAllText((Join-Path $source 'release.json'), 'new identity')
    Check ((Get-ArkDevConfigureFingerprint -Repository $source) -cne $changed) 'Release identity must invalidate configuration fingerprint.'
    foreach ($bad in @($cache.Replace($source, (Join-Path $workspace 'elsewhere')), $cache.Replace('Visual Studio 17 2022','Ninja'), $cache.Replace('INTERNAL=x64','INTERNAL=Win32'), ($cache + "`nCMAKE_SUPPRESS_REGENERATION:BOOL=ON"))) {
        [IO.File]::WriteAllText($cachePath, $bad)
        Reject { Get-ArkDevConfigurePlan -Repository $source -BuildDirectory $build -SdkDirectory $sdk -Fingerprint $fingerprint } 'Foreign source/generator/platform or disabled regeneration accepted.'
    }
    [IO.File]::WriteAllText($cachePath, $cache.Replace('ARK_WITH_OCCT:BOOL=ON','ARK_WITH_OCCT:BOOL=OFF'))
    Check ((Get-ArkDevConfigurePlan -Repository $source -BuildDirectory $build -SdkDirectory $sdk -Fingerprint $fingerprint).reasons -contains 'native-geometry-not-enabled') 'Portable cache silently reused for native build.'
    [IO.File]::WriteAllText($cachePath, $cache.Replace($sdk, (Join-Path $workspace 'old-sdk/cmake')))
    Check ((Get-ArkDevConfigurePlan -Repository $source -BuildDirectory $build -SdkDirectory $sdk -Fingerprint $fingerprint).reasons -contains 'native-sdk-changed') 'Changed SDK silently reused.'

    $testPath = Join-Path $build 'Release/ark-test.exe'
    [IO.File]::WriteAllText((Join-Path $build 'ark-test.vcxproj'), 'synthetic project')
    $selected = [pscustomobject]@{tests=@([pscustomobject]@{name='one';command=@($testPath,'arg')},[pscustomobject]@{name='two';command=@($testPath)})}
    $targets = @(Resolve-ArkDevTestTargets -Discovery $selected -BuildDirectory $build)
    Check ($targets.Count -eq 1 -and $targets[0] -ceq 'ark-test') 'Selected tests did not share the one exact executable target.'
    $disabled = [pscustomobject]@{tests=@([pscustomobject]@{name='disabled';command=@($testPath);properties=@([pscustomobject]@{name='DISABLED';value=$true})})}
    Reject { Resolve-ArkDevTestTargets -Discovery $disabled -BuildDirectory $build } 'Disabled selected test accepted as verification.'
    $generated = Join-Path $build 'CTestTestfile.cmake'
    [IO.File]::WriteAllText($generated, ('add_test("not-built" "' + $testPath.Replace('\','/') + '")'))
    $notBuilt = [pscustomobject]@{tests=@([pscustomobject]@{name='not-built'})}
    Check ((@(Resolve-ArkDevTestTargets -Discovery $notBuilt -BuildDirectory $build))[0] -ceq 'ark-test') 'CTest missing-executable entry did not resolve through its generated literal command.'
    [IO.File]::AppendAllText($generated, ("`n" + 'add_test("not-built" "' + (Join-Path $build 'Release/another.exe').Replace('\','/') + '")'))
    Reject { Resolve-ArkDevTestTargets -Discovery $notBuilt -BuildDirectory $build } 'Conflicting generated test commands silently selected one target.'
    Reject { Resolve-ArkDevTestTargets -Discovery ([pscustomobject]@{tests=@()}) -BuildDirectory $build } 'Empty CTest selection was accepted.'
    foreach ($badCommand in @('powershell.exe',(Join-Path $workspace 'ark-test.exe'),(Join-Path $build 'Release/sub/ark-test.exe'),(Join-Path $build 'Release/unknown.exe'),(Join-Path $build 'Release/ark-test.ps1'))) {
        $badDiscovery = [pscustomobject]@{tests=@([pscustomobject]@{name='bad';command=@($badCommand)})}
        Reject { Resolve-ArkDevTestTargets -Discovery $badDiscovery -BuildDirectory $build } 'Unsupported/outside/unknown launcher target accepted.'
    }
    $stablePath = Join-Path $workspace 'stable.bin'
    $bytes = [byte[]]@(0,1,2,3,255)
    Write-ArkBytesIfDifferent -Path $stablePath -Bytes $bytes
    [IO.File]::SetLastWriteTimeUtc($stablePath,[DateTime]::new(2020,1,1,0,0,0,[DateTimeKind]::Utc))
    $timestamp = (Get-Item -LiteralPath $stablePath).LastWriteTimeUtc.Ticks
    Write-ArkBytesIfDifferent -Path $stablePath -Bytes $bytes
    Check ((Get-Item -LiteralPath $stablePath).LastWriteTimeUtc.Ticks -eq $timestamp) 'Identical pinned bytes were rewritten.'
    Write-ArkBytesIfDifferent -Path $stablePath -Bytes ([byte[]]@(0,1,2,4,255))
    Check ([IO.File]::ReadAllBytes($stablePath)[3] -eq 4) 'Changed same-size bytes were not repaired.'
    Write-ArkBytesIfDifferent -Path $stablePath -Bytes ([byte[]]@())
    Check ((Get-Item -LiteralPath $stablePath).Length -eq 0) 'Empty replacement was not preserved.'
    $policy = Get-ArkCompilerPolicy -CompilerJobs 4 -CompilerOptions '/DKEEP=1'
    Check ($policy.injected -and $policy.effective_jobs -eq 4 -and $policy.applied_options -ceq '/DKEEP=1 /MP4') 'Missing compiler parallelism was not added without losing flags.'
    foreach ($override in @('/MP','/MP2','/mp8','-MP3','"/MP2"')) {
        $options = '/DKEEP=1 ' + $override
        $policy = Get-ArkCompilerPolicy -CompilerJobs 4 -CompilerOptions $options
        Check (-not $policy.injected -and $policy.applied_options -ceq $options -and $policy.supplied_override.Count -eq 1) 'Explicit compiler parallelism was overwritten.'
    }
    Check ((Get-ArkCompilerPolicy -CompilerJobs 2 -CompilerOptions '/DNAME="/MP8"').injected) 'A macro value was mistaken for a compiler parallelism option.'
    $hadOptions = Test-Path Env:CL; $oldOptions = $env:CL
    try {
        $env:CL = '/DORIGINAL=1'
        $policy = Get-ArkCompilerPolicy -CompilerJobs 2 -CompilerOptions $env:CL
        $observed = Invoke-ArkCompilerEnvironment -Policy $policy -Action { $env:CL }
        Check ($observed -ceq '/DORIGINAL=1 /MP2' -and $env:CL -ceq '/DORIGINAL=1') 'Compiler environment did not apply and restore on success.'
        Reject { Invoke-ArkCompilerEnvironment -Policy $policy -Action { throw 'synthetic compiler failure' } } 'Compiler failure was swallowed.'
        Check ($env:CL -ceq '/DORIGINAL=1') 'Compiler environment leaked after failure.'
        Remove-Item Env:CL
        $policy = Get-ArkCompilerPolicy -CompilerJobs 1 -CompilerOptions $null
        $observed = Invoke-ArkCompilerEnvironment -Policy $policy -Action { $env:CL }
        Check ($observed -ceq '/MP1' -and -not (Test-Path Env:CL)) 'Originally absent compiler environment was not restored.'
    } finally {
        if ($hadOptions) { $env:CL = $oldOptions } else { Remove-Item Env:CL -ErrorAction SilentlyContinue }
    }
    foreach ($script in @('scripts/dev-build.ps1','scripts/build.ps1','scripts/prepare-memory-embeddings.ps1')) {
        $tokens = $null; $parseErrors = $null
        [Management.Automation.Language.Parser]::ParseFile((Join-Path $repository $script),[ref]$tokens,[ref]$parseErrors) | Out-Null
        Check ($parseErrors.Count -eq 0) "PowerShell parse failure: $script"
    }
    "Development build contracts passed: $checks checks."
} finally {
    $full = [IO.Path]::GetFullPath($workspace)
    $temporary = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\','/') + [IO.Path]::DirectorySeparatorChar
    if (-not $full.StartsWith($temporary,[StringComparison]::OrdinalIgnoreCase)) { throw 'Unsafe development build test cleanup.' }
    if (Test-Path -LiteralPath $full) { Remove-Item -LiteralPath $full -Recurse -Force }
}
