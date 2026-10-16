$ErrorActionPreference='Stop'
$repository=Split-Path $PSScriptRoot -Parent
Import-Module (Join-Path $repository 'scripts/Ark.ReleaseEvidence.psm1') -Force
$workspace=Join-Path ([IO.Path]::GetTempPath()) ('ark-release-evidence-'+[guid]::NewGuid().ToString('N'))
[IO.Directory]::CreateDirectory($workspace) | Out-Null
function Assert($condition,[string]$message){if(-not $condition){throw $message}}
function Reject([scriptblock]$action,[string]$message){$rejected=$false;try{&$action | Out-Null}catch{$rejected=$true};Assert $rejected $message}
try{
    $package=Join-Path $workspace 'package';[IO.Directory]::CreateDirectory((Join-Path $package 'licenses')) | Out-Null
    $release=Get-Content -LiteralPath (Join-Path $repository 'release.json') -Raw | ConvertFrom-Json
    $metadata=@{product='ARK';version=$release.version;iteration_code=$release.code;sequence=$release.sequence;phase=$release.phase}
    $metadata | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $package 'app-version.json') -Encoding UTF8
    foreach($name in @('LICENSE','NOTICE','licenses/mapbox-earcut-ISC.txt','licenses/stb-image-LICENSE.txt','licenses/sqlite-provenance.txt','licenses/LICENSE_LGPL_21.txt','licenses/OCCT_LGPL_EXCEPTION.txt')){[IO.File]::WriteAllText((Join-Path $package $name),'synthetic test notice')}
    [IO.File]::WriteAllText((Join-Path $package 'ark-cli.exe'),'synthetic non-runtime payload')
    $output=Join-Path $package 'release-evidence';$result=New-ArkReleaseEvidence $repository $package $output
    Assert ($result.iteration_code -eq $release.code -and $result.payload_files -eq 9) 'Identity and complete payload inventory'
    $sbom=Get-Content -LiteralPath (Join-Path $output 'sbom.cdx.json') -Raw | ConvertFrom-Json
    Assert ($sbom.bomFormat -eq 'CycloneDX' -and $sbom.components.Count -eq 4) 'SBOM contains the four direct source dependencies'
    foreach($name in @('Install-ARK.ps1','Install-ARK.cmd','GETTING_STARTED.txt')) { [IO.File]::WriteAllText((Join-Path $package $name),'synthetic reviewed distribution entrypoint') }
    $distribution=Get-ArkPackageInventory $package
    Assert ($distribution.files.Count -eq 12) 'Current portable installer and getting-started payloads are included in the hash census'
    foreach($name in @('Install-ARK.ps1','Install-ARK.cmd','GETTING_STARTED.txt')) { Remove-Item -LiteralPath (Join-Path $package $name) }
    $uiNames=@('ark-app-mark.png','ark-authoring-atlas-v1.png','ark-detail-atlas-v1.png','ark-domain-atlas-v1.png','ark-tool-atlas-v1.png','ark-wordmark.png','ark-app.ico','README.md')
    $ui=Join-Path $package 'assets/ui';[IO.Directory]::CreateDirectory($ui)|Out-Null
    foreach($name in $uiNames) { [IO.File]::WriteAllText((Join-Path $ui $name),'synthetic reviewed UI asset') }
    Assert ((Get-ArkPackageInventory $package).files.Count -eq 17) 'All eight reviewed current UI payload names are inventoried'
    foreach($name in @('unknown-atlas.png','private-preview.png','private-icon.ico')) {
        $unknown=Join-Path $ui $name;[IO.File]::WriteAllText($unknown,'synthetic unapproved asset');Reject {Get-ArkPackageInventory $package} "Unknown UI asset rejects: $name";Remove-Item -LiteralPath $unknown
    }
    foreach($name in $uiNames) { Remove-Item -LiteralPath (Join-Path $ui $name) }
    foreach($name in @('libTKernel.dll','libc++.dll','libunwind.dll','libwinpthread-1.dll','unrecognized.dll')) { [IO.File]::WriteAllText((Join-Path $package $name),'synthetic runtime bytes') }
    $runtimeResult=New-ArkReleaseEvidence $repository $package $output
    $runtimeSbom=Get-Content -LiteralPath (Join-Path $output 'sbom.cdx.json') -Raw | ConvertFrom-Json
    Assert ($runtimeResult.runtime_components -eq 5 -and $runtimeSbom.components.Count -eq 9) 'Every bundled DLL is represented in the SBOM'
    $unknown=@($runtimeSbom.components | Where-Object {$_.'bom-ref' -eq 'runtime:unrecognized.dll'})[0]
    Assert (@($unknown.properties | Where-Object {$_.name -eq 'ark:runtime-family'})[0].value -eq 'Unclassified bundled runtime') 'Unknown runtime family stays explicit'
    Assert (@($unknown.properties | Where-Object {$_.name -eq 'ark:version-scope'})[0].value -match 'Not determined') 'Missing binary version cannot become a guessed source version'
    $occtRuntime=@($runtimeSbom.components | Where-Object {$_.'bom-ref' -eq 'runtime:libTKernel.dll'})[0]
    Assert (-not $occtRuntime.PSObject.Properties['version']) 'An OCCT filename does not prove its binary version'
    Assert (@($occtRuntime.properties | Where-Object {$_.name -eq 'ark:declared-source-version'})[0].value -eq '8.0.1') 'Declared pinned source association remains distinct from binary evidence'
    foreach($name in @('libTKernel.dll','libc++.dll','libunwind.dll','libwinpthread-1.dll','unrecognized.dll')) { Remove-Item -LiteralPath (Join-Path $package $name) }
    New-ArkReleaseEvidence $repository $package $output | Out-Null
    $inventory=Get-ArkPackageInventory $package
    Assert ($inventory.files.Count -eq 9) 'Evidence is excluded from its own hash census'
    $exe=@($inventory.files | Where-Object {$_.path -eq 'ark-cli.exe'})[0]
    Assert ($exe.sha256 -eq (Get-FileHash -LiteralPath (Join-Path $package 'ark-cli.exe') -Algorithm SHA256).Hash.ToLowerInvariant()) 'Per-file digest is observed bytes'
    foreach($name in @('customer.ark','auth.json','.env','secret.pem','session.arkjson','surprise.txt')){
        $path=Join-Path $package $name;[IO.File]::WriteAllText($path,'synthetic forbidden file');Reject {Get-ArkPackageInventory $package} "Rejects $name";Remove-Item -LiteralPath $path
    }
    Remove-Item -LiteralPath (Join-Path $package 'NOTICE');Reject {New-ArkReleaseEvidence $repository $package $output} 'Missing mandatory notice rejects';[IO.File]::WriteAllText((Join-Path $package 'NOTICE'),'synthetic notice')
    $metadata.version='999.0.0';$metadata | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $package 'app-version.json') -Encoding UTF8;Reject {New-ArkReleaseEvidence $repository $package $output} 'Mismatched runtime identity rejects'
    New-ArkReleaseEvidence $repository $package $output -UseRuntimeIdentity | Out-Null
    $retained=Get-Content -LiteralPath (Join-Path $output 'source-inventory.json') -Raw | ConvertFrom-Json
    Assert ($retained.repository_release_identity.version -eq $release.version -and $retained.packaged_runtime_identity.version -eq '999.0.0' -and $retained.runtime_identity_mode -match 'does not attest') 'Personal packaging preserves compiled identity and distinguishes packaging source'
    $metadata.sequence=[double]$release.sequence+0.2;$metadata | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $package 'app-version.json') -Encoding UTF8;Reject {New-ArkReleaseEvidence $repository $package $output -UseRuntimeIdentity} 'Fractional sequence cannot be rounded to a valid personal runtime identity'
    $archive=Join-Path $workspace 'artifact.zip';[IO.File]::WriteAllText($archive,'synthetic archive');$receiptPath=$archive+'.provenance.json';New-ArkArtifactReceipt $archive $output $receiptPath
    $receipt=Get-Content -LiteralPath $receiptPath -Raw | ConvertFrom-Json
    Assert ($receipt.publisher_signature -eq 'absent' -and $receipt.evidence.Count -eq 3 -and $receipt.artifact.sha256 -eq (Get-FileHash -LiteralPath $archive).Hash.ToLowerInvariant()) 'Receipt binds artifact and evidence without inventing signing'
    'Release evidence tests passed: package allowlist, credentials/design rejection, notices, identity, SBOM, hashes, provenance.'
}finally{
    $resolved=[IO.Path]::GetFullPath($workspace);$temp=[IO.Path]::GetFullPath([IO.Path]::GetTempPath());if(-not $resolved.StartsWith($temp,[StringComparison]::OrdinalIgnoreCase)){throw 'Unsafe test cleanup path'};Remove-Item -LiteralPath $workspace -Recurse -Force
}
