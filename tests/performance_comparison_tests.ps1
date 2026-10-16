$ErrorActionPreference='Stop'
Import-Module (Join-Path (Split-Path $PSScriptRoot -Parent) 'scripts/Ark.Performance.psm1') -Force
function Fixture {
    @{schema='ark.performance.v1';workload='synthetic';samples=21;machine_signature='machine';results=@(foreach($size in @(16,128,512)){
        $row=@{feature_count=$size;fixture_sha256=('digest-'+$size)};foreach($name in @('fresh_process_open','warm_open','incremental_save','interaction_doc_info','recompute_changed_parameter')){$row[$name]=@{median_ms=10;p95_ms=20}};$row
    })}
}
$a=Fixture;$b=Fixture;$comparison=Compare-ArkPerformance $a $b
if($comparison.Count -ne 30 -or @($comparison | Where-Object {$_.ratio -ne 1}).Count){throw 'Equivalent workload comparison differs.'}
foreach($field in @('workload','samples','machine_signature')){$different=Fixture;$different[$field]='different';$rejected=$false;try{Compare-ArkPerformance $a $different | Out-Null}catch{$rejected=$true};if(-not $rejected){throw "Comparison accepted changed $field"}}
$different=Fixture;$different.results[0].fixture_sha256='different';$rejected=$false;try{Compare-ArkPerformance $a $different | Out-Null}catch{$rejected=$true};if(-not $rejected){throw 'Changed source fixture accepted'}
$different=Fixture;$different.results[0].warm_open.median_ms=0;$rejected=$false;try{Compare-ArkPerformance $a $different | Out-Null}catch{$rejected=$true};if(-not $rejected){throw 'Invalid zero baseline accepted'}
$different=Fixture;$different.valid=$false;$rejected=$false;try{Compare-ArkPerformance $a $different | Out-Null}catch{$rejected=$true};if(-not $rejected){throw 'Incomplete attempt accepted'}
if($env:OS -eq 'Windows_NT' -and $PSVersionTable.PSVersion.Major -le 5){
    $workspace=Join-Path ([IO.Path]::GetTempPath()) ('ark-performance-failure-'+[guid]::NewGuid().ToString('N'))
    [IO.Directory]::CreateDirectory($workspace)|Out-Null
    try{
        $executable=Join-Path $workspace 'synthetic-failure.exe';$receipt=Join-Path $workspace 'attempt.json'
        Add-Type -TypeDefinition 'public class ArkPerfFailureFixture { public static int Main(string[] args) { return 9; } }' -OutputAssembly $executable -OutputType ConsoleApplication
        $runner=Join-Path (Split-Path $PSScriptRoot -Parent) 'scripts/benchmark-regression.ps1'
        $rejected=$false;try{& $runner -Executable $executable -OutputPath $receipt -Samples 3 | Out-Null}catch{$rejected=$true}
        if(-not $rejected){throw 'Synthetic failed measurement was accepted'}
        $failed=Get-Content -LiteralPath $receipt -Raw|ConvertFrom-Json
        if($failed.valid -or $failed.stage -ne 'failed' -or -not $failed.finished_utc -or $failed.executable_sha256 -cne (Get-FileHash -LiteralPath $executable).Hash.ToLowerInvariant()){throw 'Failed process did not retain its invalid attempt receipt'}
        $before=(Get-FileHash -LiteralPath $receipt).Hash
        $rejected=$false;try{& $runner -Executable $executable -OutputPath $receipt -Samples 3 | Out-Null}catch{$rejected=$true}
        if(-not $rejected -or (Get-FileHash -LiteralPath $receipt).Hash -cne $before){throw 'A retry replaced the failed attempt receipt'}
    }finally{
        $full=[IO.Path]::GetFullPath($workspace);$temporary=[IO.Path]::GetFullPath([IO.Path]::GetTempPath());if(-not $full.StartsWith($temporary,[StringComparison]::OrdinalIgnoreCase)){throw 'Unsafe performance test cleanup'};Remove-Item -LiteralPath $workspace -Recurse -Force
    }
}
'Performance comparison tests passed: equivalent 3-size results, machine/workload/sample/hash mismatch and invalid baseline rejection.'
