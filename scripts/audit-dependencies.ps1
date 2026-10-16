param(
    [Parameter(Mandatory=$true)][string]$SbomPath,
    [Parameter(Mandatory=$true)][string]$OutputPath,
    [switch]$FailOnFinding
)
$ErrorActionPreference='Stop'
Import-Module (Join-Path $PSScriptRoot 'Ark.DependencyAudit.psm1') -Force
$sbomBytes = [IO.File]::ReadAllBytes((Resolve-Path -LiteralPath $SbomPath).Path)
$decoder = New-Object Text.UTF8Encoding($false,$true)
$sbomText = $decoder.GetString($sbomBytes)
if ($sbomText.Length -and $sbomText[0] -eq [char]0xfeff) { $sbomText = $sbomText.Substring(1) }
$sbom = $sbomText | ConvertFrom-Json
$sha = [Security.Cryptography.SHA256]::Create()
try { $sbomHash = -join ($sha.ComputeHash($sbomBytes) | ForEach-Object { $_.ToString('x2') }) } finally { $sha.Dispose() }
$result = Invoke-ArkDependencyAudit $sbom
$result | Add-Member -NotePropertyName sbom_sha256 -NotePropertyValue $sbomHash
$parent = Split-Path ([IO.Path]::GetFullPath($OutputPath)) -Parent
[IO.Directory]::CreateDirectory($parent) | Out-Null
$bytes = [Text.Encoding]::UTF8.GetBytes(($result | ConvertTo-Json -Depth 60) + "`n")
$stream = [IO.File]::Open($OutputPath, 'CreateNew', 'Write', 'None')
try { $stream.Write($bytes,0,$bytes.Length) } finally { $stream.Dispose() }
Write-Output "Dependency database receipt: $OutputPath; findings=$($result.findings), failed queries=$($result.query_failures), unsupported components=$($result.unqueried_components)."
if ($result.query_failures) { exit 1 }
if ($FailOnFinding -and $result.findings) { exit 2 }
