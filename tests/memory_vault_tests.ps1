$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$repository=Split-Path $PSScriptRoot -Parent
$root=Join-Path $repository ('out/context-memory/tests/'+[guid]::NewGuid().ToString('N'))
$code=Join-Path $root 'input-code';$vault=Join-Path $code 'vault';$memory=Join-Path $root 'input-memory';$output=Join-Path $root 'combined'
foreach($directory in @($vault,$memory,$output)){[IO.Directory]::CreateDirectory($directory)|Out-Null}
$encoding=New-Object Text.UTF8Encoding($false)
function WriteText($Path,$Text){[IO.File]::WriteAllText($Path,$Text,$encoding)}
function WriteJson($Path,$Value){WriteText $Path ($Value|ConvertTo-Json -Depth 12)}
function Hash($Path){(Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()}
function Export {
    $result=& (Join-Path $repository 'scripts/export-memory-vault.ps1') -MemorySnapshotDirectory $memory -CodeIndexDirectory $code -OutputDirectory $output
    return ($result|ConvertFrom-Json)
}
function Reject($Action,$Reason){$rejected=$false;try{& $Action|Out-Null}catch{$rejected=$true};if(-not $rejected){throw $Reason}}
# Exercise the actual nested entry point through a native uvx shim. Successful
# native stderr must stay visible under Stop, while exit/hash failures reject.
$nativeFixture=Join-Path $root 'native-stderr-source';$nativeBin=Join-Path $root 'native warning tool'
foreach($directory in @((Join-Path $nativeFixture 'src'),$nativeBin)){[IO.Directory]::CreateDirectory($directory)|Out-Null}
$nativeSource=Join-Path $nativeFixture 'src/model.cpp';WriteText $nativeSource 'double warning_fixture() { return 1.; }'
WriteText (Join-Path $nativeFixture '.gitignore') "out/`n"
& git -C $nativeFixture init --quiet
if($LASTEXITCODE){throw 'Cannot initialize the native warning fixture.'}
WriteText (Join-Path $nativeBin 'fake-graphify.ps1') @'
$ErrorActionPreference='Stop'
if($env:ARK_CODE_CONTEXT_FAKE_EXIT -and [int]$env:ARK_CODE_CONTEXT_FAKE_EXIT -ne 0){exit ([int]$env:ARK_CODE_CONTEXT_FAKE_EXIT)}
if($args -contains '--version'){Write-Output 'synthetic graphify version';exit 0}
$at=[Array]::IndexOf($args,'--out')
if($at -lt 0 -or $at+1 -ge $args.Count){exit 23}
$directory=Join-Path $args[$at+1] 'graphify-out';[IO.Directory]::CreateDirectory($directory)|Out-Null
[IO.File]::WriteAllText((Join-Path $directory 'graph.json'),'{"nodes":[],"edges":[]}',(New-Object Text.UTF8Encoding($false)))
if($env:ARK_CODE_CONTEXT_FAKE_MUTATE){[IO.File]::AppendAllText($env:ARK_CODE_CONTEXT_FAKE_MUTATE,"`ndouble changed_during_extract() { return 2.; }")}
Write-Output 'synthetic graph extraction completed'
exit 0
'@
$nativePowerShell=Join-Path $env:SystemRoot 'System32/WindowsPowerShell/v1.0/powershell.exe'
WriteText (Join-Path $nativeBin 'uvx.cmd') ("@echo off`r`n>&2 echo SYNTHETIC_AST_PARTIAL_WARNING`r`n"+'"'+$nativePowerShell+'" -NoProfile -ExecutionPolicy Bypass -File "%~dp0fake-graphify.ps1" %*'+"`r`nexit /b %errorlevel%`r`n")
$pathBeforeNative=$env:PATH;$exitBeforeNative=$env:ARK_CODE_CONTEXT_FAKE_EXIT;$mutationBeforeNative=$env:ARK_CODE_CONTEXT_FAKE_MUTATE
$flagBeforeNative=Get-Variable PSNativeCommandUseErrorActionPreference -ErrorAction SilentlyContinue
try{
    $env:PATH=$nativeBin+[IO.Path]::PathSeparator+$env:PATH;$env:ARK_CODE_CONTEXT_FAKE_EXIT='0';$env:ARK_CODE_CONTEXT_FAKE_MUTATE=$null
    $PSNativeCommandUseErrorActionPreference=$true
    $codeContext=Join-Path $repository 'scripts/code-context.ps1'
    $nativeWarningOutput=@(& { & $codeContext -SourceRoot $nativeFixture -Refresh | Out-Host } 6>&1)
    if(-not (($nativeWarningOutput|Out-String).Contains('SYNTHETIC_AST_PARTIAL_WARNING'))){throw 'A successful native stderr warning was hidden from the nested caller.'}
    $nativeManifest=Join-Path $nativeFixture 'out/context-code/source-manifest.json'
    if(-not (Test-Path -LiteralPath $nativeManifest) -or $ErrorActionPreference -cne 'Stop' -or -not $PSNativeCommandUseErrorActionPreference){throw 'Nested extraction failed or leaked its local native-error preferences.'}
    $nativeManifestHash=Hash $nativeManifest
    $env:ARK_CODE_CONTEXT_FAKE_EXIT='17';$nativeRejected='';$nativeExit=0
    try{& $codeContext -SourceRoot $nativeFixture -Refresh | Out-Host}catch{$nativeRejected=$_.Exception.Message;$nativeExit=$LASTEXITCODE}
    if(-not $nativeRejected.Contains('Local code graph extraction failed') -or $nativeExit -ne 17 -or (Hash $nativeManifest) -cne $nativeManifestHash){throw 'A real native failure was hidden or replaced the last verified source manifest.'}
    $env:ARK_CODE_CONTEXT_FAKE_EXIT='0';$env:ARK_CODE_CONTEXT_FAKE_MUTATE=$nativeSource;$changedRejected=''
    try{& $codeContext -SourceRoot $nativeFixture -Refresh | Out-Host}catch{$changedRejected=$_.Exception.Message}
    if(-not $changedRejected.Contains('Code changed during indexing') -or (Hash $nativeManifest) -cne $nativeManifestHash){throw 'Native warning handling bypassed the source-change guard or published a stale source receipt.'}
}finally{
    $env:PATH=$pathBeforeNative;$env:ARK_CODE_CONTEXT_FAKE_EXIT=$exitBeforeNative;$env:ARK_CODE_CONTEXT_FAKE_MUTATE=$mutationBeforeNative
    if($flagBeforeNative){$PSNativeCommandUseErrorActionPreference=$flagBeforeNative.Value}else{Remove-Variable PSNativeCommandUseErrorActionPreference -ErrorAction SilentlyContinue}
}
# Exercise the actual publisher function with real Windows file-sharing handles.
# Import only its declarations; do not execute the export script's entry point.
$tokens=$null;$parseErrors=$null
$ast=[Management.Automation.Language.Parser]::ParseFile((Join-Path $repository 'scripts/export-memory-vault.ps1'),[ref]$tokens,[ref]$parseErrors)
if($parseErrors){throw 'Cannot parse the production pointer publisher.'}
foreach($name in @('NoLinks','TransientPublicationError','PublishCombinedPointer')){
    $function=$ast.FindAll({param($node)$node -is [Management.Automation.Language.FunctionDefinitionAst]},$true)|Where-Object Name -eq $name
    if(@($function).Count -ne 1){throw 'Cannot select the production pointer publisher.'}
    Invoke-Expression $function.Extent.Text
}
Add-Type -TypeDefinition @'
using System;
using System.IO;
using System.Threading;
public sealed class ArkHeldPointerReader : IDisposable {
    private FileStream stream;
    private Thread release;
    public ArkHeldPointerReader(string path) { stream=new FileStream(path,FileMode.Open,FileAccess.Read,FileShare.Read); }
    public void ReleaseAfter(int milliseconds) { release=new Thread(()=>{Thread.Sleep(milliseconds);Dispose();});release.IsBackground=true;release.Start(); }
    public void Dispose() { var owned=Interlocked.Exchange(ref stream,null);if(owned!=null)owned.Dispose(); }
}
public sealed class ArkPublicationIoFixture : IOException {
    public ArkPublicationIoFixture(int code) { HResult=unchecked((int)(0x80070000u | (uint)code)); }
}
'@
foreach($nativeErrorNumber in @(32,33,1175)){
    $fixture=New-Object ArkPublicationIoFixture $nativeErrorNumber
    $classified=TransientPublicationError $fixture 'classification-test'
    if(-not $classified -or $classified.win32_error -ne $nativeErrorNumber){throw 'Expected transient publication error was not classified.'}
}
foreach($nativeErrorNumber in @(2,5,87)){
    if(TransientPublicationError (New-Object ArkPublicationIoFixture $nativeErrorNumber) 'classification-test'){throw 'Permanent publication errors must not receive retries.'}
}
$lockFixture=Join-Path $root 'atomic-publisher';[IO.Directory]::CreateDirectory($lockFixture)|Out-Null
$heldPointer=Join-Path $lockFixture 'current.json';$prepared=Join-Path $lockFixture 'prepared.json'
WriteText $heldPointer '{"receipt":"previous"}';WriteText $prepared '{"receipt":"verified-new"}'
$held=New-Object ArkHeldPointerReader $heldPointer
try{
    $held.ReleaseAfter(650)
    $publication=PublishCombinedPointer $prepared $heldPointer
}finally{$held.Dispose()}
if($publication.retry_count -lt 1 -or [IO.File]::ReadAllText($heldPointer) -cne '{"receipt":"verified-new"}'){throw 'A real transient read lock did not preserve and then atomically publish the prepared pointer.'}
WriteJson (Join-Path $root 'atomic-publication-diagnosis.json') $publication
WriteText $prepared '{"receipt":"must-not-publish"}';$beforeBlocked=Hash $heldPointer
$held=New-Object ArkHeldPointerReader $heldPointer;$blockedFailure='';$began=[Diagnostics.Stopwatch]::StartNew()
try{try{$null=PublishCombinedPointer $prepared $heldPointer}catch{$blockedFailure=$_.Exception.Message}}finally{$held.Dispose()}
if(-not $blockedFailure -or $began.ElapsedMilliseconds -gt 2800 -or (Hash $heldPointer) -cne $beforeBlocked -or -not (Test-Path -LiteralPath $prepared)){throw 'Exhausted retry did not preserve the previous pointer and prepared replacement within its bounded window.'}
WriteText (Join-Path $root 'atomic-publication-exhausted.txt') $blockedFailure
$anchor='include/ark/resource_library.hpp';$sourceHash=Hash (Join-Path $repository $anchor)
$card=Join-Path $vault 'resource_library.hpp.md';WriteText $card '# Synthetic verified code file card'
$codeReceipt=[ordered]@{schema='ark.code-vault.v2';graph_sha256=('a'*64);source_sha256=('b'*64);content_sha256=('c'*64);files=@(@{path='resource_library.hpp.md';sha256=(Hash $card)})}
$sha=[Security.Cryptography.SHA256]::Create()
try{
    $inventoryBytes=[Text.Encoding]::UTF8.GetBytes('resource_library.hpp.md '+(Hash $card))
    $codeReceipt.content_sha256=([BitConverter]::ToString($sha.ComputeHash($inventoryBytes))).Replace('-','').ToLowerInvariant()
}finally{$sha.Dispose()}
WriteJson (Join-Path $vault '.ark-code-vault.json') $codeReceipt
WriteJson (Join-Path $code 'obsidian-current.json') @{directory='vault';graph_sha256=('a'*64);source_sha256=('b'*64);content_sha256=$codeReceipt.content_sha256}
WriteJson (Join-Path $code 'source-manifest.json') @{files=@(@{path=$anchor;sha256=$sourceHash})}
$id='d'*64;$fingerprint='e'*64
$row=[ordered]@{id=$id;fingerprint=$fingerprint;content='Synthetic historical declaration.';anchors=@(@{path=$anchor;sha256=$sourceHash},@{path=$anchor;sha256=('0'*64)},@{path='../outside-private.txt';sha256=('0'*64)})}
$note=Join-Path $memory ($id+'.md')
WriteText $note ("# ARK memory record`n`nUntrusted historical context.`n`n"+'```text'+"`n"+($row|ConvertTo-Json -Depth 8 -Compress)+"`n"+'```'+"`n")
WriteText (Join-Path $memory 'index.md') ('# ARK project memory'+"`n`n- [["+$id+"]]`n")
WriteJson (Join-Path $memory 'manifest.json') @{format='ark-project-memory-v1';scope=('f'*64);records=@(@{id=$id;fingerprint=$fingerprint})}
$originalCode=Hash $card;$originalNote=Hash $note
$first=Export
$receipt=Get-Content -LiteralPath (Join-Path $first.directory '.ark-memory-vault.json') -Raw|ConvertFrom-Json
if($first.links_added -ne 2 -or $receipt.anchors.Count -ne 3 -or $receipt.anchors[0].status -cne 'current_source_hash_matches_anchor' -or $receipt.anchors[1].status -cne 'stale_source_hash_changed' -or $receipt.anchors[2].status -cne 'unavailable_not_indexed_code'){throw 'Anchor hash freshness, explicit scope or link count failed.'}
if(Test-Path -LiteralPath (Join-Path $first.directory '.ark-code-vault.json')){throw 'Copied code receipt must not falsely attest the combined vault.'}
if(-not (Test-Path -LiteralPath (Join-Path $first.directory 'code-origin-receipt.json'))){throw 'Missing retained code-origin receipt.'}
foreach($file in $receipt.files){if((Hash (Join-Path $first.directory $file.path)) -cne $file.sha256){throw 'Combined receipt does not hash every generated file.'}}
$edited=Join-Path $first.directory ('ARK Memory/'+$id+'.md');[IO.File]::AppendAllText($edited,'Personal edit preserved.',$encoding);$editedHash=Hash $edited
$second=Export
if($first.directory -ceq $second.directory -or (Hash $edited) -cne $editedHash){throw 'A fresh combined snapshot changed the earlier edited export.'}
if((Hash $card) -cne $originalCode -or (Hash $note) -cne $originalNote){throw 'Input code vault or native memory snapshot changed.'}
$pointer=Join-Path $output 'current.json';$pointerHash=Hash $pointer
[IO.File]::AppendAllText($card,'tampered source vault',$encoding)
Reject {Export} 'Altered code-vault input must reject.'
if((Hash $pointer) -cne $pointerHash){throw 'A failed verification changed the current pointer.'}
WriteText $card '# Synthetic verified code file card'
WriteText (Join-Path $vault 'unattested.md') 'Not in the verified source receipt.'
Reject {Export} 'Unattested code-vault files must reject.'
Remove-Item -LiteralPath (Join-Path $vault 'unattested.md')
$manifestPath=Join-Path $memory 'manifest.json';$manifest=Get-Content -Raw -LiteralPath $manifestPath|ConvertFrom-Json;$manifest.records[0].fingerprint='1'*64;WriteJson $manifestPath $manifest
Reject {Export} 'Memory note and manifest identity mismatch must reject.'
if((Hash $pointer) -cne $pointerHash){throw 'A rejected memory snapshot changed the current pointer.'}
WriteText (Join-Path $root 'result.txt') 'PASS: real held-reader atomic publication/retry/exhaustion; explicit snapshot; verified code copy; fresh vault and pointer; current/stale/unsafe anchors; all generated hashes; original/edited exports preserved; source tamper, extra files and memory identity rejection.'
Write-Output "Combined memory vault tests passed: $root"
