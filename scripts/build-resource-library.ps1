param([Parameter(Mandatory=$true)][string]$SourceDirectory,[Parameter(Mandatory=$true)][string]$OutputPath)
$ErrorActionPreference='Stop'
$names=@('ark-app-mark.png','ark-authoring-atlas-v1.png','ark-detail-atlas-v1.png','ark-domain-atlas-v1.png','ark-tool-atlas-v1.png','ark-wordmark.png')
$rows=@();$buffers=@();[int64]$offset=0
foreach($name in $names){
 $path=Join-Path $SourceDirectory $name
 if((Get-Item -LiteralPath $path).Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Linked resources are not accepted.'}
 $bytes=[IO.File]::ReadAllBytes($path)
 $rows+=,[ordered]@{name=$name;offset=$offset;size=$bytes.Length;sha256=(Get-FileHash -LiteralPath $path).Hash.ToLowerInvariant();media_type='image/png'}
 $buffers+=,$bytes;$offset+=$bytes.Length
}
$index=[Text.Encoding]::UTF8.GetBytes(([ordered]@{format='ark.resource-library.v1';entries=$rows} | ConvertTo-Json -Depth 5 -Compress))
$destination=[IO.Path]::GetFullPath($OutputPath);[IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($destination))|Out-Null
$temp=$destination+'.'+[guid]::NewGuid().ToString('N')+'.tmp'
try{
 $stream=[IO.File]::Create($temp);$writer=New-Object IO.BinaryWriter($stream)
 try{$writer.Write([Text.Encoding]::ASCII.GetBytes('ARKLIB01'));$writer.Write([uint32]$index.Length);$writer.Write($index);foreach($buffer in $buffers){$writer.Write([byte[]]$buffer)}}finally{$writer.Dispose()}
 if((Test-Path -LiteralPath $destination) -and (Get-FileHash -LiteralPath $destination).Hash -eq (Get-FileHash -LiteralPath $temp).Hash){Remove-Item -LiteralPath $temp}
 elseif(Test-Path -LiteralPath $destination){[IO.File]::Replace($temp,$destination,$null)}else{[IO.File]::Move($temp,$destination)}
 Write-Output ('ARK resource library: '+$destination+'; entries='+$rows.Count+'; original compressed bytes='+$offset)
}finally{if(Test-Path -LiteralPath $temp){Remove-Item -LiteralPath $temp}}
