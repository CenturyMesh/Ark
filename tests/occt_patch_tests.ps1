param([string]$VendorSourceDirectory,[switch]$RequirePinned)
$ErrorActionPreference='Stop'
$repository=Split-Path $PSScriptRoot -Parent
$helper=Join-Path $repository 'scripts/ApplyOcctPatch.cmake'
$fixture=Join-Path $repository ('out/private/occt patch tests '+[guid]::NewGuid().ToString('N'))
$definitionDirectory=Join-Path $fixture definition
$source=Join-Path $fixture source
$utf8=[Text.UTF8Encoding]::new($false)
$checks=0
function Check($ok,[string]$why){if(-not $ok){throw $why};$script:checks++}
function Write-Test([string]$path,[string]$text){[IO.Directory]::CreateDirectory((Split-Path $path -Parent))|Out-Null;[IO.File]::WriteAllText($path,$text,$utf8)}
function Hash([string]$path){(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()}
function Invoke-Patch([string]$mode='APPLY',[string]$directory=$source,[string]$manifest=(Join-Path $definitionDirectory patchset.json),[switch]$AllowCandidate,[string]$Install,[string]$Notice,[string]$Runtime,[string]$BuildKind){
    $args=@("-DARK_OCCT_MODE=$mode","-DARK_OCCT_SOURCE=$directory","-DARK_OCCT_PATCHSET=$manifest","-DARK_OCCT_ALLOW_CANDIDATE=$([bool]$AllowCandidate)")
    if($Install){$args+=@("-DARK_OCCT_INSTALL=$Install")};if($Notice){$args+=@("-DARK_OCCT_NOTICE=$Notice")}
    if($Runtime){$args+=@("-DARK_OCCT_RUNTIME=$Runtime")}
    if($BuildKind){$args+=@("-DARK_OCCT_BUILD_KIND=$BuildKind")}
    $ErrorActionPreference='Continue';$output=@(& cmake @args -P $helper 2>&1)
    [pscustomobject]@{code=$LASTEXITCODE;text=($output -join "`n")}
}
$originals=@('alpha','beta','gamma');$names=@('one','two','three');$targets=@()
for($i=0;$i -lt 3;$i++){
    $relative='src/'+$names[$i]+'.cxx';$before=$originals[$i]+"`n";$after=$originals[$i].ToUpperInvariant()+"`n";Write-Test (Join-Path $source $relative) $before
    $beforeFile=Join-Path $fixture ('hash-before-'+$i);$afterFile=Join-Path $fixture ('hash-after-'+$i);Write-Test $beforeFile $before;Write-Test $afterFile $after
    $targets+=@{path=$relative;source_sha256=(Hash $beforeFile);patched_sha256=(Hash $afterFile)}
}
$patch1="--- a/src/one.cxx`n+++ b/src/one.cxx`n@@ -1 +1 @@`n-alpha`n+ALPHA`n"
$patch2="--- a/src/two.cxx`n+++ b/src/two.cxx`n@@ -1 +1 @@`n-beta`n+BETA`n--- a/src/three.cxx`n+++ b/src/three.cxx`n@@ -1 +1 @@`n-gamma`n+GAMMA`n"
Write-Test (Join-Path $definitionDirectory one.patch) $patch1;Write-Test (Join-Path $definitionDirectory two.patch) $patch2
$definition=@{schema='ark.occt-patchset.v1';id='ark-fixture-001';version='8.0.1';status='candidate';archive_sha256=('0'*64);scope='Synthetic fixture, not an SDK or native qualification';patches=@(@{file='one.patch';sha256=(Hash (Join-Path $definitionDirectory one.patch));targets=@($targets[0])},@{file='two.patch';sha256=(Hash (Join-Path $definitionDirectory two.patch));targets=@($targets[1],$targets[2])})}
Write-Test (Join-Path $definitionDirectory patchset.json) ($definition|ConvertTo-Json -Depth 10)
$extraPatch=$patch1+$patch2;Write-Test (Join-Path $definitionDirectory extra.patch) $extraPatch
$extraDefinition=$definition|ConvertTo-Json -Depth 10|ConvertFrom-Json
$extraDefinition.patches[0].file='extra.patch';$extraDefinition.patches[0].sha256=Hash (Join-Path $definitionDirectory extra.patch)
Write-Test (Join-Path $definitionDirectory extra.json) ($extraDefinition|ConvertTo-Json -Depth 10)
Check ((Invoke-Patch APPLY $source (Join-Path $definitionDirectory extra.json)).code -ne 0 -and (Hash (Join-Path $source $targets[0].path)) -ceq $targets[0].source_sha256) 'A patch with undeclared paths rejects before changing any source bytes'
$applied=Invoke-Patch;Check ($applied.code -eq 0) $applied.text
foreach($target in $targets){Check ((Hash (Join-Path $source $target.path)) -ceq $target.patched_sha256) 'Every file of both patches has its exact declared final bytes'}
$stamp=Join-Path $source '.ark-occt-patchset.json';$stampHash=Hash $stamp
$repeated=Invoke-Patch;Check ($repeated.code -eq 0 -and (Hash $stamp) -ceq $stampHash) 'Repeated application is idempotent and does not rewrite the source stamp'
Check ((Invoke-Patch VERIFY).code -eq 0) 'Exact patched source verifies without applying again'
Push-Location (Join-Path $repository out)
try{Check ((Invoke-Patch VERIFY).code -eq 0) 'Patch path verification is independent of a nested repository working directory'}finally{Pop-Location}
$bad=Join-Path $fixture bad-source
for($i=0;$i -lt 3;$i++){Write-Test (Join-Path $bad $targets[$i].path) ($originals[$i]+"`n")}
Add-Content -LiteralPath (Join-Path $bad $targets[2].path) -Value 'unexpected source bytes'
$badHash=Hash (Join-Path $bad $targets[2].path)
Check ((Invoke-Patch APPLY $bad).code -ne 0 -and (Hash (Join-Path $bad $targets[0].path)) -ceq $targets[0].source_sha256 -and (Hash (Join-Path $bad $targets[2].path)) -ceq $badHash) 'A bad later target rejects before any earlier target is changed'
Add-Content -LiteralPath (Join-Path $definitionDirectory one.patch) -Value 'unexpected patch bytes'
Check ((Invoke-Patch).code -ne 0) 'Patch-file SHA mismatch rejects explicitly'
Write-Test (Join-Path $definitionDirectory one.patch) $patch1
$changed=Join-Path $definitionDirectory changed.json;$definition.scope='Changed definition cannot reuse a previous patchset stamp';Write-Test $changed ($definition|ConvertTo-Json -Depth 10)
Check ((Invoke-Patch APPLY $source $changed).code -ne 0 -and (Hash $stamp) -ceq $stampHash) 'A changed manifest cannot silently reuse an existing source stamp'
Check ((Invoke-Patch BUILD_POLICY).code -ne 0 -and (Invoke-Patch BUILD_POLICY -AllowCandidate).code -eq 0) 'Candidate SDK preparation is explicit and public build policy stays blocked'
$sdk=Join-Path $fixture sdk;[IO.Directory]::CreateDirectory($sdk)|Out-Null;Copy-Item -LiteralPath $stamp -Destination $sdk
Write-Test (Join-Path $sdk 'bin/TKOffset.dll') 'synthetic offset SDK bytes'
Write-Test (Join-Path $sdk 'bin/TKGeomBase.dll') 'synthetic geometry SDK bytes'
Write-Test (Join-Path $sdk 'lib/libTKMath.so.8.0.1') 'synthetic Linux SDK bytes'
Check ((Invoke-Patch VERIFY_INSTALLED -AllowCandidate -Install $sdk).code -ne 0) 'A source stamp alone does not establish an installed SDK build inventory'
Check ((Invoke-Patch RECORD_BUILD -AllowCandidate -Install $sdk).code -ne 0) 'Inventory generation requires an explicit declared build route'
Check ((Invoke-Patch RECORD_BUILD -AllowCandidate -Install $sdk -BuildKind local-sdk-overlay).code -eq 0) 'An explicit overlay declaration inventories every synthetic shared library'
$buildDeclaration=Get-Content -LiteralPath (Join-Path $sdk '.ark-occt-build.json') -Raw|ConvertFrom-Json
Check ($buildDeclaration.schema -ceq 'ark.occt-sdk-build.v2' -and $buildDeclaration.kind -ceq 'local-sdk-overlay' -and $buildDeclaration.runtime_libraries.Count -eq 3) 'SDK inventory includes TKGeomBase, TKOffset and versioned Linux libraries without claiming a clean rebuild'
Check ((Invoke-Patch VERIFY_INSTALLED -AllowCandidate -Install $sdk).code -eq 0) 'Installed source stamp is checked against the exact patchset definition'
$runtime=Join-Path $fixture runtime;[IO.Directory]::CreateDirectory($runtime)|Out-Null
Copy-Item -LiteralPath (Join-Path $sdk 'bin/TKGeomBase.dll') -Destination $runtime
Check ((Invoke-Patch VERIFY_RUNTIME -AllowCandidate -Install $sdk -Runtime $runtime).code -eq 0) 'An exact runtime subset matches the selected SDK inventory'
$buildRecord=Join-Path $sdk '.ark-occt-build.json';$validBuildBytes=[IO.File]::ReadAllText($buildRecord)
$kindNotice=Join-Path $fixture 'invalid-kind-notice.txt'
foreach($invalidKind in @($null,'invented-clean-source',17)){
    $invalidBuild=$validBuildBytes|ConvertFrom-Json
    if($null -eq $invalidKind){$invalidBuild.PSObject.Properties.Remove('kind')}else{$invalidBuild.kind=$invalidKind}
    Write-Test $buildRecord ($invalidBuild|ConvertTo-Json -Depth 10)
    Check ((Invoke-Patch VERIFY_INSTALLED -AllowCandidate -Install $sdk).code -ne 0) 'An absent, unsupported or non-string build kind rejects installed-SDK verification'
    Check ((Invoke-Patch VERIFY_RUNTIME -AllowCandidate -Install $sdk -Runtime $runtime).code -ne 0) 'An invalid build kind cannot attest packaged runtime libraries'
    Check ((Invoke-Patch NOTICE -AllowCandidate -Install $sdk -Notice $kindNotice).code -ne 0 -and -not (Test-Path -LiteralPath $kindNotice)) 'An invalid build kind rejects before a corresponding-source notice is generated'
}
$cleanBuild=$validBuildBytes|ConvertFrom-Json;$cleanBuild.kind='clean-source-rebuild';Write-Test $buildRecord ($cleanBuild|ConvertTo-Json -Depth 10)
Check ((Invoke-Patch VERIFY_INSTALLED -AllowCandidate -Install $sdk).code -eq 0) 'The reader accepts an explicit supported clean-source declaration without inferring that route'
Write-Test $buildRecord $validBuildBytes
Write-Test (Join-Path $runtime 'TKGeomBase.dll') 'different runtime geometry bytes'
Check ((Invoke-Patch VERIFY_RUNTIME -AllowCandidate -Install $sdk -Runtime $runtime).code -ne 0) 'A different runtime DLL rejects despite the matching SDK source stamp'
Copy-Item -LiteralPath (Join-Path $sdk 'bin/TKGeomBase.dll') -Destination $runtime -Force
Write-Test (Join-Path $runtime 'TKUnknown.dll') 'unlisted runtime toolkit'
Check ((Invoke-Patch VERIFY_RUNTIME -AllowCandidate -Install $sdk -Runtime $runtime).code -ne 0) 'A runtime toolkit absent from the SDK rejects'
Remove-Item -LiteralPath (Join-Path $runtime 'TKUnknown.dll')
Write-Test (Join-Path $sdk 'bin/TKOffset.dll') 'changed installed offset SDK bytes'
Check ((Invoke-Patch VERIFY_INSTALLED -AllowCandidate -Install $sdk).code -ne 0) 'A changed installed SDK DLL rejects against its complete build inventory'
Write-Test (Join-Path $sdk 'bin/TKOffset.dll') 'synthetic offset SDK bytes'
$notice=Join-Path $fixture 'package/licenses/OCCT-ARK-patches.txt';[IO.Directory]::CreateDirectory((Split-Path $notice -Parent))|Out-Null
Check ((Invoke-Patch NOTICE -Install $sdk -Notice $notice).code -ne 0) 'Candidate source changes cannot be packaged by default'
$generated=Invoke-Patch NOTICE -AllowCandidate -Install $sdk -Notice $notice;Check ($generated.code -eq 0) $generated.text
$noticeText=Get-Content -LiteralPath $notice -Raw;Check ($noticeText.Contains($patch1) -and $noticeText.Contains($patch2) -and $noticeText.Contains('not attest a compiled binary') -and $noticeText -match '"kind"\s*:\s*"local-sdk-overlay"') 'Source notice contains exact diffs and preserves the overlay declaration without inferring a clean rebuild'
Import-Module (Join-Path $repository 'scripts/Ark.ReleaseEvidence.psm1') -Force
$inventory=Get-ArkPackageInventory (Join-Path $fixture package);Check ($inventory.files.Count -eq 1 -and $inventory.files[0].path -ceq 'licenses/OCCT-ARK-patches.txt') 'The exact patch notice is accepted and hashed by the package allowlist'
$productManifest=Join-Path $repository 'third_party/occt/patches/8.0.1/patchset.json';$product=Get-Content -LiteralPath $productManifest -Raw|ConvertFrom-Json
$pinned='pending-native-owner'
if(@($product.patches).Count -and ($RequirePinned -or $VendorSourceDirectory)){
    if(-not $VendorSourceDirectory){$common=(& git -C $repository rev-parse --path-format=absolute --git-common-dir).Trim();$VendorSourceDirectory=Join-Path (Split-Path $common -Parent) 'out/deps/occt/cache/OCCT-8_0_1'}
    $pinnedSource=Join-Path $fixture pinned-source;$originalHashes=@{}
    foreach($patch in $product.patches){foreach($target in $patch.targets){$original=Join-Path $VendorSourceDirectory $target.path;Check ((Hash $original) -ceq $target.source_sha256) 'Pinned real vendor target matches the original expected SHA';$originalHashes[$original]=Hash $original;$destination=Join-Path $pinnedSource $target.path;[IO.Directory]::CreateDirectory((Split-Path $destination -Parent))|Out-Null;Copy-Item -LiteralPath $original -Destination $destination}}
    $actual=Invoke-Patch APPLY $pinnedSource $productManifest;Check ($actual.code -eq 0) $actual.text
    Check ((Invoke-Patch APPLY $pinnedSource $productManifest).code -eq 0) 'Real pinned vendor-file application is idempotent'
    $rejectedSource=Join-Path $fixture rejected-pinned-source;$lastTarget=''
    foreach($patch in $product.patches){foreach($target in $patch.targets){$original=Join-Path $VendorSourceDirectory $target.path;$lastTarget=Join-Path $rejectedSource $target.path;[IO.Directory]::CreateDirectory((Split-Path $lastTarget -Parent))|Out-Null;Copy-Item -LiteralPath $original -Destination $lastTarget}}
    Add-Content -LiteralPath $lastTarget -Value '/* Synthetic pinned-target hash rejection fixture */';$rejectedHash=Hash $lastTarget
    Check ((Invoke-Patch APPLY $rejectedSource $productManifest).code -ne 0 -and (Hash $lastTarget) -ceq $rejectedHash) 'A modified real pinned target rejects without overwriting its unexpected bytes'
    foreach($entry in $originalHashes.GetEnumerator()){Check ((Hash $entry.Key) -ceq $entry.Value) 'Original vendor/SDK source remained byte-identical'}
    $pinned='exact-targets-applied-and-repeated; no SDK compilation or geometry qualification'
}elseif($RequirePinned){throw 'Pinned application is pending the native owner final patch list; no real-source success is claimed'}elseif(@($product.patches).Count){$pinned='not-requested'}
Write-Output "$checks OCCT patch mechanism checks passed; pinned=$pinned; fixture=$fixture; paid_model_calls=0"
