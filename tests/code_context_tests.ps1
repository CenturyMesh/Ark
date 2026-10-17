$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$script = Join-Path $repo 'scripts/code-context.ps1'
$fixture = Join-Path $repo ('out/private/code-context-tests-' + [Guid]::NewGuid().ToString('N'))
[IO.Directory]::CreateDirectory((Join-Path $fixture 'src')) | Out-Null
[IO.Directory]::CreateDirectory((Join-Path $fixture 'include')) | Out-Null
foreach ($directory in @('src/private','src/credentials','scripts','credentials','out/private','drawings')) { [IO.Directory]::CreateDirectory((Join-Path $fixture $directory)) | Out-Null }
[IO.Directory]::CreateDirectory((Join-Path $fixture 'docs/knowledge')) | Out-Null
'# Public electronics fixture [[ARK Knowledge/Workflow]]' | Set-Content -LiteralPath (Join-Path $fixture 'docs/knowledge/Electronics.md') -Encoding ASCII
@'

Literal code: `[[nodiscard]]` and ``[[not a note]] ` [[also code]]``.
```cpp
[[nodiscard]] int public_fixture();
````
~~~text
[[not an Obsidian link in a tilde fence]]
~~~
    [[indented code is not a note link]]
\[[escaped brackets are not a note link]]
Actual targets: [[ARK Knowledge/Workflow#Public workflow fixture|Workflow]]
and [[../model.cpp#Connections|Model]], plus [[#Public electronics fixture]].
'@ | Add-Content -LiteralPath (Join-Path $fixture 'docs/knowledge/Electronics.md') -Encoding ASCII
'# Public workflow fixture [[ARK Knowledge/Electronics]]' | Set-Content -LiteralPath (Join-Path $fixture 'docs/knowledge/Workflow.md') -Encoding ASCII
'SYNTHETIC_PRIVATE_PLACEHOLDER' | Set-Content -LiteralPath (Join-Path $fixture 'docs/knowledge/Credentials.md') -Encoding ASCII
function Check($Condition,[string]$Reason) { if (-not $Condition) { throw $Reason }; $script:checks++ }
$checks = 0
& git -C $fixture init --quiet
if ($LASTEXITCODE) { throw 'Cannot initialize isolated code fixture.' }
'out/' | Set-Content -LiteralPath (Join-Path $fixture '.gitignore') -Encoding ASCII
@('language.h=cpp','language.inl=cpp') | Set-Content -LiteralPath (Join-Path $fixture '.graphifyrc') -Encoding ASCII
'double volume(double width);' | Set-Content -LiteralPath (Join-Path $fixture 'include/model.h') -Encoding ASCII
@'
#include "../include/model.h"
double volume(double width) { return width * width * width; }
double estimate() { return volume(3.0); }
'@ | Set-Content -LiteralPath (Join-Path $fixture 'src/model.cpp') -Encoding ASCII
'double obsolete() { return 1.0; }' | Set-Content -LiteralPath (Join-Path $fixture 'src/obsolete.cpp') -Encoding ASCII
'double inline_probe_one() { return 1.0; }' | Set-Content -LiteralPath (Join-Path $fixture 'src/inline_probe.inl') -Encoding ASCII
foreach ($private in @('src/private/hidden.cpp','src/credentials/local.ps1','include/credentials.hpp','scripts/.credentials.ps1','credentials/account.ps1','out/private/hidden.cpp','drawings/private.h')) { 'SYNTHETIC_PRIVATE_PLACEHOLDER' | Set-Content -LiteralPath (Join-Path $fixture $private) -Encoding ASCII }
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
foreach ($private in @('src/private/hidden.cpp','src/credentials/local.ps1','include/credentials.hpp','scripts/.credentials.ps1','credentials/account.ps1','out/private/hidden.cpp','drawings/private.h')) { Check (-not (Test-Path -LiteralPath (Join-Path $index ('corpus/'+$private)))) 'Private and credential paths must never enter the source snapshot.' }
Check (@($firstManifest.files | Where-Object { $_.path -eq 'src/inline_probe.inl' }).Count -eq 1 -and (Test-Path -LiteralPath (Join-Path $index 'corpus/src/inline_probe.inl'))) '.inl code is an explicit hashed source input.'
$selectedVault = Get-Content -Raw -LiteralPath (Join-Path $index 'obsidian-current.json') | ConvertFrom-Json
$vault = Join-Path $index $selectedVault.directory
Check ($selectedVault.knowledge_notes -eq 2 -and (Test-Path -LiteralPath (Join-Path $vault 'ARK Knowledge/Electronics.md'))) 'Curated knowledge notes must be included without semantic model extraction.'
Check ($first.code -eq 0 -and (Get-Content -Raw -LiteralPath (Join-Path $vault 'ARK Knowledge/Electronics.md')).Contains('[[nodiscard]]')) 'Code fences/inline code/escaped brackets stay literal while file paths, aliases and anchors resolve.'
Check (-not (Test-Path -LiteralPath (Join-Path $vault 'ARK Knowledge/Credentials.md'))) 'Credential notes cannot enter the derived knowledge export.'
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
'double inline_probe_two() { return 2.0; }' | Set-Content -LiteralPath (Join-Path $fixture 'src/inline_probe.inl') -Encoding ASCII
$inlUpdated = Invoke-Context @('-Query','inline_probe_two')
Check ($inlUpdated.code -eq 0 -and $inlUpdated.text.Contains('inline_probe_two')) $inlUpdated.text
$inlManifest = Get-Content -Raw -LiteralPath $manifestPath | ConvertFrom-Json
Check ($inlManifest.source_sha256 -cne $firstManifest.source_sha256 -and $inlManifest.graph_sha256 -cne $firstManifest.graph_sha256) '.inl edits invalidate source and extracted graph without requiring Refresh.'
@'
double changed_volume(double width) { return width * width; }
'@ | Set-Content -LiteralPath (Join-Path $fixture 'src/model.cpp') -Encoding ASCII
$obsolete = [IO.Path]::GetFullPath((Join-Path $fixture 'src/obsolete.cpp'))
if (-not $obsolete.StartsWith([IO.Path]::GetFullPath($fixture) + [IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) { throw 'Invalid owned fixture path.' }
Remove-Item -LiteralPath $obsolete
$updated = Invoke-Context @('-Obsidian','-Query','changed_volume')
Check ($updated.code -eq 0 -and $updated.text.Contains('changed_volume')) $updated.text
$updatedManifest = Get-Content -Raw -LiteralPath $manifestPath | ConvertFrom-Json
Check ($updatedManifest.source_sha256 -cne $inlManifest.source_sha256) 'An authored change must invalidate the source receipt.'
$nextVault = Get-Content -Raw -LiteralPath (Join-Path $index 'obsidian-current.json') | ConvertFrom-Json
Check ($nextVault.directory -cne $selectedVault.directory -and (Test-Path -LiteralPath (Join-Path $vault 'graph.canvas'))) 'A changed graph gets a separate vault while preserving the earlier export.'
Check (-not (Test-Path -LiteralPath (Join-Path $index 'corpus/src/obsolete.cpp'))) 'Deleted sources must leave the owned snapshot.'
$graph = Get-Content -Raw -LiteralPath $graphPath | ConvertFrom-Json
Check (-not (($graph.nodes | ConvertTo-Json -Depth 20 -Compress).Contains('obsolete()'))) 'Deleted symbols must leave the extracted graph.'
$priorKnowledge = $nextVault
'# Updated public workflow fixture [[ARK Knowledge/Electronics]]' | Set-Content -LiteralPath (Join-Path $fixture 'docs/knowledge/Workflow.md') -Encoding ASCII
$knowledgeOnly = Invoke-Context @('-Obsidian')
Check ($knowledgeOnly.code -eq 0 -and $knowledgeOnly.text.Contains('rebuilt False')) 'Knowledge-only edits must reuse the unchanged code graph.'
$changedKnowledge = Get-Content -Raw -LiteralPath (Join-Path $index 'obsidian-current.json') | ConvertFrom-Json
Check ($changedKnowledge.graph_sha256 -ceq $priorKnowledge.graph_sha256 -and $changedKnowledge.knowledge_sha256 -cne $priorKnowledge.knowledge_sha256 -and $changedKnowledge.directory -cne $priorKnowledge.directory) 'Knowledge notes must version the vault independently from code.'
Check ((Get-Content -Raw -LiteralPath (Join-Path (Join-Path $index $changedKnowledge.directory) 'ARK Knowledge/Workflow.md')).Contains('Updated public workflow')) 'Selected notes must match the current curated source.'
$editedVault = Join-Path $index $changedKnowledge.directory
$editedGuide = Join-Path $editedVault 'ARK Knowledge/Workflow.md'
Add-Content -LiteralPath $editedGuide -Encoding UTF8 -Value '# Synthetic personal edit to preserve'
$personalHash = (Get-FileHash -LiteralPath $editedGuide -Algorithm SHA256).Hash
$repaired = Invoke-Context @('-Obsidian')
Check ($repaired.code -eq 0) $repaired.text
$verified = Get-Content -Raw -LiteralPath (Join-Path $index 'obsidian-current.json') | ConvertFrom-Json
Check ($verified.directory -cne $changedKnowledge.directory -and (Get-FileHash -LiteralPath $editedGuide -Algorithm SHA256).Hash -ceq $personalHash) 'A locally edited guide gets a separate verified vault without touching the earlier edit.'
$verifiedVault = Join-Path $index $verified.directory
$receiptPath = Join-Path $verifiedVault '.ark-code-vault.json'
$receipt = Get-Content -Raw -LiteralPath $receiptPath | ConvertFrom-Json
Check ($receipt.schema -ceq 'ark.code-vault.v2' -and $receipt.content_sha256 -ceq $verified.content_sha256 -and $receipt.files.Count -gt 0 -and $receipt.verified_links -gt 0) 'Vault receipts attest generated content and resolving canvas/wiki links.'
Check (-not (Get-Content -Raw -LiteralPath (Join-Path $verifiedVault 'ARK Knowledge/Workflow.md')).Contains('Synthetic personal edit')) 'Fresh generated guides match source rather than inheriting an edited export.'
$receiptHash = (Get-FileHash -LiteralPath $receiptPath -Algorithm SHA256).Hash
$stable = Invoke-Context @('-Obsidian')
$stablePointer = Get-Content -Raw -LiteralPath (Join-Path $index 'obsidian-current.json') | ConvertFrom-Json
Check ($stable.code -eq 0 -and $stablePointer.directory -ceq $verified.directory -and (Get-FileHash -LiteralPath $receiptPath -Algorithm SHA256).Hash -ceq $receiptHash) 'A verified replacement is reused without rewriting existing vault files or receipts.'
$codeNote = Get-ChildItem -LiteralPath $verifiedVault -Filter '*.md' -File | Select-Object -First 1
Add-Content -LiteralPath $codeNote.FullName -Encoding UTF8 -Value '[[Missing synthetic exported target]]'
$editedCodeHash = (Get-FileHash -LiteralPath $codeNote.FullName -Algorithm SHA256).Hash
$codeRepair = Invoke-Context @('-Obsidian')
$codePointer = Get-Content -Raw -LiteralPath (Join-Path $index 'obsidian-current.json') | ConvertFrom-Json
Check ($codeRepair.code -eq 0 -and $codePointer.directory -cne $verified.directory -and (Get-FileHash -LiteralPath $codeNote.FullName -Algorithm SHA256).Hash -ceq $editedCodeHash) 'A broken/edited code-note link is preserved and replaced by a separately verified export.'
$codeStable = Invoke-Context @('-Obsidian')
$codeStablePointer = Get-Content -Raw -LiteralPath (Join-Path $index 'obsidian-current.json') | ConvertFrom-Json
Check ($codeStable.code -eq 0 -and $codeStablePointer.directory -ceq $codePointer.directory) 'Repeated replacement vaults remain reusable instead of generating a new suffix on every call.'
$guideSource = Join-Path $fixture 'docs/knowledge/Workflow.md'
$guideBytes = [IO.File]::ReadAllBytes($guideSource)
$lastPointer = Get-Content -Raw -LiteralPath (Join-Path $index 'obsidian-current.json')
try {
    Add-Content -LiteralPath $guideSource -Encoding UTF8 -Value '[[Missing synthetic curated target]]'
    $broken = Invoke-Context @('-Obsidian')
    Check ($broken.code -ne 0 -and $broken.text.Contains('link does not resolve')) 'A new export containing a broken curated link cannot be published as verified.'
    Check ((Get-Content -Raw -LiteralPath (Join-Path $index 'obsidian-current.json')) -ceq $lastPointer) 'A failed link check preserves the last verified pointer.'
} finally { [IO.File]::WriteAllBytes($guideSource,$guideBytes) }
$backend = $env:GRAPHIFY_BACKEND; $logging = $env:GRAPHIFY_QUERY_LOG_DISABLE
try {
    $env:GRAPHIFY_BACKEND='synthetic-audit-placeholder'; $env:GRAPHIFY_QUERY_LOG_DISABLE='synthetic-audit-placeholder'
    & $script -SourceRoot $fixture | Out-Null
    Check ($env:GRAPHIFY_BACKEND -ceq 'synthetic-audit-placeholder' -and $env:GRAPHIFY_QUERY_LOG_DISABLE -ceq 'synthetic-audit-placeholder') 'Offline wrapper restores both caller environment settings after execution.'
} finally { $env:GRAPHIFY_BACKEND=$backend; $env:GRAPHIFY_QUERY_LOG_DISABLE=$logging }
$rejected = Invoke-Context @('-IndexDirectory',(Join-Path $repo 'out/not-owned-by-fixture'))
Check ($rejected.code -ne 0 -and $rejected.text.Contains('must stay inside')) 'An index outside the selected repository output must reject.'
$before = Get-Content -Raw -LiteralPath $manifestPath
$handle = [IO.File]::Open((Join-Path $index 'context.lock'),'OpenOrCreate','ReadWrite','None')
try { $busy = Invoke-Context @(); Check ($busy.code -ne 0 -and $busy.text.Contains('index is busy')) 'A concurrent writer must reject explicitly.' }
finally { $handle.Dispose() }
Check ((Get-Content -Raw -LiteralPath $manifestPath) -ceq $before) 'Rejected operations preserve the last completed manifest.'
Write-Output "$checks code-context checks passed; fixture=$fixture; paid_model_calls=0"
