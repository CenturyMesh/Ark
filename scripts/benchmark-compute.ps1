param([string]$McpExe = 'out/native-release/Release/ark-mcp.exe')

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
if (-not [System.IO.Path]::IsPathRooted($McpExe)) {
    $McpExe = Join-Path $root $McpExe
}
$McpExe = (Resolve-Path -LiteralPath $McpExe).Path
$requests = foreach ($count in @(262144, 1048576, 4000000)) {
    @{
        jsonrpc = '2.0'
        id = $count
        method = 'tools/call'
        params = @{ name = 'compute_benchmark'; arguments = @{ vertices = $count } }
    } | ConvertTo-Json -Compress -Depth 8
}

$start = New-Object System.Diagnostics.ProcessStartInfo
$start.FileName = $McpExe
$start.WorkingDirectory = $root
$start.UseShellExecute = $false
$start.CreateNoWindow = $true
$start.RedirectStandardInput = $true
$start.RedirectStandardOutput = $true
$start.RedirectStandardError = $true
$process = New-Object System.Diagnostics.Process
$process.StartInfo = $start
$started = $false
try {
    if (-not $process.Start()) { throw 'Could not start ark-mcp' }
    $started = $true
    $stdout = $process.StandardOutput.ReadToEndAsync()
    $stderr = $process.StandardError.ReadToEndAsync()
    foreach ($request in $requests) { $process.StandardInput.WriteLine($request) }
    $process.StandardInput.Close()
    if (-not $process.WaitForExit(30000)) {
        & taskkill.exe /PID $process.Id /T /F *> $null
        throw 'Compute benchmark exceeded 30 seconds'
    }
    if ($process.ExitCode -ne 0) {
        throw "ark-mcp exited $($process.ExitCode): $($stderr.Result)"
    }
    $results = @($stdout.Result -split "`r?`n" | Where-Object { $_ } | ForEach-Object {
        $reply = $_ | ConvertFrom-Json
        if ($reply.error -or $reply.result.isError) { throw "Benchmark tool failed: $_" }
        $reply.result.structuredContent
    })
    if ($results.Count -ne $requests.Count) { throw 'Benchmark reply count mismatch' }
    @{ results = $results; warmup_excluded = $true; cold_runtime_initialization_reported_separately = $true } |
        ConvertTo-Json -Depth 12
} finally {
    if ($started -and -not $process.HasExited) { & taskkill.exe /PID $process.Id /T /F *> $null }
    $process.Dispose()
}
