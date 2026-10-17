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
function Source-Inventory {
    $paths = @(& git -C $SourceRoot -c core.quotepath=false ls-files --cached --others --exclude-standard -- src include tests scripts cmake CMakeLists.txt CMakePresets.json .graphifyrc)
    if ($LASTEXITCODE) { throw 'Cannot inventory repository code inputs.' }
    $rows = foreach ($relative in ($paths | Sort-Object -Unique)) {
        if ($relative -ne '.graphifyrc' -and [IO.Path]::GetExtension($relative) -notin @('.cpp','.c','.h','.hpp','.inl','.ps1','.cmake')) { continue }
        $absolute = [IO.Path]::GetFullPath((Join-Path $SourceRoot $relative))
        if (-not $absolute.StartsWith($SourceRoot.TrimEnd('\','/') + [IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) { throw 'Source input escaped the repository.' }
        if (Test-Path -LiteralPath $absolute -PathType Leaf) { [pscustomobject]@{ path=$relative; sha256=(Get-FileHash -LiteralPath $absolute -Algorithm SHA256).Hash.ToLowerInvariant() } }
    }
    return @($rows)
}
function Inventory-Digest($Rows) {
    $sha = [Security.Cryptography.SHA256]::Create()
    try { return ([BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes((($Rows | ForEach-Object { "$($_.path) $($_.sha256)" }) -join "`n"))))).Replace('-','').ToLowerInvariant() }
    finally { $sha.Dispose() }
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
        $vaultRelative = 'obsidian/' + $graphHash
        $vault = Join-Path $IndexDirectory $vaultRelative
        $complete = Join-Path $vault '.ark-code-vault.json'
        if ((Test-Path -LiteralPath $vault) -and -not (Test-Path -LiteralPath $complete)) {
            $vaultRelative += '-' + [Guid]::NewGuid().ToString('N')
            $vault = Join-Path $IndexDirectory $vaultRelative
            $complete = Join-Path $vault '.ark-code-vault.json'
        }
        if (-not (Test-Path -LiteralPath $complete)) {
            & uvx --offline --from $tool graphify export obsidian --graph $graph --dir $vault
            if ($LASTEXITCODE) { throw 'Cannot export the derived Obsidian vault.' }
        }
        $canvas = Get-Content -Raw -Encoding UTF8 -LiteralPath (Join-Path $vault 'graph.canvas') | ConvertFrom-Json
        $cards = @($canvas.nodes | Where-Object { $_.type -eq 'file' })
        $data = Get-Content -Raw -Encoding UTF8 -LiteralPath $graph | ConvertFrom-Json
        if ($cards.Count -ne @($data.nodes).Count) { throw 'Obsidian export does not cover every graph node.' }
        foreach ($card in $cards) {
            $note = [IO.Path]::GetFullPath((Join-Path $vault $card.file))
            if (-not $note.StartsWith($vault.TrimEnd('\','/') + [IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase) -or -not (Test-Path -LiteralPath $note -PathType Leaf)) { throw 'Obsidian canvas references a missing or invalid note; retain the earlier vault.' }
        }
        [ordered]@{graph_sha256=$graphHash;source_sha256=$digest;nodes=$cards.Count;tool=$tool} | ConvertTo-Json | Set-Content -LiteralPath $complete -Encoding UTF8
        [ordered]@{directory=$vaultRelative;graph_sha256=$graphHash;source_sha256=$digest} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $IndexDirectory 'obsidian-current.json') -Encoding UTF8
        Write-Host "Verified separate Obsidian vault: $vault ($($cards.Count) file cards)."
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
