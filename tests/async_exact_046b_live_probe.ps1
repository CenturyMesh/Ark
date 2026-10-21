param(
    [Parameter(Mandatory=$true)][string]$StudioPath,
    [Parameter(Mandatory=$true)][string]$McpPath,
    [string]$Label='studio',
    [ValidateRange(1,20)][int]$Reps=3,
    [Parameter(Mandatory=$true)][string]$Out
)
# 046B external live MCP probe. One real Studio window with an isolated profile,
# two ark-mcp --connect clients and the synthetic freeform fixture of
# studio-async-exact-ui. Client A runs a query_batch of two exact clearance rows;
# client B sends doc_info 500 ms later; this script times WM_NULL round trips to
# the Studio window every 20 ms. Times have about 20 ms polling resolution.
# No user drawing, settings or cache is read or written.
$ErrorActionPreference='Stop'
[Console]::InputEncoding=New-Object System.Text.UTF8Encoding($false) # no BOM on redirected stdin
Add-Type @"
using System;using System.Runtime.InteropServices;
public static class ArkProbe046 {
  [DllImport("user32.dll",SetLastError=true)] public static extern IntPtr SendMessageTimeout(IntPtr hWnd,uint Msg,IntPtr wParam,IntPtr lParam,uint flags,uint timeout,out IntPtr result);
  [DllImport("user32.dll")] public static extern bool IsHungAppWindow(IntPtr hWnd);
}
"@
$root=Join-Path ([IO.Path]::GetTempPath()) ("ark-046b-probe-"+[guid]::NewGuid().ToString('N').Substring(0,8))
New-Item -ItemType Directory -Force (Join-Path $root 'local'),(Join-Path $root 'roaming') | Out-Null
$studio=$null;$clients=@()
try {
    $endpointFile=Join-Path $root 'endpoint.txt'
    $si=[Diagnostics.ProcessStartInfo]::new();$si.FileName=(Resolve-Path -LiteralPath $StudioPath).Path;$si.UseShellExecute=$false
    $si.Arguments="--live-endpoint-file `"$endpointFile`" --settings-file `"$root\settings.json`" --exit-after-ms 240000 --size 1280 800"
    $si.EnvironmentVariables['LOCALAPPDATA']=(Join-Path $root 'local');$si.EnvironmentVariables['APPDATA']=(Join-Path $root 'roaming');$si.EnvironmentVariables['ARK_NATIVE_RESULT_CACHE']='0'
    $studio=[Diagnostics.Process]::Start($si)
    $deadline=(Get-Date).AddSeconds(30);while(!(Test-Path $endpointFile) -or !(Get-Content $endpointFile -Raw)){if((Get-Date) -gt $deadline){throw 'Studio live endpoint not ready'};Start-Sleep -Milliseconds 100}
    $endpoint=(Get-Content $endpointFile -Raw).Trim()
    $deadline=(Get-Date).AddSeconds(30);while($studio.MainWindowHandle -eq [IntPtr]::Zero){$studio.Refresh();if((Get-Date) -gt $deadline){throw 'Studio window not ready'};Start-Sleep -Milliseconds 100}
    $hwnd=$studio.MainWindowHandle
    function Start-Client(){
        $p=[Diagnostics.ProcessStartInfo]::new();$p.FileName=(Resolve-Path -LiteralPath $McpPath).Path;$p.Arguments="--connect $endpoint";$p.UseShellExecute=$false;$p.CreateNoWindow=$true;$p.RedirectStandardInput=$true;$p.RedirectStandardOutput=$true;$p.RedirectStandardError=$true
        $proc=[Diagnostics.Process]::Start($p);$null=$proc.StandardError.ReadToEndAsync()
        $w=New-Object System.IO.StreamWriter($proc.StandardInput.BaseStream,(New-Object System.Text.UTF8Encoding($false)));$w.AutoFlush=$true
        [pscustomobject]@{proc=$proc;writer=$w;id=0}
    }
    function Send($client,[string]$tool,$arguments){$client.id++;$client.writer.WriteLine((@{jsonrpc='2.0';id=$client.id;method='tools/call';params=@{name=$tool;arguments=$arguments}}|ConvertTo-Json -Depth 64 -Compress));return $client.proc.StandardOutput.ReadLineAsync()}
    function Result($task){$response=$task.Result|ConvertFrom-Json;if($response.error){throw "JSON-RPC error: $($response.error.message)"};return $response.result}
    $a=Start-Client;$b=Start-Client;$clients=@($a,$b)
    Start-Sleep -Seconds 2
    $info=Result (Send $a 'doc_info' @{detail='revision'})
    $fresh=Result (Send $a 'doc_new' @{expected_revision=$info.structuredContent.revision});if($fresh.isError){throw "doc_new: $($fresh.structuredContent.error)"}
    function Section([double]$scale,[double]$phase){$dof=New-Object System.Collections.Generic.List[double];for($i=0;$i -lt 32;$i++){$t=2*[math]::PI*$i/32;$r=$scale*(1+0.18*[math]::Sin(5*$t+$phase)+0.07*[math]::Cos(9*$t));$dof.Add([math]::Round($r*[math]::Cos($t)*1.6,4));$dof.Add([math]::Round($r*[math]::Sin($t),4))};return ,($dof.ToArray())}
    $commands=New-Object System.Collections.Generic.List[object]
    foreach($body in 'A','B'){$names=New-Object System.Collections.Generic.List[string]
        for($s=0;$s -lt 5;$s++){$plane="P$body$s";$commands.Add(@{op='plane.set';name=$plane;p=@{oz=[string](18*$s)}});$phase=$s*0.7;if($body -eq 'B'){$phase+=1}
            $commands.Add(@{op='sketch.set';name="S$body$s";sketch=@{plane=$plane;entities=@(@{name='skin';kind='spline';closed=$true;dof=(Section (20+8*[math]::Sin($s*1.1)) $phase)})}});$names.Add("S$body$s")}
        $commands.Add(@{op='feature.add';type='loft';name="Hull $body";p=@{solid='1';ruled='0';smooth='1'};native=@{sections=$names.ToArray()}})}
    $commands.Add(@{op='assembly.add';name='Hull A';feature=1});$commands.Add(@{op='assembly.add';name='Hull B';feature=2;p=@{tx='72';ty='6';tz='4';rz='7'}})
    $applied=Result (Send $a 'doc_apply' @{expected_revision=$fresh.structuredContent.revision;commands=$commands.ToArray()});if($applied.isError){throw "fixture: $($applied.structuredContent.error)"}
    $clearance=@{detail='result';clearance=@{a=1;b=2}}
    for($i=0;$i -lt 2;$i++){$null=Result (Send $a 'assembly_info' $clearance)} # warm the worker paths
    $rows=@()
    for($rep=0;$rep -lt $Reps;$rep++){
        $watch=[Diagnostics.Stopwatch]::StartNew()
        $task=Send $a 'query_batch' @{queries=@(@{tool='assembly_info';args=$clearance},@{tool='assembly_info';args=$clearance})}
        $beats=New-Object System.Collections.Generic.List[double];$wall=$null;$second=$null;$secondTask=$null;$secondStart=0;$hung=0
        while($wall -eq $null -or $second -eq $null){
            if(!$secondTask -and $watch.ElapsedMilliseconds -ge 500){$secondStart=$watch.Elapsed.TotalMilliseconds;$secondTask=Send $b 'doc_info' @{detail='revision'}}
            if($secondTask -and $second -eq $null -and $secondTask.IsCompleted){$second=$watch.Elapsed.TotalMilliseconds-$secondStart}
            if($wall -eq $null -and $task.IsCompleted){$wall=$watch.Elapsed.TotalMilliseconds}
            if($wall -eq $null){$t0=$watch.Elapsed.TotalMilliseconds;$ignored=[IntPtr]::Zero;$null=[ArkProbe046]::SendMessageTimeout($hwnd,0,[IntPtr]::Zero,[IntPtr]::Zero,0,30000,[ref]$ignored);$beats.Add($watch.Elapsed.TotalMilliseconds-$t0);if([ArkProbe046]::IsHungAppWindow($hwnd)){$hung++}}
            if($watch.Elapsed.TotalSeconds -gt 120){throw 'probe request did not complete in 120 s'}
            Start-Sleep -Milliseconds 20
        }
        $res=Result $task;if($res.isError){throw "query_batch: $($res.structuredContent.error)"}
        $sorted=@($beats|Sort-Object);$p95=$sorted[[math]::Max(0,[int][math]::Ceiling(0.95*$sorted.Count)-1)]
        $rows+=[pscustomobject]@{label=$Label;rep=$rep;wall_ms=[math]::Round($wall,1);row_errors=$res.structuredContent.error_count;background_job=($null -ne $res.structuredContent.exact_job);observed_revision=$res.structuredContent.observed_revision;run_ms=$res.structuredContent.exact_job.run_ms;heartbeat_max_ms=[math]::Round($sorted[-1],1);heartbeat_p95_ms=[math]::Round($p95,1);heartbeats=$beats.Count;hung_samples=$hung;second_client_doc_info_ms=[math]::Round($second,1)}
    }
    $rows|ConvertTo-Json|Set-Content -Path $Out -Encoding UTF8
    $rows|Format-Table -AutoSize|Out-String -Width 240
} finally {
    foreach($client in $clients){try{$client.writer.Close();if(!$client.proc.WaitForExit(5000)){$client.proc.Kill()}}catch{}}
    if($studio){try{if(!$studio.HasExited){$studio.CloseMainWindow()|Out-Null;if(!$studio.WaitForExit(20000)){$studio.Kill()}}}catch{}}
    Remove-Item -Recurse -Force $root -ErrorAction SilentlyContinue
}
