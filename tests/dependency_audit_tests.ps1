$ErrorActionPreference='Stop'
Import-Module (Join-Path (Split-Path $PSScriptRoot -Parent) 'scripts/Ark.DependencyAudit.psm1') -Force
function Assert($Condition,[string]$Message) { if(-not $Condition){throw $Message} }
$sbom = @'
{"bomFormat":"CycloneDX","specVersion":"1.5","components":[
 {"bom-ref":"stb_image","name":"stb_image","version":"2.30","externalReferences":[{"type":"vcs","url":"https://github.com/nothings/stb/tree/013ac3beddff3dbffafd5177e7972067cd2b5083"}]},
 {"bom-ref":"SQLite","name":"SQLite","version":"3.54.0","externalReferences":[]},
 {"bom-ref":"OCCT","name":"OCCT","version":"8.0.1","externalReferences":[{"type":"vcs","url":"https://github.com/Open-Cascade-SAS/OCCT/tree/V8_0_1"}]},
 {"bom-ref":"runtime:libc++.dll","name":"libc++.dll","properties":[]}]}
'@ | ConvertFrom-Json
$commit=Get-ArkDependencyQuery $sbom.components[0]
Assert ($commit.commit -eq '013ac3beddff3dbffafd5177e7972067cd2b5083' -and -not $commit.ContainsKey('version')) 'Pinned commit is queried without incompatible version fields'
$tag=Get-ArkDependencyQuery $sbom.components[2]
Assert ($tag.package.ecosystem -eq 'GIT' -and $tag.version -eq 'V8_0_1') 'Git tag preserves upstream naming'
$script:page=0
$audit=Invoke-ArkDependencyAudit $sbom {
    param($Request)
    if($Request.ContainsKey('commit')) {
        $script:page++
        if($script:page -eq 1) {return ('{"vulns":[{"id":"SYNTHETIC-1"}],"next_page_token":"second"}' | ConvertFrom-Json)}
        Assert ($Request.page_token -eq 'second') 'Pagination token forwarded'
        return ('{"vulns":[{"id":"SYNTHETIC-1"},{"id":"SYNTHETIC-2"}]}' | ConvertFrom-Json)
    }
    if($Request.package.ecosystem -eq 'GIT') { throw 'Synthetic database outage' }
    return ('{}' | ConvertFrom-Json)
}
Assert ($audit.findings -eq 2 -and $audit.records[0].pages -eq 2) 'All pages are retained and findings deduplicated'
Assert ($audit.records[1].status -eq 'no_records_returned') 'Empty database response is explicitly scoped'
Assert ($audit.query_failures -eq 1 -and $audit.unqueried_components -eq 1) 'Unavailable database and unsupported runtime identity cannot be reported clean'
$loop=Invoke-ArkDependencyAudit ([pscustomobject]@{bomFormat='CycloneDX';specVersion='1.5';components=@($sbom.components[0])}) {param($Request) '{"next_page_token":"loop"}' | ConvertFrom-Json}
Assert ($loop.query_failures -eq 1 -and $loop.records[0].pages -eq 2) 'Repeated pagination token rejects'
$malformed=Invoke-ArkDependencyAudit ([pscustomobject]@{bomFormat='CycloneDX';specVersion='1.5';components=@($sbom.components[0])}) {param($Request) 'unexpected response'}
Assert ($malformed.query_failures -eq 1) 'Malformed response rejects'
$numeric=Invoke-ArkDependencyAudit ([pscustomobject]@{bomFormat='CycloneDX';specVersion='1.5';components=@($sbom.components[0])}) {param($Request) 23}
Assert ($numeric.query_failures -eq 1) 'Numeric JSON response cannot become an empty clean result'
foreach ($invalid in @('{"message":"temporarily unavailable"}', '{"next_page_token":23}', '{"vulns":{"id":"SYNTHETIC-1"}}', '{"vulns":[{"id":23}]}', '{"vulns":[{"id":" "}]}')) {
    $script:invalidAnswer=$invalid | ConvertFrom-Json
    $invalidAudit=Invoke-ArkDependencyAudit ([pscustomobject]@{bomFormat='CycloneDX';specVersion='1.5';components=@($sbom.components[0])}) {param($Request) $script:invalidAnswer}
    Assert ($invalidAudit.query_failures -eq 1 -and $invalidAudit.findings -eq 0) 'Unexpected response fields and wrong schema types cannot become a successful audit'
}
$unsupported=Invoke-ArkDependencyAudit ([pscustomobject]@{bomFormat='CycloneDX';specVersion='1.5';components=@([pscustomobject]@{'bom-ref'='unmapped';name='Unmapped source'})}) {param($Request) throw 'Must not query an invented identity'}
Assert ($unsupported.unqueried_components -eq 1 -and $unsupported.query_failures -eq 0) 'Unknown source identity stays unsupported'
$workspace=Join-Path ([IO.Path]::GetTempPath()) ('ark-dependency-audit-'+[guid]::NewGuid().ToString('N'))
[IO.Directory]::CreateDirectory($workspace) | Out-Null
try {
    $unicodeName='runtime-'+[char]0x00e7+[char]0x03c0+'.dll'
    $unicodeSbom=[pscustomobject]@{bomFormat='CycloneDX';specVersion='1.5';components=@([pscustomobject]@{'bom-ref'=('runtime:'+$unicodeName);name=$unicodeName})}
    $sbomPath=Join-Path $workspace 'unicode-sbom.json';$outputPath=Join-Path $workspace 'unicode-audit.json'
    [IO.File]::WriteAllText($sbomPath,($unicodeSbom | ConvertTo-Json -Depth 8),(New-Object Text.UTF8Encoding($false)))
    $beforeHash=(Get-FileHash -LiteralPath $sbomPath).Hash
    $shell=(Get-Process -Id $PID).Path
    & $shell -NoProfile -NonInteractive -ExecutionPolicy Bypass -File (Join-Path (Split-Path $PSScriptRoot -Parent) 'scripts/audit-dependencies.ps1') -SbomPath $sbomPath -OutputPath $outputPath | Out-Null
    Assert ($LASTEXITCODE -eq 0) 'Runtime-only Unicode CLI fixture completes without any database request'
    $unicodeAudit=[IO.File]::ReadAllText($outputPath) | ConvertFrom-Json
    Assert ($unicodeAudit.records[0].name -ceq $unicodeName -and $unicodeAudit.records[0].component -ceq ('runtime:'+$unicodeName)) 'Windows PowerShell reads exact UTF-8 SBOM names'
    Assert ((Get-FileHash -LiteralPath $sbomPath).Hash -eq $beforeHash -and $unicodeAudit.sbom_sha256 -ceq $beforeHash.ToLowerInvariant()) 'Audit preserves and binds original SBOM bytes'
    $bomPath=Join-Path $workspace 'bom-sbom.json';$bomOutput=Join-Path $workspace 'bom-audit.json'
    [IO.File]::WriteAllText($bomPath,($unicodeSbom | ConvertTo-Json -Depth 8),(New-Object Text.UTF8Encoding($true)))
    & $shell -NoProfile -NonInteractive -ExecutionPolicy Bypass -File (Join-Path (Split-Path $PSScriptRoot -Parent) 'scripts/audit-dependencies.ps1') -SbomPath $bomPath -OutputPath $bomOutput | Out-Null
    Assert ($LASTEXITCODE -eq 0) 'A UTF-8 BOM is accepted without changing semantic input'
    $bomAudit=[IO.File]::ReadAllText($bomOutput) | ConvertFrom-Json
    Assert ($bomAudit.sbom_sha256 -ceq (Get-FileHash -LiteralPath $bomPath).Hash.ToLowerInvariant() -and $bomAudit.records[0].name -ceq $unicodeName) 'Exact original byte snapshot, including BOM, is bound before database queries'
} finally {
    $full=[IO.Path]::GetFullPath($workspace);$temporary=[IO.Path]::GetFullPath([IO.Path]::GetTempPath())
    if(-not $full.StartsWith($temporary,[StringComparison]::OrdinalIgnoreCase)){throw 'Unsafe audit test cleanup'}
    Remove-Item -LiteralPath $full -Recurse -Force
}
'Dependency audit contracts passed: exact identities, pagination, findings, outages, malformed responses and honest coverage.'
