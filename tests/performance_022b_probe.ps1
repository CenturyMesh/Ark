param(
    [Parameter(Mandatory=$true)][string]$McpPath,
    [ValidateRange(1,16)][int]$FeatureThreads=4,
    [ValidateRange(1,64)][int]$NativeThreads=1,
    [ValidateRange(1,64)][int]$CpuThreads=8
)
$ErrorActionPreference='Stop'
$resolved=(Resolve-Path -LiteralPath $McpPath).Path
$start=[Diagnostics.ProcessStartInfo]::new()
$start.FileName=$resolved
$start.UseShellExecute=$false
$start.CreateNoWindow=$true
$start.RedirectStandardInput=$true
$start.RedirectStandardOutput=$true
$start.RedirectStandardError=$true
$start.EnvironmentVariables['ARK_CPU_THREADS']=[string]$CpuThreads
$start.EnvironmentVariables['ARK_FEATURE_THREADS']=[string]$FeatureThreads
$start.EnvironmentVariables['ARK_NATIVE_THREADS']=[string]$NativeThreads
$process=[Diagnostics.Process]::Start($start)
$stderr=$process.StandardError.ReadToEndAsync()
$script:probeId=0
$script:probeRows=[Collections.Generic.List[object]]::new()
function Invoke-Probe([string]$Tool,[hashtable]$Arguments,[string]$Stage){
    $script:probeId++
    $request=@{jsonrpc='2.0';id=$script:probeId;method='tools/call';params=@{name=$Tool;arguments=$Arguments}}
    $watch=[Diagnostics.Stopwatch]::StartNew()
    $process.StandardInput.WriteLine(($request|ConvertTo-Json -Depth 64 -Compress))
    $line=$process.StandardOutput.ReadLine()
    $watch.Stop()
    if(!$line){throw 'MCP exited without a response'}
    $response=$line|ConvertFrom-Json
    $failed=($null -ne $response.error -or $response.result.isError -eq $true)
    $data=$response.result.structuredContent
    $script:probeRows.Add([pscustomobject]@{stage=$Stage;tool=$Tool;failed=$failed;elapsed_ms=$watch.Elapsed.TotalMilliseconds;recompute_ms=$data.recompute_ms;features_evaluated=$data.features_evaluated;features_reused=$data.features_reused;peak_parallel_features=$data.peak_parallel_features;applied_prepared=$data.applied_prepared})
    if($failed){throw "Synthetic probe rejected at $Stage"}
    return $data
}
$failure=$null
try {
    $commands=@(@{op='param.set';name='height';expr=30})
    for($i=0;$i -lt 4;$i++){
        $commands+=@{op='feature.add';type='native_solid';name="Root$i";native=@{op='box'};p=@{width=10;depth=20;height='height'}}
        $commands+=@{op='feature.add';type='native_solid';name="Move$i";native=@{op='transform'};inputs=@("Root$i");p=@{tx=5}}
    }
    $cold=Invoke-Probe 'doc_apply' @{expected_revision=0;commands=$commands} 'cold_apply'
    $revision=$cold.revision
    for($sample=1;$sample -le 3;$sample++){
        $warm=Invoke-Probe 'doc_apply' @{expected_revision=$revision;commands=@(@{op='param.set';name='height';expr=(30+$sample)})} "warm_apply_$sample"
        $revision=$warm.revision
    }
    $preview=Invoke-Probe 'doc_preview' @{expected_revision=$revision;detail='changes';commands=@(@{op='param.set';name='height';expr=34})} 'preview'
    if(!$preview.valid -or !$preview.candidate_cached){throw 'Synthetic preview did not retain a valid candidate'}
    $prepared=Invoke-Probe 'doc_apply' @{expected_revision=$revision;candidate_id=$preview.candidate_id} 'prepared_commit'
    if(!$prepared.applied_prepared -or $prepared.features_evaluated -ne 0){throw 'Prepared commit recomputed geometry'}
    # Inspect only after measured operations: the inspection itself adds one
    # native request. With NativeThreads=1 it cannot change the worker budget.
    $kernel=Invoke-Probe 'kernel_info' @{} 'kernel_counters'
} catch {$failure=$_.Exception.Message}
finally {
    $process.StandardInput.Close()
    if(!$process.WaitForExit(10000)){$process.Kill();$process.WaitForExit()}
    $diagnostic=$stderr.GetAwaiter().GetResult()
    $process.Dispose()
}
[pscustomobject]@{schema='ark.synthetic-performance.022B.v1';cpu_threads=$CpuThreads;feature_threads=$FeatureThreads;native_threads=$NativeThreads;failure=$failure;failed_attempts=@($script:probeRows|Where-Object failed).Count;worker_requests_including_final_inspection=$kernel.total_worker_requests;worker_starts=$kernel.total_worker_process_starts;rows=$script:probeRows;diagnostic=$diagnostic}|ConvertTo-Json -Depth 8
if($failure){exit 1}
