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
    foreach ($path in @('scripts/package-release.ps1','scripts/install-portable.ps1','scripts/publish-test-build.ps1','updates/Ark.Update.psm1','updates/ARK.cmd','packaging/Install-ARK.cmd','packaging/GETTING_STARTED.txt')) {
        $target = Join-Path $fixture $path
        [IO.Directory]::CreateDirectory((Split-Path $target -Parent)) | Out-Null
        Copy-Item -LiteralPath (Join-Path $repository $path) -Destination $target
    }
    foreach ($path in @('docs/UPDATES.md','docs/BOM.md','docs/AGENT_ACTIVITY.md','docs/MEASUREMENT.md','docs/schemas/ark-bom-v1.schema.json','docs/THIRD_PARTY_NOTICES.md','third_party/mapbox/LICENSE','third_party/sqlite/README.md','third_party/sqlite/sqlite3.h','assets/ui/README.md','occt/LICENSE_LGPL_21.txt','occt/OCCT_LGPL_EXCEPTION.txt')) { Write-Fixture (Join-Path $fixture $path) }
    $runtime = Join-Path $fixture 'runtime'
    foreach ($name in @('ark-studio.exe','ark-cli.exe','ark-mcp.exe','ark-occt-worker.exe','TKernel.dll','TKBRep.dll','TKDESTEP.dll','assets/ui/ark-wordmark.png','assets/ui/ark-tool-atlas-v1.png','assets/ui/ark-detail-atlas-v1.png','assets/ui/ark-domain-atlas-v1.png','updates/ark-update.ps1','updates/ark-launch.ps1','updates/Ark.Update.psm1')) { Write-Fixture (Join-Path $runtime $name) }
    Copy-Item -LiteralPath (Join-Path $repository 'updates/Ark.Update.psm1') -Destination (Join-Path $runtime 'updates/Ark.Update.psm1') -Force
    Write-ArkAtomicJson (Join-Path $runtime 'app-version.json') @{schema=1; product='ARK'; version='0.9.15'; iteration_code='014F'; platform='windows-x64'; entrypoint='ark-studio.exe'}
    Write-Fixture (Join-Path $runtime 'private-design.ark') 'never ship this document'
    $msvc = Join-Path $fixture 'msvc'
    foreach ($name in @('vcruntime140.dll','vcruntime140_1.dll','msvcp140.dll','concrt140.dll')) { Write-Fixture (Join-Path $msvc $name) }
    $output = Join-Path $fixture 'output'
    & (Join-Path $fixture 'scripts/package-release.ps1') -RuntimeDirectory $runtime -OutputDirectory $output -MsvcRuntimeDirectory $msvc -OcctSourceDirectory (Join-Path $fixture 'occt') -SkipUpdateManifest
    Assert-True (-not (Test-Path -LiteralPath (Join-Path $output 'ark-update.json'))) 'Private downloads must not advertise the placeholder update feed.'
    $expanded = Join-Path $fixture 'expanded'
    Expand-ArkPackage (Join-Path $output 'ark-windows-x64-0.9.15.zip') $expanded
    Assert-True ((Get-ArkRuntimeVersion $expanded) -ceq '0.9.15') 'Packaged runtime must validate.'
    foreach ($name in @('Install-ARK.ps1','Install-ARK.cmd','GETTING_STARTED.txt','vcruntime140.dll','vcruntime140_1.dll','msvcp140.dll','concrt140.dll','licenses/LICENSE_LGPL_21.txt','licenses/OCCT_LGPL_EXCEPTION.txt','licenses/mapbox-earcut-ISC.txt','licenses/THIRD_PARTY_NOTICES.md','assets/contracts/BOM.md')) { Assert-True (Test-Path -LiteralPath (Join-Path $expanded $name)) "Missing packaged file: $name" }
    Assert-True (-not (Test-Path -LiteralPath (Join-Path $expanded 'private-design.ark'))) 'Local CAD documents must be excluded.'
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
    $testOutput = Join-Path $fixture 'personal test output'
    & (Join-Path $fixture 'scripts/publish-test-build.ps1') -RuntimeDirectory $runtime -OutputDirectory $testOutput -MsvcRuntimeDirectory $msvc -OcctSourceDirectory (Join-Path $fixture 'occt')
    Assert-True (Test-Path -LiteralPath (Join-Path $testOutput 'ark-windows-x64-0.9.15.zip')) 'Personal test packaging must reuse the existing runtime.'
    Assert-True (Test-Path -LiteralPath (Join-Path $testOutput 'SHA256SUMS.txt')) 'Personal test package must have checksums.'
    Assert-True (-not (Test-Path -LiteralPath (Join-Path $testOutput 'ark-update.json'))) 'Personal test packaging must not publish an Internet update feed.'
    Write-Output "Distribution fixture: $checks checks passed. Fake binaries; no native execution or installer compilation claimed."
} finally {
    if (Test-Path -LiteralPath $temporary) { Remove-Item -LiteralPath (Assert-ArkChildPath ([IO.Path]::GetTempPath()) $temporary) -Recurse -Force }
}
