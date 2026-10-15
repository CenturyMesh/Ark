param(
    [string]$Executable='out/native-release/ark-performance-regression.exe',
    [Parameter(Mandatory=$true)][string]$OutputPath,
    [string]$Baseline,
    [ValidateRange(3,101)][int]$Samples=21
)
$ErrorActionPreference='Stop'
Import-Module (Join-Path $PSScriptRoot 'Ark.Performance.psm1') -Force
$repository=Split-Path $PSScriptRoot -Parent
if(-not [IO.Path]::IsPathRooted($Executable)){$Executable=Join-Path $repository $Executable}
$Executable=(Resolve-Path -LiteralPath $Executable).Path
$OutputPath=[IO.Path]::GetFullPath($OutputPath)
if(Test-Path -LiteralPath $OutputPath){throw 'Receipt already exists; preserve previous runs and use a new output path.'}
$directory=Split-Path $OutputPath -Parent;[IO.Directory]::CreateDirectory($directory) | Out-Null
$fixtures=Join-Path $directory ('synthetic-fixtures-'+[guid]::NewGuid().ToString('N'))
$executableHash=(Get-FileHash -LiteralPath $Executable -Algorithm SHA256).Hash.ToLowerInvariant()
$attempt=[ordered]@{schema='ark.performance.attempt.v1';valid=$false;started_utc=[DateTime]::UtcNow.ToString('o');samples=$Samples;executable_sha256=$executableHash;stage='collecting_measurements';scope='Incomplete attempt; no valid timing comparison. Synthetic fixtures and runner diagnostics are retained.'}
$attemptBytes=[Text.Encoding]::UTF8.GetBytes(($attempt | ConvertTo-Json -Depth 8)+"`n")
$attemptFile=[IO.File]::Open($OutputPath,'CreateNew','Write','None')
try{$attemptFile.Write($attemptBytes,0,$attemptBytes.Length)}finally{$attemptFile.Dispose()}
function Invoke-Measurement([string]$Arguments){
    $start=New-Object Diagnostics.ProcessStartInfo;$start.FileName=$Executable;$start.Arguments=$Arguments;$start.UseShellExecute=$false;$start.CreateNoWindow=$true;$start.RedirectStandardOutput=$true;$start.RedirectStandardError=$true
    $process=New-Object Diagnostics.Process;$process.StartInfo=$start;$watch=[Diagnostics.Stopwatch]::StartNew()
    try{
        if(-not $process.Start()){throw 'Cannot start performance executable.'}
        $stdout=$process.StandardOutput.ReadToEndAsync();$stderr=$process.StandardError.ReadToEndAsync()
        if(-not $process.WaitForExit(120000)){$process.Kill();throw 'Performance process timed out.'};$watch.Stop()
        if($process.ExitCode -ne 0){throw ('Performance process failed: '+$stderr.Result)}
        @{result=($stdout.Result | ConvertFrom-Json);process_ms=$watch.Elapsed.TotalMilliseconds}
    }finally{$process.Dispose()}
}
function Distribution($Values){$sorted=@($Values | Sort-Object);@{samples_ms=@($Values);median_ms=$sorted[[int][math]::Floor($sorted.Count/2)];p95_ms=$sorted[[int][math]::Ceiling(.95*$sorted.Count)-1]}}
try {
$measurement=Invoke-Measurement ('--out-dir "'+$fixtures+'" --samples '+$Samples)
$report=$measurement.result
foreach($row in $report.results){
    $open=@();$processWall=@();$fixture=Join-Path $fixtures $row.fixture_file
    for($index=0;$index -lt $Samples;$index++){
        $opened=Invoke-Measurement ('--open-once "'+$fixture+'"')
        if($opened.result.feature_count -ne $row.feature_count -or $opened.result.authoring_sha256 -ne $row.fixture_sha256){throw 'Fresh-process readback differs from the benchmark fixture.'}
        $open+=$opened.result.load_recompute_ms;$processWall+=$opened.process_ms
    }
    $row | Add-Member NoteProperty fresh_process_open (Distribution $open)
    $row | Add-Member NoteProperty fresh_process_wall (Distribution $processWall)
}
$cpu=(Get-CimInstance Win32_Processor | Select-Object -First 1).Name.Trim()
$os=Get-CimInstance Win32_OperatingSystem
$machineText=$cpu+'|'+$os.Version+'|'+$os.OSArchitecture+'|'+$env:NUMBER_OF_PROCESSORS
$sha=[Security.Cryptography.SHA256]::Create();try{$machineHash=-join ($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($machineText)) | ForEach-Object {$_.ToString('x2')})}finally{$sha.Dispose()}
$report | Add-Member NoteProperty machine_signature $machineHash
$report | Add-Member NoteProperty machine @{cpu=$cpu;os_version=$os.Version;architecture=$os.OSArchitecture;logical_processors=$env:NUMBER_OF_PROCESSORS}
$report | Add-Member NoteProperty executable_sha256 $executableHash
$report | Add-Member NoteProperty measured_utc ([DateTime]::UtcNow.ToString('o'))
$report | Add-Member NoteProperty cold_scope 'Fresh executable process, deserialize and full recompute; filesystem cache remains warm. OS cold-cache and UI-frame latency are not measured.'
$report | Add-Member NoteProperty fixture_scope 'Synthetic local benchmark fixtures retained beside this report; no user drawings are read.'
if($Baseline){$comparison=Compare-ArkPerformance $report (Get-Content -LiteralPath $Baseline -Raw | ConvertFrom-Json);$report | Add-Member NoteProperty comparison $comparison}
$report | Add-Member NoteProperty valid $true
[IO.File]::WriteAllText($OutputPath,($report | ConvertTo-Json -Depth 30)+"`n",(New-Object Text.UTF8Encoding($false)))
Write-Output "Performance receipt: $OutputPath"
} catch {
    $attempt.stage='failed'
    $attempt.finished_utc=[DateTime]::UtcNow.ToString('o')
    $attempt.failure_type=$_.Exception.GetType().Name
    [IO.File]::WriteAllText($OutputPath,($attempt | ConvertTo-Json -Depth 8)+"`n",(New-Object Text.UTF8Encoding($false)))
    throw
}
