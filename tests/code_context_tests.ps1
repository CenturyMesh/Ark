$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$script = Join-Path $repo 'scripts/code-context.ps1'
$fixture = Join-Path $repo ('out/private/code-context-tests-' + [Guid]::NewGuid().ToString('N'))
[IO.Directory]::CreateDirectory((Join-Path $fixture 'src')) | Out-Null
[IO.Directory]::CreateDirectory((Join-Path $fixture 'include')) | Out-Null
function Check($Condition,[string]$Reason) { if (-not $Condition) { throw $Reason }; $script:checks++ }
$checks = 0
& git -C $fixture init --quiet
if ($LASTEXITCODE) { throw 'Cannot initialize isolated code fixture.' }
'out/' | Set-Content -LiteralPath (Join-Path $fixture '.gitignore') -Encoding ASCII
'language.h=cpp' | Set-Content -LiteralPath (Join-Path $fixture '.graphifyrc') -Encoding ASCII
'double volume(double width);' | Set-Content -LiteralPath (Join-Path $fixture 'include/model.h') -Encoding ASCII
@'
#include "../include/model.h"
double volume(double width) { return width * width * width; }
double estimate() { return volume(3.0); }
'@ | Set-Content -LiteralPath (Join-Path $fixture 'src/model.cpp') -Encoding ASCII
'double obsolete() { return 1.0; }' | Set-Content -LiteralPath (Join-Path $fixture 'src/obsolete.cpp') -Encoding ASCII
'private CAD placeholder, never index' | Set-Content -LiteralPath (Join-Path $fixture 'src/private.ark') -Encoding ASCII
'PRIVATE_TEST_PLACEHOLDER' | Set-Content -LiteralPath (Join-Path $fixture '.env') -Encoding ASCII
$index = Join-Path $fixture 'out/context-code'
function Invoke-Context([string[]]$Extra) {
    $ErrorActionPreference = 'Continue' # Native stderr warnings are captured as data.
    $output = @(& powershell -NoProfile -ExecutionPolicy Bypass -File $script -SourceRoot $fixture @Extra 2>&1)
    [pscustomobject]@{code=$LASTEXITCODE;text=($output -join "`n")}
}
$first = Invoke-Context @('-Obsidian')
Check ($first.code -eq 0) $first.text
$manifestPath = Join-Path $index 'source-manifest.json'
$firstManifest = Get-Content -Raw -LiteralPath $manifestPath | ConvertFrom-Json
$graphPath = Join-Path $index 'index/graphify-out/graph.json'
$graph = Get-Content -Raw -LiteralPath $graphPath | ConvertFrom-Json
Check (@($graph.nodes).Count -gt 0 -and $firstManifest.backend -eq 'none' -and $firstManifest.tool -eq 'graphifyy==0.9.92') 'Pinned local code graph is populated.'
Check (-not (Test-Path -LiteralPath (Join-Path $index 'corpus/src/private.ark')) -and -not (Test-Path -LiteralPath (Join-Path $index 'corpus/.env'))) 'CAD and environment files must be excluded.'
$selectedVault = Get-Content -Raw -LiteralPath (Join-Path $index 'obsidian-current.json') | ConvertFrom-Json
$vault = Join-Path $index $selectedVault.directory
Check (Test-Path -LiteralPath (Join-Path $vault 'graph.canvas')) 'Obsidian canvas must be exported.'
$notes = @(Get-ChildItem -LiteralPath $vault -Filter '*.md' -Recurse)
Check ($notes.Count -gt 0 -and (($notes | ForEach-Object { Get-Content -Raw -LiteralPath $_.FullName }) -join "`n").Contains('[[')) 'Obsidian notes must contain local links.'
$canvas = Get-Content -Raw -LiteralPath (Join-Path $vault 'graph.canvas') | ConvertFrom-Json
Check (@($canvas.nodes).Count -gt 0) 'Exported canvas JSON must contain graph nodes.'
foreach ($card in $canvas.nodes) { if ($card.type -eq 'file') { Check (Test-Path -LiteralPath (Join-Path $vault $card.file)) 'Every canvas file card must resolve.' } }
$query = Invoke-Context @('-Query','volume')
Check ($query.code -eq 0 -and $query.text.Contains('src/model.cpp') -and $query.text.Contains('volume')) $query.text
$calls = Invoke-Context @('-Query','volume','-Context','call')
Check ($calls.code -eq 0 -and $calls.text.Contains('volume')) 'Call-context filtering must retain the selected symbol.'
$secondManifest = Get-Content -Raw -LiteralPath $manifestPath | ConvertFrom-Json
Check ($firstManifest.graph_sha256 -ceq $secondManifest.graph_sha256 -and $query.text.Contains('rebuilt False')) 'Unchanged queries must reuse the index.'
$absent = Invoke-Context @('-Query','symbol_that_does_not_exist_034d')
Check ($absent.code -eq 4) 'No match must remain an explicit no-match status.'
@'
double changed_volume(double width) { return width * width; }
'@ | Set-Content -LiteralPath (Join-Path $fixture 'src/model.cpp') -Encoding ASCII
$obsolete = [IO.Path]::GetFullPath((Join-Path $fixture 'src/obsolete.cpp'))
if (-not $obsolete.StartsWith([IO.Path]::GetFullPath($fixture) + [IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) { throw 'Invalid owned fixture path.' }
Remove-Item -LiteralPath $obsolete
$updated = Invoke-Context @('-Obsidian','-Query','changed_volume')
Check ($updated.code -eq 0 -and $updated.text.Contains('changed_volume')) $updated.text
$updatedManifest = Get-Content -Raw -LiteralPath $manifestPath | ConvertFrom-Json
Check ($updatedManifest.source_sha256 -cne $firstManifest.source_sha256) 'An authored change must invalidate the source receipt.'
$nextVault = Get-Content -Raw -LiteralPath (Join-Path $index 'obsidian-current.json') | ConvertFrom-Json
Check ($nextVault.directory -cne $selectedVault.directory -and (Test-Path -LiteralPath (Join-Path $vault 'graph.canvas'))) 'A changed graph gets a separate vault while preserving the earlier export.'
Check (-not (Test-Path -LiteralPath (Join-Path $index 'corpus/src/obsolete.cpp'))) 'Deleted sources must leave the owned snapshot.'
$graph = Get-Content -Raw -LiteralPath $graphPath | ConvertFrom-Json
Check (-not (($graph.nodes | ConvertTo-Json -Depth 20 -Compress).Contains('obsolete()'))) 'Deleted symbols must leave the extracted graph.'
$rejected = Invoke-Context @('-IndexDirectory',(Join-Path $repo 'out/not-owned-by-fixture'))
Check ($rejected.code -ne 0 -and $rejected.text.Contains('must stay inside')) 'An index outside the selected repository output must reject.'
$before = Get-Content -Raw -LiteralPath $manifestPath
$handle = [IO.File]::Open((Join-Path $index 'context.lock'),'OpenOrCreate','ReadWrite','None')
try { $busy = Invoke-Context @(); Check ($busy.code -ne 0 -and $busy.text.Contains('index is busy')) 'A concurrent writer must reject explicitly.' }
finally { $handle.Dispose() }
Check ((Get-Content -Raw -LiteralPath $manifestPath) -ceq $before) 'Rejected operations preserve the last completed manifest.'
Write-Output "$checks code-context checks passed; fixture=$fixture; paid_model_calls=0"
