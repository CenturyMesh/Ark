param(
    [string]$Query,
    [string[]]$Seed = @(),
    [string[]]$Context = @(),
    [switch]$Refresh,
    [switch]$Obsidian,
    [switch]$Prepare,
    [string]$SourceRoot = (Split-Path $PSScriptRoot -Parent),
    [string]$IndexDirectory
)
$ErrorActionPreference = 'Stop'
$SourceRoot = (Resolve-Path -LiteralPath $SourceRoot).Path
if (-not $IndexDirectory) { $IndexDirectory = Join-Path $SourceRoot 'out/context-code' }
$IndexDirectory = [IO.Path]::GetFullPath($IndexDirectory)
$allowedRoot = [IO.Path]::GetFullPath((Join-Path $SourceRoot 'out')).TrimEnd('\','/') + [IO.Path]::DirectorySeparatorChar
if (-not $IndexDirectory.StartsWith($allowedRoot,[StringComparison]::OrdinalIgnoreCase)) { throw 'The derived index must stay inside this repository out directory.' }
$tool = 'graphifyy==0.9.92'
$savedBackend = $env:GRAPHIFY_BACKEND
$savedLogging = $env:GRAPHIFY_QUERY_LOG_DISABLE
$env:GRAPHIFY_BACKEND = 'none'
$env:GRAPHIFY_QUERY_LOG_DISABLE = '1'
$lock = $null
function Private-SourcePath([string]$Relative) {
    $normalized = $Relative.Replace('\','/')
    return $normalized -match '(?i)(^|/)[.]?(out|private|credentials?|secrets?|sessions?)(/|$)' -or
        [IO.Path]::GetFileNameWithoutExtension($normalized) -match '(?i)^[.]?(private|credentials?|secrets?|api[-_]?keys?)([._-].*)?$'
}
function Reparse-SourcePath([string]$Absolute,[hashtable]$SafeDirectories=@{},[IO.FileSystemInfo]$Entry=$null) {
    if (-not $Entry) { $Entry=Get-Item -Force -LiteralPath $Absolute }
    if ($Entry.Attributes -band [IO.FileAttributes]::ReparsePoint) { return $true }
    $candidate=Split-Path $Absolute -Parent; $checked=@()
    while ($candidate -and -not [string]::Equals($candidate,$SourceRoot,[StringComparison]::OrdinalIgnoreCase)) {
        if ($SafeDirectories.ContainsKey($candidate)) { break }
        if ((Get-Item -Force -LiteralPath $candidate).Attributes -band [IO.FileAttributes]::ReparsePoint) { return $true }
        $checked+=@($candidate); $candidate=Split-Path $candidate -Parent
    }
    foreach ($directory in $checked) { $SafeDirectories[$directory]=$true }
    if ($Entry.Attributes -band [IO.FileAttributes]::Directory) { $SafeDirectories[$Absolute]=$true }
    return $false
}
function Source-Inventory {
    $paths = @(& git -C $SourceRoot -c core.quotepath=false ls-files --cached --others --exclude-standard -- src include tests scripts cmake CMakeLists.txt CMakePresets.json .graphifyrc)
    if ($LASTEXITCODE) { throw 'Cannot inventory repository code inputs.' }
    $safeDirectories=@{}
    $rows = foreach ($relative in ($paths | Sort-Object -Unique)) {
        if (Private-SourcePath $relative) { continue }
        if ($relative -ne '.graphifyrc' -and [IO.Path]::GetExtension($relative) -notin @('.cpp','.c','.h','.hpp','.inl','.ps1','.cmake')) { continue }
        $absolute = [IO.Path]::GetFullPath((Join-Path $SourceRoot $relative))
        if (-not $absolute.StartsWith($SourceRoot.TrimEnd('\','/') + [IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) { throw 'Source input escaped the repository.' }
        if ((Test-Path -LiteralPath $absolute -PathType Leaf) -and -not (Reparse-SourcePath $absolute $safeDirectories)) { [pscustomobject]@{ path=$relative; sha256=(Get-FileHash -LiteralPath $absolute -Algorithm SHA256).Hash.ToLowerInvariant() } }
    }
    return @($rows)
}
function Inventory-Digest($Rows) {
    $sha = [Security.Cryptography.SHA256]::Create()
    try { return ([BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes((($Rows | ForEach-Object { "$($_.path) $($_.sha256)" }) -join "`n"))))).Replace('-','').ToLowerInvariant() }
    finally { $sha.Dispose() }
}
function Knowledge-Inventory([string]$Root) {
    if (-not (Test-Path -LiteralPath $Root -PathType Container)) { return @() }
    if (Reparse-SourcePath $Root) { throw 'The curated knowledge directory must not be a linked private directory.' }
    $safeDirectories=@{}
    return @(Get-ChildItem -LiteralPath $Root -Filter '*.md' -File | Sort-Object Name | Where-Object { -not (Private-SourcePath $_.Name) -and -not (Reparse-SourcePath $_.FullName $safeDirectories $_) } | ForEach-Object { [pscustomobject]@{path=$_.Name;sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()} })
}
function Markdown-LinkText([string]$Text) {
    $lines=New-Object 'System.Collections.Generic.List[string]'; $fence=''; $fenceLength=0
    foreach ($line in ($Text -split '\r?\n')) {
        if ($fence) { if ($line -match ('^[ ]{0,3}' + [regex]::Escape($fence) + '{' + $fenceLength + ',}[ \t]*$')) { $fence='' }; continue }
        if ($line -match '^[ ]{0,3}(`{3,}|~{3,})') { $fence=$Matches[1].Substring(0,1); $fenceLength=$Matches[1].Length; continue }
        if ($line -match '^(    |\t)') { continue }
        $lines.Add($line)
    }
    return [regex]::Replace(($lines -join "`n"),'(?s)(?<!`)(`+)(?!`).*?(?<!`)\1(?!`)','')
}
function Vault-Evidence([string]$Vault,[string]$Graph) {
    $vaultRoot = [IO.Path]::GetFullPath($Vault).TrimEnd('\','/') + [IO.Path]::DirectorySeparatorChar
    $files = @(Get-ChildItem -Force -LiteralPath $Vault -Recurse -File | Where-Object { $_.Extension -eq '.md' -or $_.Name -in @('graph.canvas','graph.json','.graphify_obsidian_manifest.json') } | Sort-Object FullName)
    $inventory = @(); $paths = @{}; $basenames = @{}; $safeDirectories=@{}
    foreach ($file in $files) {
        if (Reparse-SourcePath $file.FullName $safeDirectories $file) { throw 'A derived vault file is a link; preserve this vault and export separately.' }
        $relative = $file.FullName.Substring($vaultRoot.Length).Replace('\','/')
        $inventory += [pscustomobject]@{path=$relative;sha256=(Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()}
        $paths[$relative] = $true
        if ($file.Extension -eq '.md') { $key=[IO.Path]::GetFileNameWithoutExtension($file.Name); if (-not $basenames.ContainsKey($key)) { $basenames[$key]=@() }; $basenames[$key]=@($basenames[$key]) + @($relative) }
    }
    $canvas = Get-Content -Raw -Encoding UTF8 -LiteralPath (Join-Path $Vault 'graph.canvas') | ConvertFrom-Json
    $cards = @($canvas.nodes | Where-Object { $_.type -eq 'file' })
    $data = Get-Content -Raw -Encoding UTF8 -LiteralPath $Graph | ConvertFrom-Json
    if ($cards.Count -ne @($data.nodes).Count) { throw 'Obsidian export does not cover every graph node.' }
    $links = 0
    foreach ($card in $cards) {
        $note = [IO.Path]::GetFullPath((Join-Path $Vault $card.file))
        if (-not $note.StartsWith($vaultRoot,[StringComparison]::OrdinalIgnoreCase) -or -not $paths.ContainsKey($note.Substring($vaultRoot.Length).Replace('\','/'))) { throw 'Obsidian canvas references a missing or invalid note.' }
        $links++
    }
    foreach ($file in $files | Where-Object { $_.Extension -eq '.md' }) {
        $linkText=Markdown-LinkText (Get-Content -Raw -Encoding UTF8 -LiteralPath $file.FullName)
        foreach ($match in [regex]::Matches($linkText,'\[\[([^\]\r\n]+)\]\]')) {
            $escapes=0; for ($at=$match.Index-1;$at -ge 0 -and $linkText[$at] -eq '\';$at--) { $escapes++ }; if ($escapes % 2) { continue }
            $target = ($match.Groups[1].Value -split '\|',2)[0]; $target=($target -split '#',2)[0].Trim(); if (-not $target) { continue }
            $resolved = $false
            foreach ($base in @((Split-Path $file.FullName -Parent),$Vault)) {
                foreach ($suffix in @('','.md')) {
                    $destination=[IO.Path]::GetFullPath((Join-Path $base ($target+$suffix)))
                    if ($destination.StartsWith($vaultRoot,[StringComparison]::OrdinalIgnoreCase) -and $paths.ContainsKey($destination.Substring($vaultRoot.Length).Replace('\','/'))) { $resolved=$true }
                }
            }
            if (-not $resolved -and $target -notmatch '[/\\]' -and $basenames.ContainsKey($target) -and @($basenames[$target]).Count -eq 1) { $resolved=$true }
            if (-not $resolved) { throw "Obsidian note link does not resolve: $($file.Name) -> $target" }
            $links++
        }
    }
    return [pscustomobject]@{files=$inventory;content_sha256=(Inventory-Digest $inventory);verified_links=$links;nodes=$cards.Count}
}
try {
    if (-not (Get-Command uvx -ErrorAction SilentlyContinue)) { throw 'Install uv, then run this script with -Prepare.' }
    if ($Prepare) { & uvx --from $tool graphify --version; if ($LASTEXITCODE) { throw 'Cannot prepare pinned Graphify.' }; if (-not $Query -and -not $Refresh -and -not $Obsidian) { return } }
    [IO.Directory]::CreateDirectory($IndexDirectory) | Out-Null
    try { $lock = [IO.File]::Open((Join-Path $IndexDirectory 'context.lock'),'OpenOrCreate','ReadWrite','None') }
    catch { throw 'Code context index is busy; retry after its current operation.' }
    $corpus = Join-Path $IndexDirectory 'corpus'
    $graphRoot = Join-Path $IndexDirectory 'index'
    $graph = Join-Path $graphRoot 'graphify-out/graph.json'
    $manifestPath = Join-Path $IndexDirectory 'source-manifest.json'
    $rows = @(Source-Inventory)
    $digest = Inventory-Digest $rows
    $previous = $null
    if (Test-Path -LiteralPath $manifestPath) { $previous = Get-Content -Raw -Encoding UTF8 -LiteralPath $manifestPath | ConvertFrom-Json }
    $changed = $Refresh -or -not (Test-Path -LiteralPath $graph) -or -not $previous -or $previous.source_sha256 -cne $digest -or $previous.tool -cne $tool
    if (-not $changed) { $changed = $previous.graph_sha256 -cne (Get-FileHash -LiteralPath $graph -Algorithm SHA256).Hash.ToLowerInvariant() }
    if ($changed) {
        [IO.Directory]::CreateDirectory($corpus) | Out-Null
        $current = @{}
        foreach ($row in $rows) {
            $current[$row.path] = $true
            $destination = [IO.Path]::GetFullPath((Join-Path $corpus $row.path))
            if (-not $destination.StartsWith($corpus.TrimEnd('\','/') + [IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) { throw 'Snapshot destination escaped its owned corpus.' }
            [IO.Directory]::CreateDirectory((Split-Path $destination -Parent)) | Out-Null
            if (-not (Test-Path -LiteralPath $destination) -or (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash.ToLowerInvariant() -cne $row.sha256) { Copy-Item -LiteralPath (Join-Path $SourceRoot $row.path) -Destination $destination }
        }
        if ($previous) { foreach ($row in $previous.files) { if (-not $current.ContainsKey($row.path)) {
            $obsolete = [IO.Path]::GetFullPath((Join-Path $corpus $row.path))
            if (-not $obsolete.StartsWith($corpus.TrimEnd('\','/') + [IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) { throw 'Obsolete snapshot path escaped its owned corpus.' }
            if (Test-Path -LiteralPath $obsolete -PathType Leaf) { Remove-Item -LiteralPath $obsolete -Force }
        } } }
        & uvx --offline --from $tool graphify extract $corpus --code-only --force --no-cluster --max-workers 2 --out $graphRoot
        if ($LASTEXITCODE -or -not (Test-Path -LiteralPath $graph)) { throw 'Local code graph extraction failed; no fresh context is available.' }
        if ((Inventory-Digest @(Source-Inventory)) -cne $digest) { throw 'Code changed during indexing; retry on the current source.' }
        $manifest = [ordered]@{schema='ark.code-context.v1';source_sha256=$digest;tool=$tool;backend='none';files=$rows;graph='index/graphify-out/graph.json';graph_sha256=(Get-FileHash -LiteralPath $graph -Algorithm SHA256).Hash.ToLowerInvariant()}
        $manifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $manifestPath -Encoding UTF8
    }
    $vault = $null
    if ($Obsidian) {
        # Export new graph versions into new vaults. Upstream pruning on an
        # existing Windows vault can remove case-renamed current notes.
        # Never overwrite a personal edit or remove an earlier export.
        $graphHash = (Get-FileHash -LiteralPath $graph -Algorithm SHA256).Hash.ToLowerInvariant()
        $knowledgeRoot = Join-Path $SourceRoot 'docs/knowledge'
        $knowledge = @(Knowledge-Inventory $knowledgeRoot)
        $knowledgeHash = Inventory-Digest $knowledge
        $vaultKey = Inventory-Digest @([pscustomobject]@{path='source';sha256=$digest},[pscustomobject]@{path='graph';sha256=$graphHash},[pscustomobject]@{path='knowledge';sha256=$knowledgeHash})
        # Short directory names leave room for note filenames on Windows.
        # Full digests remain in the completion receipt and are checked below.
        $vaultBase = 'obsidian/' + $vaultKey.Substring(0,24)
        $vaultRelative = $vaultBase
        $pointerPath = Join-Path $IndexDirectory 'obsidian-current.json'
        if (Test-Path -LiteralPath $pointerPath) {
            try {
                $selection = Get-Content -Raw -Encoding UTF8 -LiteralPath $pointerPath | ConvertFrom-Json
                if ($selection.directory -match ('^' + [regex]::Escape($vaultRelative) + '(-[a-f0-9]{32})?$')) { $vaultRelative=$selection.directory }
            } catch { Write-Host 'The earlier vault pointer is unreadable; preserve its vault and verify a separate export.' }
        }
        $vault = Join-Path $IndexDirectory $vaultRelative
        $complete = Join-Path $vault '.ark-code-vault.json'
        $reusedVault = $false
        if (Test-Path -LiteralPath $complete) {
            try {
                $priorVault = Get-Content -Raw -Encoding UTF8 -LiteralPath $complete | ConvertFrom-Json
                if ($priorVault.schema -cne 'ark.code-vault.v2' -or $priorVault.graph_sha256 -cne $graphHash -or $priorVault.source_sha256 -cne $digest -or $priorVault.knowledge_sha256 -cne $knowledgeHash) { throw 'Vault identity or content receipt differs.' }
                $evidence = Vault-Evidence $vault $graph
                if ($priorVault.content_sha256 -cne $evidence.content_sha256 -or $priorVault.verified_links -ne $evidence.verified_links) { throw 'Vault content changed after export.' }
                $reusedVault = $true
            } catch { Write-Host 'The existing vault cannot be re-attested; preserve it and create a separate export.' }
        }
        if ((Test-Path -LiteralPath $vault) -and -not $reusedVault) {
            do { $vaultRelative = $vaultBase + '-' + [Guid]::NewGuid().ToString('N'); $vault = Join-Path $IndexDirectory $vaultRelative } while (Test-Path -LiteralPath $vault)
            $complete = Join-Path $vault '.ark-code-vault.json'
        }
        if (-not $reusedVault) {
            & uvx --offline --from $tool graphify export obsidian --graph $graph --dir $vault
            if ($LASTEXITCODE) { throw 'Cannot export the derived Obsidian vault.' }
        }
        $guideDirectory = Join-Path $vault 'ARK Knowledge'
        if (-not $reusedVault -and $knowledge.Count) { [IO.Directory]::CreateDirectory($guideDirectory) | Out-Null }
        if (-not $reusedVault) { foreach ($guide in $knowledge) {
            $sourceGuide = Join-Path $knowledgeRoot $guide.path
            if ((Get-FileHash -LiteralPath $sourceGuide -Algorithm SHA256).Hash.ToLowerInvariant() -cne $guide.sha256) { throw 'Knowledge notes changed during export; retain the earlier vault and retry.' }
            $destinationGuide = Join-Path $guideDirectory $guide.path
            if (-not (Test-Path -LiteralPath $destinationGuide)) {
                Copy-Item -LiteralPath $sourceGuide -Destination $destinationGuide
                $codeNotes = switch ([IO.Path]::GetFileNameWithoutExtension($guide.path)) {
                    'Electronics' { @('electronics.cpp','electronics_modeling.cpp','cable_guide_035b.inl') }
                    'Plastics' { @('plastics_electronics_035b.inl','modeling_031b.cpp','solid_modeling.cpp') }
                    'Vendors' { @('component_vendors.cpp','component_sources.cpp','component_library_tools.cpp') }
                    'Workflow' { @('context_graph_034d.inl','agent_team.cpp','assistant.cpp') }
                }
                $links = @($codeNotes | Where-Object { Test-Path -LiteralPath (Join-Path $vault ($_ + '.md')) } | ForEach-Object { '- [[' + $_ + ']]' })
                if ($links.Count) { Add-Content -LiteralPath $destinationGuide -Encoding UTF8 -Value ("`n## Related code`n`n" + ($links -join "`n")) }
            }
        } }
        $evidence = Vault-Evidence $vault $graph
        if ($reusedVault -and ($priorVault.content_sha256 -cne $evidence.content_sha256 -or $priorVault.verified_links -ne $evidence.verified_links)) { throw 'Vault content changed during verification; the earlier pointer is retained. Retry for a separate export.' }
        if ((Inventory-Digest @(Source-Inventory)) -cne $digest) { throw 'Code changed during export; the earlier vault pointer is retained.' }
        $knowledgeNow = @(Knowledge-Inventory $knowledgeRoot)
        if ((Inventory-Digest $knowledgeNow) -cne $knowledgeHash) { throw 'Knowledge notes changed during export; the earlier vault pointer is retained.' }
        if (-not $reusedVault) { [ordered]@{schema='ark.code-vault.v2';graph_sha256=$graphHash;source_sha256=$digest;knowledge_sha256=$knowledgeHash;knowledge_notes=$knowledge.Count;nodes=$evidence.nodes;tool=$tool;files=$evidence.files;content_sha256=$evidence.content_sha256;verified_links=$evidence.verified_links;link_validation='canvas file cards and wiki note file targets; heading/block existence is not validated'} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $complete -Encoding UTF8 }
        $pointerTemp = Join-Path $IndexDirectory ('obsidian-current-' + [Guid]::NewGuid().ToString('N') + '.tmp')
        try {
            [ordered]@{directory=$vaultRelative;graph_sha256=$graphHash;source_sha256=$digest;knowledge_sha256=$knowledgeHash;knowledge_notes=$knowledge.Count;content_sha256=$evidence.content_sha256;verified_links=$evidence.verified_links} | ConvertTo-Json | Set-Content -LiteralPath $pointerTemp -Encoding UTF8
            if (Test-Path -LiteralPath $pointerPath) { [IO.File]::Replace($pointerTemp,$pointerPath,[NullString]::Value) } else { [IO.File]::Move($pointerTemp,$pointerPath) }
        } finally { if (Test-Path -LiteralPath $pointerTemp) { Remove-Item -LiteralPath $pointerTemp } }
        Write-Host "Verified separate Obsidian vault: $vault ($($evidence.nodes) file cards; $($evidence.verified_links) resolved links)."
    }
    Write-Host "Code context / source $digest / rebuilt $changed / local AST / graph links are navigation evidence; read current source before editing."
    if ($Query) {
        $arguments = @('--offline','--from',$tool,'graphify','query',$Query,'--graph',$graph)
        foreach ($item in $Seed) { $arguments += @('--seed',$item) }
        foreach ($item in $Context) { $arguments += @('--context',$item) }
        & uvx @arguments
        if ($LASTEXITCODE -notin @(0,3,4)) { throw 'Code graph query failed.' }
        $queryCode = $LASTEXITCODE
        if ((Inventory-Digest @(Source-Inventory)) -cne $digest) { throw 'Code changed during this query; discard its result and retry.' }
        if ($queryCode) { exit $queryCode }
    } else {
        $data = Get-Content -Raw -Encoding UTF8 -LiteralPath $graph | ConvertFrom-Json
        [pscustomobject]@{source_sha256=$digest;nodes=@($data.nodes).Count;graph=$graph;index_rebuilt=[bool]$changed;obsidian=$vault} | ConvertTo-Json -Compress
    }
} finally {
    if ($lock) { $lock.Dispose() }
    $env:GRAPHIFY_BACKEND = $savedBackend
    $env:GRAPHIFY_QUERY_LOG_DISABLE = $savedLogging
}
