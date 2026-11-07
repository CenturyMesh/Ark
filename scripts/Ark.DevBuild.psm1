$ErrorActionPreference = 'Stop'

function Get-ArkDevConfigureFingerprint {
    param([Parameter(Mandatory)][string]$Repository)
    $paths = @('CMakeLists.txt','CMakePresets.json','release.json')
    $cmakeDirectory = Join-Path $Repository 'cmake'
    if (Test-Path -LiteralPath $cmakeDirectory) {
        $paths += @(Get-ChildItem -LiteralPath $cmakeDirectory -Recurse -File | ForEach-Object {
            $_.FullName.Substring($Repository.TrimEnd('\','/').Length + 1).Replace('\','/')
        })
    }
    $manifest = foreach ($path in ($paths | Sort-Object -Unique)) {
        $full = Join-Path $Repository $path
        if (Test-Path -LiteralPath $full -PathType Leaf) { "$path $((Get-FileHash -LiteralPath $full -Algorithm SHA256).Hash)" }
        else { "$path missing" }
    }
    $hasher = [Security.Cryptography.SHA256]::Create()
    try { ([BitConverter]::ToString($hasher.ComputeHash([Text.Encoding]::UTF8.GetBytes(($manifest -join "`n"))))).Replace('-','').ToLowerInvariant() }
    finally { $hasher.Dispose() }
}

function Get-ArkDevConfigurePlan {
    param([Parameter(Mandatory)][string]$Repository,
          [Parameter(Mandatory)][string]$BuildDirectory,
          [Parameter(Mandatory)][string]$SdkDirectory,
          [Parameter(Mandatory)][string]$Fingerprint)
    $cachePath = Join-Path $BuildDirectory 'CMakeCache.txt'
    $reasons = @()
    if (-not (Test-Path -LiteralPath $cachePath -PathType Leaf)) { $reasons += 'missing-cache' }
    else {
        $cache = @{}
        foreach ($line in (Get-Content -LiteralPath $cachePath)) {
            if ($line -match '^([^#/:][^:]*):[^=]+=(.*)$') { $cache[$matches[1]] = $matches[2] }
        }
        foreach ($required in @('CMAKE_HOME_DIRECTORY','CMAKE_GENERATOR','CMAKE_GENERATOR_PLATFORM')) {
            if (-not $cache.ContainsKey($required)) { throw "Build cache is incomplete: $required. Choose a fresh build directory." }
        }
        if ([IO.Path]::GetFullPath($cache.CMAKE_HOME_DIRECTORY).TrimEnd('\','/') -ine [IO.Path]::GetFullPath($Repository).TrimEnd('\','/')) {
            throw 'Build cache belongs to another source directory; choose its own build directory.'
        }
        if ($cache.CMAKE_GENERATOR -cne 'Visual Studio 17 2022' -or $cache.CMAKE_GENERATOR_PLATFORM -ine 'x64') {
            throw 'Development build requires the existing cache to use Visual Studio 17 2022 / x64; choose a compatible directory.'
        }
        if ($cache.CMAKE_SUPPRESS_REGENERATION -match '^(ON|TRUE|YES|1)$') {
            throw 'CMAKE_SUPPRESS_REGENERATION prevents dependency-safe incremental builds; disable it before using dev-build.'
        }
        if ($cache.ARK_WITH_OCCT -notmatch '^(ON|TRUE|YES|1)$') { $reasons += 'native-geometry-not-enabled' }
        if (-not $cache.OpenCASCADE_DIR -or [IO.Path]::GetFullPath($cache.OpenCASCADE_DIR).TrimEnd('\','/') -ine [IO.Path]::GetFullPath($SdkDirectory).TrimEnd('\','/')) {
            $reasons += 'native-sdk-changed'
        }
    }
    $stamp = Join-Path $BuildDirectory 'dev-configure-inputs.sha256'
    if (-not (Test-Path -LiteralPath $stamp -PathType Leaf) -or (Get-Content -LiteralPath $stamp -Raw).Trim() -cne $Fingerprint) {
        $reasons += 'configure-inputs-changed'
    }
    [pscustomobject]@{ required=($reasons.Count -gt 0); reasons=$reasons; stamp_path=$stamp }
}

function Resolve-ArkDevTestTargets {
    param([Parameter(Mandatory)]$Discovery,
          [Parameter(Mandatory)][string]$BuildDirectory)
    if (-not $Discovery.tests -or @($Discovery.tests).Count -eq 0) { throw 'Test selection matched no tests; no build was started.' }
    $runtime = [IO.Path]::GetFullPath((Join-Path $BuildDirectory 'Release')).TrimEnd('\','/') + [IO.Path]::DirectorySeparatorChar
    $targets = @()
    foreach ($test in $Discovery.tests) {
        if (@($test.properties | Where-Object { $_.name -ceq 'DISABLED' -and $_.value -match '^(True|ON|YES|1)$' }).Count) {
            throw "Selected test '$($test.name)' is disabled; it cannot qualify this development build."
        }
        $command = @($test.command)
        # CTest omits command entirely when a selected executable is not built
        # yet. Read only CMake's generated, literal add_test entries; never
        # evaluate source CMake or guess a target from the human test name.
        if (-not $test.command) {
            $generated = Join-Path $BuildDirectory 'CTestTestfile.cmake'
            $candidates = @()
            if (Test-Path -LiteralPath $generated -PathType Leaf) {
                foreach ($line in (Get-Content -LiteralPath $generated)) {
                    if ($line -match '^\s*add_test\("([^"\\]*)"\s+"([^"\\]*)"(?:\s|\))' -and $matches[1] -ceq $test.name) {
                        $candidate = $matches[2]
                        if ([IO.Path]::IsPathRooted($candidate) -and [IO.Path]::GetFullPath($candidate).StartsWith($runtime,[StringComparison]::OrdinalIgnoreCase)) {
                            $candidates += [IO.Path]::GetFullPath($candidate)
                        }
                    }
                }
            }
            $candidates = @($candidates | Sort-Object -Unique)
            if ($candidates.Count -eq 1) { $command = @($candidates[0]) }
        }
        if (-not $command.Count -or -not [IO.Path]::IsPathRooted([string]$command[0])) {
            throw "Cannot infer a build target for test '$($test.name)'; build its launcher explicitly and run CTest directly."
        }
        $executable = [IO.Path]::GetFullPath([string]$command[0])
        $target = [IO.Path]::GetFileNameWithoutExtension($executable)
        if (-not $executable.StartsWith($runtime, [StringComparison]::OrdinalIgnoreCase) -or
            [IO.Path]::GetDirectoryName($executable).TrimEnd('\','/') -ine $runtime.TrimEnd('\','/') -or
            [IO.Path]::GetExtension($executable) -ine '.exe' -or
            $target -notmatch '^[A-Za-z0-9_-]+$' -or
            -not (Test-Path -LiteralPath (Join-Path $BuildDirectory "$target.vcxproj") -PathType Leaf)) {
            throw "Cannot infer a CMake executable target for test '$($test.name)' ($executable); build its launcher explicitly and run CTest directly."
        }
        $targets += $target
    }
    @($targets | Sort-Object -Unique)
}

function Write-ArkBytesIfDifferent {
    param([Parameter(Mandatory)][string]$Path,
          [Parameter(Mandatory)][AllowEmptyCollection()][byte[]]$Bytes)
    if (Test-Path -LiteralPath $Path -PathType Leaf) {
        $item = Get-Item -LiteralPath $Path
        if ($item.Length -eq $Bytes.Length) {
            $hasher = [Security.Cryptography.SHA256]::Create()
            try { $digest = ([BitConverter]::ToString($hasher.ComputeHash($Bytes))).Replace('-','') }
            finally { $hasher.Dispose() }
            if ((Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash -ceq $digest) { return }
        }
    }
    [IO.File]::WriteAllBytes($Path, $Bytes)
}

function Get-ArkCompilerPolicy {
    param([ValidateRange(1,16)][int]$CompilerJobs = 4, [AllowNull()][string]$CompilerOptions)
    $overrides = @([regex]::Matches($CompilerOptions, '(?i)(?:^|\s)"?[/\-]MP(?<count>\d*)"?(?=\s|$)'))
    $effective = $CompilerJobs
    $applied = $CompilerOptions
    if ($overrides.Count) {
        $last = $overrides[$overrides.Count - 1].Groups['count'].Value
        $effective = if ($last) { $last } else { 'compiler-default' }
    } else { $applied = ($CompilerOptions + " /MP$CompilerJobs").Trim() }
    [pscustomobject]@{
        requested_jobs=$CompilerJobs
        effective_jobs=$effective
        supplied_override=@($overrides | ForEach-Object {$_.Value.Trim()})
        injected=(-not $overrides.Count)
        applied_options=$applied
    }
}

function Invoke-ArkCompilerEnvironment {
    param([Parameter(Mandatory)]$Policy, [Parameter(Mandatory)][scriptblock]$Action)
    $hadOptions = Test-Path Env:CL
    $oldOptions = $env:CL
    try { $env:CL = $Policy.applied_options; & $Action }
    finally {
        if ($hadOptions) { $env:CL = $oldOptions }
        else { Remove-Item Env:CL -ErrorAction SilentlyContinue }
    }
}

Export-ModuleMember -Function Get-ArkDevConfigureFingerprint,Get-ArkDevConfigurePlan,Resolve-ArkDevTestTargets,Write-ArkBytesIfDifferent,Get-ArkCompilerPolicy,Invoke-ArkCompilerEnvironment
