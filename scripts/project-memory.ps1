param(
 [ValidateSet('wakeup','search','get','store','forget','stats','export','promote','feedback','consolidate','reindex','compress')][string]$Action='wakeup',
 [string]$Query,
 [string]$Project,
 [switch]$Shared,
 [switch]$Obsidian,
 [string]$ArgumentsJson='{}',
 [string]$ArgumentsFile,
 [string]$Directory,
 [string]$Executable,
 [string]$Actor='coding-agent:caller-declared'
)
$ErrorActionPreference='Stop'
if($Obsidian){$Action='export'}
$repository=Split-Path $PSScriptRoot -Parent
if($Shared -and $Project){throw 'Choose Shared or an explicit Project, not both.'}
if($Shared){$Project='ark:shared'}
if(-not $Project){
 $remote=& git -C $repository remote get-url origin 2>$null
 if($LASTEXITCODE -ne 0 -or -not $remote){$remote=(& git -C $repository rev-parse --path-format=absolute --git-common-dir).Trim()}
 $sha=[Security.Cryptography.SHA256]::Create()
 try{$Project='code:'+([BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($remote.Trim().ToLowerInvariant())))).Replace('-','').ToLowerInvariant()}finally{$sha.Dispose()}
}
if(-not $Executable){
 $candidates=@((Join-Path $repository 'out/native-release/Release/ark-cli.exe'),(Join-Path $repository 'out/core/Release/ark-cli.exe'))
 $install=Join-Path $env:LOCALAPPDATA 'ARK/app'
 if(Test-Path -LiteralPath (Join-Path $install 'current.json')){$state=Get-Content -LiteralPath (Join-Path $install 'current.json') -Raw | ConvertFrom-Json;$candidates+=Join-Path $install ('versions/'+$state.current+'/ark-cli.exe')}
 foreach($candidate in $candidates){
  $identity=Join-Path (Split-Path $candidate -Parent) 'app-version.json'
  if((Test-Path -LiteralPath $candidate) -and (Test-Path -LiteralPath $identity)){
   $version=Get-Content -LiteralPath $identity -Raw | ConvertFrom-Json
   if(([version]$version.version) -ge [version]'0.9.40'){$Executable=$candidate;break}
  }
 }
}
if(-not $Executable -or -not (Test-Path -LiteralPath $Executable -PathType Leaf)){throw 'Build or install the ARK runtime before using project memory.'}
if($ArgumentsFile){$ArgumentsJson=Get-Content -LiteralPath $ArgumentsFile -Raw -Encoding UTF8}
$argsObject=$ArgumentsJson | ConvertFrom-Json
if(-not ($argsObject -is [pscustomobject])){throw 'Memory arguments must be a JSON object.'}
if($Query){$argsObject | Add-Member -Force NoteProperty text $Query}
$json=ConvertTo-Json -InputObject $argsObject -Depth 16 -Compress
$nativeArgs=@('--memory',$Action,'--project',$Project,'--actor',$Actor)
if($Directory){$nativeArgs+=@('--root',$Directory)}
$previousEncoding=$OutputEncoding
$previousConsoleEncoding=[Console]::OutputEncoding
try{
 $OutputEncoding=New-Object Text.UTF8Encoding($false)
 [Console]::OutputEncoding=New-Object Text.UTF8Encoding($false)
 $nativeOutput=@($json | & $Executable @nativeArgs)
 if($LASTEXITCODE -ne 0){$nativeOutput | Write-Output;throw 'ARK memory returned an explicit rejection.'}
 if($Obsidian){
  $snapshot=($nativeOutput -join "`n") | ConvertFrom-Json
  & (Join-Path $PSScriptRoot 'code-context.ps1') -Obsidian | Out-Host
  if($LASTEXITCODE -ne 0){throw 'Code vault verification failed before memory bridge export.'}
  & (Join-Path $PSScriptRoot 'export-memory-vault.ps1') -MemorySnapshotDirectory $snapshot.directory
 }else{$nativeOutput | Write-Output}
}finally{$OutputEncoding=$previousEncoding;[Console]::OutputEncoding=$previousConsoleEncoding}
