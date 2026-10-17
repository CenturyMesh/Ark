param(
    [Parameter(Mandatory=$true)][string]$MemorySnapshotDirectory,
    [string]$CodeIndexDirectory,
    [string]$OutputDirectory
)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$repository=[IO.Path]::GetFullPath((Split-Path $PSScriptRoot -Parent)).TrimEnd('\','/')
if(-not $CodeIndexDirectory){$CodeIndexDirectory=Join-Path $repository 'out/context-code'}
$outputRoot=Join-Path $repository 'out/context-memory'
if(-not $OutputDirectory){$OutputDirectory=$outputRoot}
$utf8=New-Object Text.UTF8Encoding($false)
function Child([string]$Root,[string]$Relative){
    if([IO.Path]::IsPathRooted($Relative) -or $Relative -match '[:\x00-\x1f]'){throw 'A vault path must be relative.'}
    $base=[IO.Path]::GetFullPath($Root).TrimEnd('\','/')+[IO.Path]::DirectorySeparatorChar
    $path=[IO.Path]::GetFullPath((Join-Path $Root $Relative))
    if(-not $path.StartsWith($base,[StringComparison]::OrdinalIgnoreCase)){throw 'A vault path escapes its owned directory.'}
    return $path
}
function NoLinks([string]$Path){
    $item=Get-Item -Force -LiteralPath $Path
    while($item){
        if($item.Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Linked vault inputs or outputs are not accepted.'}
        $parent=Split-Path $item.FullName -Parent
        if(-not $parent -or $parent -eq $item.FullName){break}
        $item=Get-Item -Force -LiteralPath $parent
    }
}
function Files([string]$Directory,[int]$Depth=0){
    if($Depth -gt 32){throw 'Vault directory depth is excessive.'}
    foreach($item in Get-ChildItem -Force -LiteralPath $Directory){
        if($item.Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Linked vault content is not accepted.'}
        if($item.PSIsContainer){Files $item.FullName ($Depth+1)}else{$item}
    }
}
function Hash([string]$Path){(Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()}
function InventoryHash($Rows){
    $sha=[Security.Cryptography.SHA256]::Create()
    try{return ([BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes((($Rows|ForEach-Object{"$($_.path) $($_.sha256)"})-join "`n"))))).Replace('-','').ToLowerInvariant()}finally{$sha.Dispose()}
}
function Relative([string]$Root,[string]$Path){$Path.Substring($Root.TrimEnd('\','/').Length+1).Replace('\','/')}
function WriteJson([string]$Path,$Value){[IO.File]::WriteAllText($Path,($Value|ConvertTo-Json -Depth 16),$utf8)}
function TransientPublicationError($Exception,[string]$Stage){
    $cause=$Exception
    while($cause.InnerException){$cause=$cause.InnerException}
    $code=$cause.HResult -band 0xffff
    if($cause -isnot [IO.IOException] -or $code -notin @(32,33,1175)){return $null}
    $unsigned=[uint32]([int64]$cause.HResult -band 0xffffffffL)
    return [pscustomobject]@{stage=$Stage;hresult=('0x{0:X8}' -f $unsigned);win32_error=$code}
}
function PublishCombinedPointer([string]$Temporary,[string]$Destination){
    $clock=[Diagnostics.Stopwatch]::StartNew();$errors=@();$lock=$null
    $expected=Hash $Temporary
    $lockPath=Join-Path (Split-Path $Destination -Parent) '.publish.lock'
    try{
        while(-not $lock){
            try{
                if(Test-Path -LiteralPath $lockPath){NoLinks $lockPath}
                $lock=[IO.File]::Open($lockPath,[IO.FileMode]::OpenOrCreate,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
            }catch{
                $detail=TransientPublicationError $_.Exception 'publisher_lock'
                if(-not $detail){throw}
                $errors+=,$detail
                if($clock.ElapsedMilliseconds -ge 2000){throw ('Atomic pointer publisher lock remained unavailable; HRESULT='+$detail.hresult+'; Win32='+$detail.win32_error)}
                Start-Sleep -Milliseconds ([int][Math]::Max(1,[Math]::Min(50,2000-$clock.ElapsedMilliseconds)))
            }
        }
        while($true){
            try{
                # Reconcile an uncertain native outcome before retrying a move.
                if(-not (Test-Path -LiteralPath $Temporary)){
                    if((Test-Path -LiteralPath $Destination) -and (Hash $Destination) -ceq $expected){break}
                    throw 'Atomic pointer publication requires inspection: prepared file is absent and destination differs.'
                }
                if(Test-Path -LiteralPath $Destination){NoLinks $Destination;[IO.File]::Replace($Temporary,$Destination,[Management.Automation.Language.NullString]::Value)}
                else{[IO.File]::Move($Temporary,$Destination)}
                if((Hash $Destination) -cne $expected){throw 'Published pointer does not match the complete prepared receipt.'}
                break
            }catch{
                $detail=TransientPublicationError $_.Exception 'atomic_replace'
                if(-not $detail){throw}
                $errors+=,$detail
                if($clock.ElapsedMilliseconds -ge 2000){throw ('Atomic pointer replacement remained locked; HRESULT='+$detail.hresult+'; Win32='+$detail.win32_error)}
                Start-Sleep -Milliseconds ([int][Math]::Max(1,[Math]::Min(50,2000-$clock.ElapsedMilliseconds)))
            }
        }
        return [pscustomobject]@{retry_count=$errors.Count;errors=$errors;elapsed_ms=$clock.ElapsedMilliseconds}
    }finally{if($lock){$lock.Dispose()}}
}
function VerifyCode($Root,$Receipt){
    $expected=@{};$evidence=@()
    if(@($Receipt.files).Count -eq 0 -or @($Receipt.files).Count -gt 50000 -or (InventoryHash $Receipt.files) -cne $Receipt.content_sha256){throw 'Code-vault file inventory is not bound to its content receipt.'}
    foreach($row in $Receipt.files){
        if($expected.ContainsKey($row.path) -or $row.sha256 -cnotmatch '^[a-f0-9]{64}$'){throw 'Invalid or duplicate code-vault inventory.'}
        $path=Child $Root $row.path;NoLinks $path
        if((Hash $path) -cne $row.sha256){throw 'Code vault changed after its original verification; preserve it and export a fresh code vault.'}
        $expected[$row.path]=$row.sha256;$evidence+=,$row
    }
    foreach($file in @(Files $Root)){
        $name=Relative $Root $file.FullName
        if($name -cne '.ark-code-vault.json' -and -not $expected.ContainsKey($name)){throw 'Code vault contains an unattested file; preserve it and export separately.'}
    }
    return $expected
}
$CodeIndexDirectory=[IO.Path]::GetFullPath($CodeIndexDirectory).TrimEnd('\','/')
$MemorySnapshotDirectory=[IO.Path]::GetFullPath($MemorySnapshotDirectory).TrimEnd('\','/')
$OutputDirectory=[IO.Path]::GetFullPath($OutputDirectory).TrimEnd('\','/')
if($OutputDirectory -ine $outputRoot -and -not $OutputDirectory.StartsWith($outputRoot+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Combined vaults must stay below this repository out/context-memory.'}
NoLinks $CodeIndexDirectory;NoLinks $MemorySnapshotDirectory
$pointerPath=Child $CodeIndexDirectory 'obsidian-current.json';NoLinks $pointerPath;$pointerHash=Hash $pointerPath
$selection=Get-Content -Raw -Encoding UTF8 -LiteralPath $pointerPath|ConvertFrom-Json
$codeRoot=Child $CodeIndexDirectory $selection.directory;NoLinks $codeRoot
$originPath=Child $codeRoot '.ark-code-vault.json';NoLinks $originPath;$originHash=Hash $originPath
$origin=Get-Content -Raw -Encoding UTF8 -LiteralPath $originPath|ConvertFrom-Json
if($origin.schema -cne 'ark.code-vault.v2' -or $origin.graph_sha256 -cnotmatch '^[a-f0-9]{64}$' -or $origin.graph_sha256 -cne $selection.graph_sha256 -or $origin.source_sha256 -cne $selection.source_sha256 -or $origin.content_sha256 -cne $selection.content_sha256){throw 'Code-vault pointer and source receipt do not agree.'}
$codeFiles=VerifyCode $codeRoot $origin
$manifestPath=Child $MemorySnapshotDirectory 'manifest.json';NoLinks $manifestPath;$memoryHash=Hash $manifestPath
$manifest=Get-Content -Raw -Encoding UTF8 -LiteralPath $manifestPath|ConvertFrom-Json
if($manifest.format -cne 'ark-project-memory-v1' -or $manifest.scope -cnotmatch '^[a-f0-9]{64}$' -or @($manifest.records).Count -gt 2000){throw 'Invalid explicit native memory snapshot.'}
$memoryFiles=@{'manifest.json'=$memoryHash};$notes=@();$ids=@{}
foreach($record in $manifest.records){
    if($record.id -cnotmatch '^[a-f0-9]{64}$' -or $record.fingerprint -cnotmatch '^[a-f0-9]{64}$' -or $ids.ContainsKey($record.id)){throw 'Invalid memory snapshot identity.'}
    $ids[$record.id]=$true;$name=$record.id+'.md';$path=Child $MemorySnapshotDirectory $name;NoLinks $path
    if((Get-Item -LiteralPath $path).Length -gt 512KB){throw 'A memory note exceeds the export bound.'}
    $text=[IO.File]::ReadAllText($path)
    $fenced=[regex]::Matches($text,'(?m)^(`{3,})text\r?\n([^\r\n]+)\r?\n\1[ \t]*(?:\r?\n|$)')
    if($fenced.Count -ne 1){throw 'A native memory note must contain exactly one fenced JSON record.'}
    $row=$fenced[0].Groups[2].Value|ConvertFrom-Json
    if($row.id -cne $record.id -or $row.fingerprint -cne $record.fingerprint){throw 'Memory note and snapshot manifest identities differ.'}
    $memoryFiles[$name]=Hash $path;$notes+=,[pscustomobject]@{name=$name;row=$row}
}
$indexPath=Child $MemorySnapshotDirectory 'index.md';NoLinks $indexPath;$memoryFiles['index.md']=Hash $indexPath
foreach($file in @(Files $MemorySnapshotDirectory)){if(-not $memoryFiles.ContainsKey((Relative $MemorySnapshotDirectory $file.FullName))){throw 'Memory snapshot contains an undeclared file; preserve it and create a fresh native export.'}}
$sourcePaths=@{}
$sourceManifest=Child $CodeIndexDirectory 'source-manifest.json'
if(Test-Path -LiteralPath $sourceManifest){
    NoLinks $sourceManifest
    foreach($source in (Get-Content -Raw -Encoding UTF8 -LiteralPath $sourceManifest|ConvertFrom-Json).files){
        $private=$source.path -match '(?i)(^|/)[.]?(out|private|credentials?|secrets?|sessions?)(/|$)' -or [IO.Path]::GetFileNameWithoutExtension($source.path) -match '(?i)^[.]?(private|credentials?|secrets?|api[-_]?keys?)([._-].*)?$'
        if(-not $private -and ($source.path -match '^(src|include|tests|scripts|cmake)/[^\x00-\x1f]+\.(cpp|hpp|h|inl|c|ps1|cmake)$' -or $source.path -in @('CMakeLists.txt','CMakePresets.json','.graphifyrc'))){$sourcePaths[$source.path]=$true}
    }
}
$existingParent=$OutputDirectory
while(-not (Test-Path -LiteralPath $existingParent)){$existingParent=Split-Path $existingParent -Parent}
NoLinks $existingParent
[IO.Directory]::CreateDirectory($OutputDirectory)|Out-Null;NoLinks $OutputDirectory
$key=$origin.graph_sha256.Substring(0,8)+'-'+$memoryHash.Substring(0,8)+'-'+[guid]::NewGuid().ToString('N').Substring(0,8)
$vault=Child $OutputDirectory $key
if(Test-Path -LiteralPath $vault){throw 'A combined snapshot must use a fresh directory.'}
[IO.Directory]::CreateDirectory($vault)|Out-Null
foreach($name in $codeFiles.Keys){$destination=Child $vault $name;[IO.Directory]::CreateDirectory((Split-Path $destination -Parent))|Out-Null;[IO.File]::Copy((Child $codeRoot $name),$destination);if((Hash $destination) -cne $codeFiles[$name]){throw 'A copied code-vault file did not retain its verified bytes.'}}
[IO.File]::Copy($originPath,(Child $vault 'code-origin-receipt.json'))
$memoryRoot=Child $vault 'ARK Memory';[IO.Directory]::CreateDirectory($memoryRoot)|Out-Null
foreach($name in $memoryFiles.Keys){[IO.File]::Copy((Child $MemorySnapshotDirectory $name),(Child $memoryRoot $name))}
$cards=@{}
foreach($name in $codeFiles.Keys){if([IO.Path]::GetExtension($name) -eq '.md'){$base=[IO.Path]::GetFileName($name);if(-not $cards.ContainsKey($base)){$cards[$base]=@()};$cards[$base]=@($cards[$base])+@($name)}}
$links=@();$anchors=@()
foreach($note in $notes){
    $lines=@('','## Source anchors','','Historical context only. These links grant no edit or geometry authority.','')
    $declared=$note.row.PSObject.Properties['anchors']
    if(-not $declared){continue}
    foreach($anchor in @($declared.Value)){
        $path=[string]$anchor.path;$status='unavailable_not_indexed_code';$current='';$target=$null
        if($path -notmatch '[:\x00-\x1f\[\]`|#]' -and -not [IO.Path]::IsPathRooted($path) -and $path -notmatch '(^|[/\\])\.\.([/\\]|$)' -and $sourcePaths.ContainsKey($path)){
            $source=Child $repository $path
            if(Test-Path -LiteralPath $source -PathType Leaf){NoLinks $source;$current=Hash $source;$status=if($current -ceq $anchor.sha256){'current_source_hash_matches_anchor'}else{'stale_source_hash_changed'}}else{$status='stale_source_missing'}
            $basename=[IO.Path]::GetFileName($path)+'.md'
            if($cards.ContainsKey($basename) -and @($cards[$basename]).Count -eq 1){$target=@($cards[$basename])[0]}
        }
        $anchors+=,[ordered]@{note=('ARK Memory/'+$note.name);path=$path;status=$status;declared_sha256=$anchor.sha256;current_sha256=$current;code_card=$target}
        if($target -and $target -notmatch '[\[\]|#\x00-\x1f]'){
            $destination=Child $vault $target
            if(-not (Test-Path -LiteralPath $destination -PathType Leaf) -or (Hash $destination) -cne $codeFiles[$target]){throw 'A generated memory link has no verified code-card destination.'}
            $wiki='../'+$target.Substring(0,$target.Length-3)
            $lines+='- [['+$wiki+']] - '+$status
            $links+=,[ordered]@{from=('ARK Memory/'+$note.name);to=$target;anchor=$path;status=$status}
        }else{$lines+='- Source anchor: '+$status+'; no unique verified code card.'}
    }
    [IO.File]::AppendAllText((Child $memoryRoot $note.name),($lines -join "`n")+"`n",$utf8)
}
# Recheck inputs after copying; an interrupted or concurrent source edit never
# publishes a current pointer to a partly verified combined view.
if((Hash $pointerPath) -cne $pointerHash -or (Hash $originPath) -cne $originHash -or (Hash $manifestPath) -cne $memoryHash){throw 'An input receipt changed during combined export.'}
$null=VerifyCode $codeRoot $origin
foreach($name in $memoryFiles.Keys){if((Hash (Child $MemorySnapshotDirectory $name)) -cne $memoryFiles[$name]){throw 'Memory snapshot changed during combined export.'}}
foreach($anchor in $anchors){if($anchor.current_sha256){$source=Child $repository $anchor.path;NoLinks $source;if((Hash $source) -cne $anchor.current_sha256){throw 'An observed code anchor changed during combined export.'}}}
$inventory=@()
foreach($file in @(Files $vault|Sort-Object FullName)){$inventory+=,[ordered]@{path=(Relative $vault $file.FullName);sha256=(Hash $file.FullName)}}
$receipt=[ordered]@{schema='ark.combined-memory-vault.v1';graph_sha256=$origin.graph_sha256;code_source_sha256=$origin.source_sha256;code_receipt_sha256=$originHash;memory_manifest_sha256=$memoryHash;memory_scope=$manifest.scope;authority='untrusted_historical_context_only';code_origin_receipt='code-origin-receipt.json';files=$inventory;links_added=$links;anchors=$anchors;source_vault_modified=$false;personal_vault_modified=$false}
$complete=Child $vault '.ark-memory-vault.json';WriteJson $complete $receipt
$receiptHash=Hash $complete
$combinedPointer=Child $OutputDirectory 'current.json';$temporary=Child $OutputDirectory ('current-'+[guid]::NewGuid().ToString('N')+'.tmp')
WriteJson $temporary ([ordered]@{schema='ark.combined-memory-vault-pointer.v1';directory=$key;receipt_sha256=$receiptHash;graph_sha256=$origin.graph_sha256;memory_manifest_sha256=$memoryHash})
$publication=PublishCombinedPointer $temporary $combinedPointer
[ordered]@{directory=$vault;receipt_sha256=$receiptHash;graph_sha256=$origin.graph_sha256;memory_manifest_sha256=$memoryHash;memory_scope=$manifest.scope;links_added=$links.Count;anchor_count=$anchors.Count;publication_retry_count=$publication.retry_count;publication_errors=$publication.errors;personal_vault_modified=$false}|ConvertTo-Json -Depth 5 -Compress
