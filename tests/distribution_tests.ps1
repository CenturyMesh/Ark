$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$repository = Split-Path $PSScriptRoot -Parent
Import-Module (Join-Path $repository 'updates/Ark.Update.psm1') -Force
$temporary = Join-Path ([IO.Path]::GetTempPath()) ('ark distribution fixture ' + [guid]::NewGuid().ToString('N'))
$checks = 0
function Assert-True([bool]$Value, [string]$Name) {
    if (-not $Value) { throw $Name }
    $script:checks++
}
function Write-Fixture([string]$Path, [string]$Value = 'fixture') {
    [IO.Directory]::CreateDirectory((Split-Path $Path -Parent)) | Out-Null
    [IO.File]::WriteAllText($Path, $Value)
}
try {
    $fixture = Join-Path $temporary 'repository'
    foreach ($path in @('scripts/package-release.ps1','scripts/ApplyOcctPatch.cmake','scripts/Ark.ReleaseEvidence.psm1','scripts/install-portable.ps1','scripts/publish-test-build.ps1','updates/Ark.Update.psm1','updates/ARK.cmd','updates/ark-update.ps1','updates/ark-launch.ps1','packaging/Install-ARK.cmd','packaging/GETTING_STARTED.txt')) {
        $target = Join-Path $fixture $path
        [IO.Directory]::CreateDirectory((Split-Path $target -Parent)) | Out-Null
        Copy-Item -LiteralPath (Join-Path $repository $path) -Destination $target
    }
    foreach ($path in @('LICENSE','NOTICE','docs/UPDATES.md','docs/BOM.md','docs/AGENT_ACTIVITY.md','docs/MEASUREMENT.md','docs/schemas/ark-bom-v1.schema.json','docs/THIRD_PARTY_NOTICES.md','third_party/mapbox/LICENSE','third_party/stb/LICENSE','third_party/stb/README.md','third_party/sqlite/README.md','third_party/sqlite/sqlite3.h','assets/ui/README.md','occt/LICENSE_LGPL_21.txt','occt/OCCT_LGPL_EXCEPTION.txt')) { Write-Fixture (Join-Path $fixture $path) }
    foreach ($path in @('third_party/mapbox/earcut.hpp','third_party/stb/stb_image.h','third_party/sqlite/sqlite3.c')) { Write-Fixture (Join-Path $fixture $path) 'synthetic source bytes for evidence fixture' }
    Write-ArkAtomicJson (Join-Path $fixture 'release.json') @{sequence=14;phase='F';code='014F';version='0.9.15'}
    & git -C $fixture init --quiet
    if ($LASTEXITCODE -ne 0) { throw 'Cannot initialize synthetic source inventory.' }
    & git -C $fixture add .
    & git -C $fixture -c user.name='ARK Fixture' -c user.email='fixture@example.invalid' -c commit.gpgSign=false commit --quiet --no-verify -m 'Synthetic distribution source'
    if ($LASTEXITCODE -ne 0) { throw 'Cannot record synthetic source inventory.' }
    $runtime = Join-Path $fixture 'runtime'
    foreach ($name in @('ark-studio.exe','ark-cli.exe','ark-mcp.exe','ark-occt-worker.exe','TKernel.dll','TKBRep.dll','TKDESTEP.dll','assets/ui/ark-wordmark.png','assets/ui/ark-tool-atlas-v1.png','assets/ui/ark-detail-atlas-v1.png','assets/ui/ark-domain-atlas-v1.png','updates/ark-update.ps1','updates/ark-launch.ps1','updates/Ark.Update.psm1')) { Write-Fixture (Join-Path $runtime $name) }
    Copy-Item -LiteralPath (Join-Path $repository 'updates/Ark.Update.psm1') -Destination (Join-Path $runtime 'updates/Ark.Update.psm1') -Force
    foreach ($name in @('ark-app-mark.png','ark-authoring-atlas-v1.png','ark-app.ico','README.md')) { Write-Fixture (Join-Path $runtime ('assets/ui/' + $name)) 'synthetic reviewed UI asset' }
    Write-ArkAtomicJson (Join-Path $runtime 'app-version.json') @{schema=1; product='ARK'; version='0.9.15'; iteration_code='014F'; sequence=14; phase='F'; platform='windows-x64'; entrypoint='ark-studio.exe'}
    Write-Fixture (Join-Path $runtime 'private-design.ark') 'never ship this document'
    $msvc = Join-Path $fixture 'msvc'
    foreach ($name in @('vcruntime140.dll','vcruntime140_1.dll','msvcp140.dll','concrt140.dll')) { Write-Fixture (Join-Path $msvc $name) }
    # Freeze a wholly synthetic verified patch/SDK; this does not qualify OCCT.
    $patchDirectory=Join-Path $fixture 'third_party/occt/patches/8.0.1'
    $patch=Join-Path $patchDirectory 'fixture.patch'
    Write-Fixture $patch "--- a/src/fixture.cxx`n+++ b/src/fixture.cxx`n@@ -1 +1 @@`n-before`n+after`n"
    $patchManifest=Join-Path $patchDirectory 'patchset.json'
    Write-ArkAtomicJson $patchManifest @{schema='ark.occt-patchset.v1';id='ark-distribution-fixture';version='8.0.1';status='verified-in-scope';archive_sha256=('0'*64);scope='Synthetic distribution test only';patches=@(@{file='fixture.patch';sha256=(Get-FileHash -LiteralPath $patch).Hash.ToLowerInvariant();targets=@(@{path='src/fixture.cxx';source_sha256=('1'*64);patched_sha256=('2'*64)})})}
    $sdk=Join-Path $fixture 'out/deps/occt/install-msvc-ark-offset-001'
    [IO.Directory]::CreateDirectory($sdk)|Out-Null
    Copy-Item -LiteralPath $patchManifest -Destination (Join-Path $sdk '.ark-occt-patchset.json')
    foreach($name in @('TKernel.dll','TKBRep.dll','TKDESTEP.dll')){Write-Fixture (Join-Path $sdk ('bin/'+$name))}
    & cmake -DARK_OCCT_MODE=RECORD_BUILD -DARK_OCCT_BUILD_KIND=local-sdk-overlay "-DARK_OCCT_INSTALL=$sdk" -P (Join-Path $fixture 'scripts/ApplyOcctPatch.cmake')
    if($LASTEXITCODE -ne 0){throw 'Cannot record synthetic SDK distribution inventory'}
    $output = Join-Path $fixture 'output'
    & (Join-Path $fixture 'scripts/package-release.ps1') -RuntimeDirectory $runtime -OutputDirectory $output -MsvcRuntimeDirectory $msvc -OcctSourceDirectory (Join-Path $fixture 'occt') -SkipUpdateManifest
    Assert-True (-not (Test-Path -LiteralPath (Join-Path $output 'ark-update.json'))) 'Private downloads must not advertise the placeholder update feed.'
    $expanded = Join-Path $fixture 'expanded'
    Expand-ArkPackage (Join-Path $output 'ark-windows-x64-0.9.15.zip') $expanded
    Assert-True ((Get-ArkRuntimeVersion $expanded) -ceq '0.9.15') 'Packaged runtime must validate.'
    foreach ($name in @('Install-ARK.ps1','Install-ARK.cmd','GETTING_STARTED.txt','vcruntime140.dll','vcruntime140_1.dll','msvcp140.dll','concrt140.dll','licenses/LICENSE_LGPL_21.txt','licenses/OCCT_LGPL_EXCEPTION.txt','licenses/mapbox-earcut-ISC.txt','licenses/THIRD_PARTY_NOTICES.md','assets/contracts/BOM.md')) { Assert-True (Test-Path -LiteralPath (Join-Path $expanded $name)) "Missing packaged file: $name" }
    Assert-True (-not (Test-Path -LiteralPath (Join-Path $expanded 'private-design.ark'))) 'Local CAD documents must be excluded.'
    foreach ($name in @('ark-app-mark.png','ark-authoring-atlas-v1.png','ark-app.ico','README.md')) { Assert-True (Test-Path -LiteralPath (Join-Path $expanded ('assets/ui/' + $name))) "Reviewed UI asset missing: $name" }
    Assert-True (Test-Path -LiteralPath (Join-Path $expanded 'release-evidence/sbom.cdx.json')) 'Portable package contains its SBOM.'
    Assert-True (Test-Path -LiteralPath (Join-Path $output 'ark-windows-x64-0.9.15.zip.provenance.json')) 'Portable archive has an integrity receipt.'
    Assert-True (Test-Path -LiteralPath (Join-Path $expanded 'licenses/OCCT-ARK-patches.txt')) 'Portable package preserves the exact modified-source notice and SDK declaration.'
    Write-Fixture (Join-Path $runtime 'TKBRep.dll') 'different runtime bytes'
    $rejectedOutput=Join-Path $fixture 'rejected runtime output';$rejected=$false
    try { & (Join-Path $fixture 'scripts/package-release.ps1') -RuntimeDirectory $runtime -OutputDirectory $rejectedOutput -MsvcRuntimeDirectory $msvc -OcctSourceDirectory (Join-Path $fixture 'occt') -SkipUpdateManifest } catch { $rejected=$true }
    Assert-True ($rejected -and -not (Test-Path -LiteralPath (Join-Path $rejectedOutput 'ark-windows-x64-0.9.15.zip'))) 'Mismatched runtime DLL rejects before an archive is published.'
    Write-Fixture (Join-Path $runtime 'TKBRep.dll')
    Move-Item -LiteralPath $patchManifest -Destination ($patchManifest+'.saved')
    $missingOutput=Join-Path $fixture 'missing manifest output';$rejected=$false
    try { & (Join-Path $fixture 'scripts/package-release.ps1') -RuntimeDirectory $runtime -OutputDirectory $missingOutput -MsvcRuntimeDirectory $msvc -OcctSourceDirectory (Join-Path $fixture 'occt') -SkipUpdateManifest } catch { $rejected=$true }
    Assert-True ($rejected -and -not (Test-Path -LiteralPath (Join-Path $missingOutput 'ark-windows-x64-0.9.15.zip'))) 'A missing source manifest rejects rather than bypassing the SDK gate.'
    Move-Item -LiteralPath ($patchManifest+'.saved') -Destination $patchManifest
    $installRoot = Join-Path $fixture 'installation with spaces'
    $document = Join-Path $fixture 'Documents/untouched.ark'
    Write-Fixture $document 'preserve user drawing'
    & (Join-Path $expanded 'Install-ARK.ps1') -InstallRoot $installRoot
    Assert-True ((Get-ArkState $installRoot).current -ceq '0.9.15') 'Standalone install must select its runtime.'
    Assert-ArkInstalledRuntime $installRoot '0.9.15' | Out-Null
    $checks++
    Assert-True ((Get-Content -LiteralPath $document -Raw) -ceq 'preserve user drawing') 'Installation must preserve external drawings.'
    & (Join-Path $expanded 'Install-ARK.ps1') -InstallRoot $installRoot
    Assert-ArkInstalledRuntime $installRoot '0.9.15' | Out-Null
    $checks++
    # A personal package may retain binaries built before the packaging checkout.
    Write-ArkAtomicJson (Join-Path $fixture 'release.json') @{sequence=29;phase='B';code='029B';version='0.9.30'}
    $testOutput = Join-Path $fixture 'personal test output'
    & (Join-Path $fixture 'scripts/publish-test-build.ps1') -RuntimeDirectory $runtime -OutputDirectory $testOutput -MsvcRuntimeDirectory $msvc -OcctSourceDirectory (Join-Path $fixture 'occt')
    Assert-True (Test-Path -LiteralPath (Join-Path $testOutput 'ark-windows-x64-0.9.15.zip')) 'Personal test packaging must reuse the existing runtime.'
    Assert-True (Test-Path -LiteralPath (Join-Path $testOutput 'SHA256SUMS.txt')) 'Personal test package must have checksums.'
    Assert-True (-not (Test-Path -LiteralPath (Join-Path $testOutput 'ark-update.json'))) 'Personal test packaging must not publish an Internet update feed.'
    $llvm = Join-Path $fixture 'llvm-runtime'
    Copy-Item -LiteralPath $runtime -Destination $llvm -Recurse
    foreach ($name in @('TKernel.dll','TKBRep.dll','TKDESTEP.dll')) { Move-Item -LiteralPath (Join-Path $llvm $name) -Destination (Join-Path $llvm ('lib' + $name)) }
    foreach ($name in @('libc++.dll','libunwind.dll')) { Write-Fixture (Join-Path $llvm $name) }
    $llvmSdk=Join-Path $fixture 'llvm-sdk';[IO.Directory]::CreateDirectory($llvmSdk)|Out-Null
    Copy-Item -LiteralPath $patchManifest -Destination (Join-Path $llvmSdk '.ark-occt-patchset.json')
    foreach($name in @('libTKernel.dll','libTKBRep.dll','libTKDESTEP.dll')){Write-Fixture (Join-Path $llvmSdk ('bin/'+$name))}
    & cmake -DARK_OCCT_MODE=RECORD_BUILD -DARK_OCCT_BUILD_KIND=local-sdk-overlay "-DARK_OCCT_INSTALL=$llvmSdk" -P (Join-Path $fixture 'scripts/ApplyOcctPatch.cmake')
    if($LASTEXITCODE -ne 0){throw 'Cannot record synthetic LLVM SDK inventory'}
    $compiler = Join-Path $fixture 'compiler'
    Write-Fixture (Join-Path $compiler 'LICENSE.TXT') 'synthetic matching compiler notice'
    Write-Fixture (Join-Path $compiler 'x86_64-w64-mingw32/share/mingw32/COPYING.MinGW-w64-runtime') 'synthetic matching runtime notice'
    $llvmOutput = Join-Path $fixture 'llvm output'
    & (Join-Path $fixture 'scripts/publish-test-build.ps1') -RuntimeDirectory $llvm -OutputDirectory $llvmOutput -CompilerDirectory $compiler -OcctSourceDirectory (Join-Path $fixture 'occt') -OcctSdkDirectory $llvmSdk
    $llvmExpanded = Join-Path $fixture 'llvm expanded'
    Expand-ArkPackage (Join-Path $llvmOutput 'ark-windows-x64-0.9.15.zip') $llvmExpanded
    foreach ($name in @('libTKernel.dll','libTKBRep.dll','libTKDESTEP.dll','libc++.dll','libunwind.dll','licenses/LLVM-Apache-exception.txt','licenses/COPYING.MinGW-w64-runtime.txt')) { Assert-True (Test-Path -LiteralPath (Join-Path $llvmExpanded $name)) "Missing LLVM payload/notice: $name" }
    Assert-True (-not (Test-Path -LiteralPath (Join-Path $llvmExpanded 'vcruntime140.dll'))) 'LLVM packaging does not introduce an unrelated MSVC runtime.'
    $sourceEvidence = Read-ArkJson (Join-Path $llvmExpanded 'release-evidence/source-inventory.json') 8MB
    Assert-True ($sourceEvidence.repository_release_identity.code -ceq '029B' -and $sourceEvidence.packaged_runtime_identity.code -ceq '014F') 'Retained runtime and packaging source identities remain distinct.'
    Assert-True (Test-Path -LiteralPath (Join-Path $llvmOutput 'ark-windows-x64-0.9.15.zip.provenance.json')) 'LLVM personal package retains its archive receipt.'
    Write-Output "Distribution fixture: $checks checks passed. Fake binaries; no native execution or installer compilation claimed."
} finally {
    if (Test-Path -LiteralPath $temporary) { Remove-Item -LiteralPath (Assert-ArkChildPath ([IO.Path]::GetTempPath()) $temporary) -Recurse -Force }
}
