Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Get-ArkDependencyQuery($Component) {
    if ($Component.'bom-ref'.StartsWith('runtime:')) { return $null }
    $reference = @(if ($Component.PSObject.Properties['externalReferences']) { $Component.externalReferences | Where-Object { $_.type -eq 'vcs' } | Select-Object -First 1 })
    if ($reference.Count -and $reference[0].url -match '^(https://github\.com/[^/]+/[^/]+)/tree/([^/]+)$') {
        $repositoryUrl = $Matches[1]
        $revision = $Matches[2]
        if ($revision -cmatch '^[a-f0-9]{40}$') { return @{commit=$revision} }
        return @{package=@{ecosystem='GIT';name=$repositoryUrl};version=$revision}
    }
    if ($Component.name -eq 'SQLite') { return @{package=@{ecosystem='OSS-Fuzz';name='sqlite3'};version=$Component.version} }
    return $null
}

function Invoke-ArkDependencyAudit($Sbom, [scriptblock]$Query = {
    param($Request)
    Invoke-RestMethod -Uri 'https://api.osv.dev/v1/query' -Method Post -ContentType 'application/json' -Body ($Request | ConvertTo-Json -Depth 10 -Compress) -TimeoutSec 30 -MaximumRedirection 0
}) {
    if ($Sbom.bomFormat -ne 'CycloneDX' -or $Sbom.specVersion -ne '1.5') { throw 'Expected the generated CycloneDX 1.5 SBOM.' }
    $records = @()
    foreach ($component in $Sbom.components) {
        $request = Get-ArkDependencyQuery $component
        $record = [ordered]@{component=$component.'bom-ref';name=$component.name;status='not_queried';query=$(if($request){$request.Clone()}else{$null});pages=0;vulnerabilities=@()}
        if (-not $request) {
            $record.reason='No supported public database identity/source revision. Bundled runtime hashes remain in the SBOM; no clean result inferred.'
            $records += [pscustomobject]$record
            continue
        }
        $tokens = New-Object 'Collections.Generic.HashSet[string]' ([StringComparer]::Ordinal)
        $vulnerabilities = @{}
        try {
            do {
                $answer = & $Query $request
                $record.pages++
                if ($null -eq $answer -or $answer -isnot [pscustomobject] -or $answer.PSObject.Properties['error']) { throw 'Malformed OSV response.' }
                foreach ($property in $answer.PSObject.Properties) {
                    if ($property.Name -notin @('vulns','next_page_token')) { throw 'Unexpected OSV response field.' }
                }
                if ($answer.PSObject.Properties['vulns']) {
                    if ($answer.vulns -isnot [array]) { throw 'OSV vulnerabilities must be an array.' }
                    foreach ($finding in $answer.vulns) {
                        if ($null -eq $finding -or $finding -isnot [pscustomobject] -or -not $finding.PSObject.Properties['id'] -or $finding.id -isnot [string] -or [string]::IsNullOrWhiteSpace($finding.id)) { throw 'OSV finding has no string ID.' }
                        $vulnerabilities[$finding.id]=$finding
                    }
                }
                if ($answer.PSObject.Properties['next_page_token'] -and $answer.next_page_token -isnot [string]) { throw 'OSV pagination token must be a string.' }
                $next = if ($answer.PSObject.Properties['next_page_token']) { $answer.next_page_token } else { '' }
                if ($next) {
                    if (-not $tokens.Add($next) -or $record.pages -ge 32) { throw 'OSV pagination did not complete.' }
                    $request.page_token=$next
                }
            } while ($next)
            $record.status=if($vulnerabilities.Count){'findings_returned'}else{'no_records_returned'}
        } catch {
            $record.status='query_failed'
            # Exception messages can include proxy/user paths. Keep the public
            # receipt deterministic and let the caller retain local diagnostics.
            $record.reason='Database query or response validation failed; no clean result inferred.'
        }
        $record.vulnerabilities=@($vulnerabilities.Values | Sort-Object id)
        $records += [pscustomobject]$record
    }
    [pscustomobject][ordered]@{
        schema='ark.dependency-audit.v1';queried_at_utc=[DateTime]::UtcNow.ToString('o');database='https://api.osv.dev/v1/query';
        records=$records;query_failures=@($records | Where-Object {$_.status -eq 'query_failed'}).Count;
        findings=@($records | ForEach-Object {$_.vulnerabilities} | Select-Object -ExpandProperty id -Unique).Count;
        unqueried_components=@($records | Where-Object {$_.status -eq 'not_queried'}).Count;
        scope='Public upstream commit/tag or OSS-Fuzz package/version queries only. Empty results do not prove project/version indexing, exploitability or absence of vulnerabilities. Runtime source revisions, transitive dependencies and OS/GPU components need separate review. Findings require triage through SECURITY.md.'
    }
}

Export-ModuleMember -Function Get-ArkDependencyQuery,Invoke-ArkDependencyAudit
