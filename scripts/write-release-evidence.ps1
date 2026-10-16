param([Parameter(Mandatory=$true)][string]$PackageDirectory,[Parameter(Mandatory=$true)][string]$OutputDirectory,[string]$Archive)
$ErrorActionPreference='Stop'
Import-Module (Join-Path $PSScriptRoot 'Ark.ReleaseEvidence.psm1') -Force
$repository=Split-Path $PSScriptRoot -Parent
New-ArkReleaseEvidence $repository $PackageDirectory $OutputDirectory | ConvertTo-Json
if($Archive){New-ArkArtifactReceipt $Archive $OutputDirectory ($Archive+'.provenance.json')}
