param([Parameter(Mandatory=$true)][string]$Executable)
$ErrorActionPreference = 'Stop'
$benchmarkExe = (Resolve-Path -LiteralPath $Executable).Path
$benchmarkRoot = Split-Path -Parent $PSScriptRoot
$benchmarkArtifacts = Join-Path $benchmarkRoot ('out/verification/benchmark-paths-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path (Join-Path $benchmarkArtifacts 'benchmarks') -Force | Out-Null
# A conflicting working-directory path must not shadow bundled resources.
Set-Content -LiteralPath (Join-Path $benchmarkArtifacts 'benchmarks/agent_briefs_v1.json') -Value 'invalid JSON on purpose' -Encoding UTF8
Set-Content -LiteralPath (Join-Path $benchmarkArtifacts 'benchmarks/offline.json') -Value 'invalid JSON on purpose' -Encoding UTF8
$previousLocation = Get-Location
try {
    Set-Location -LiteralPath $benchmarkArtifacts
    $preflightText = & $benchmarkExe --preflight
    if ($LASTEXITCODE -ne 0) { throw 'Bundled native preflight failed from an unrelated working directory' }
    $preflight = ($preflightText -join "`n") | ConvertFrom-Json
    if (-not $preflight.passed -or $preflight.cases.Count -ne 30) { throw 'Bundled preflight did not evaluate all thirty cases' }
    # The caller-relative output parent must be created in this working directory.
    $relativeReceipt = 'caller-receipts/nested/preflight.jsonl'
    $receiptText = & $benchmarkExe --preflight --out $relativeReceipt
    if ($LASTEXITCODE -ne 0) { throw 'Could not create the caller-relative benchmark receipt' }
    $receiptPath = Join-Path $benchmarkArtifacts $relativeReceipt
    if (-not (Test-Path -LiteralPath $receiptPath -PathType Leaf)) { throw 'Explicit output was not written at the caller-relative path' }
    $receiptLines = @(Get-Content -LiteralPath $receiptPath)
    if ($receiptLines.Count -ne 1) { throw 'Preflight must persist exactly one JSONL receipt' }
    $receipt = $receiptLines[0] | ConvertFrom-Json
    if (-not $receipt.passed -or $receipt.cases.Count -ne 30 -or $receiptLines[0] -ne ($receiptText -join "`n")) { throw 'Persisted preflight receipt does not match its reported result' }
    $offlineText = & $benchmarkExe --cases B01 --repetitions 1
    if ($LASTEXITCODE -ne 0) { throw 'Bundled offline config failed from an unrelated working directory' }
    $offline = $offlineText[-1] | ConvertFrom-Json
    if ($offline.attempt_count -ne 3 -or $offline.accepted_count -ne 3 -or $offline.live) { throw 'Expected three accepted offline strategies' }
    $visionText = & $benchmarkExe --vision-smoke
    if ($LASTEXITCODE -ne 0) { throw 'Bundled synthetic vision fixture failed from an unrelated working directory' }
    $vision = ($visionText -join "`n") | ConvertFrom-Json
    if (-not $vision.completed -or -not $vision.document_unchanged -or $vision.captures.Count -lt 1) { throw 'Bundled fixture did not produce a real capture without a document mutation' }
    # Explicit paths keep their exact caller meaning, including failure.
    $ErrorActionPreference = 'Continue'
    $explicitText = & $benchmarkExe --preflight --suite benchmarks/agent_briefs_v1.json 2>&1
    $explicitExit = $LASTEXITCODE
    $ErrorActionPreference = 'Stop'
    if ($explicitExit -eq 0 -or ($explicitText -join "`n") -notmatch 'Invalid JSON: benchmarks/agent_briefs_v1.json') { throw 'Explicit suite incorrectly fell back to bundled resources' }
    $ErrorActionPreference = 'Continue'
    $explicitConfigText = & $benchmarkExe --cases B01 --repetitions 1 --config benchmarks/offline.json 2>&1
    $explicitConfigExit = $LASTEXITCODE
    $ErrorActionPreference = 'Stop'
    if ($explicitConfigExit -eq 0 -or ($explicitConfigText -join "`n") -notmatch 'Invalid JSON: benchmarks/offline.json') { throw 'Explicit config incorrectly fell back to bundled resources' }
    Write-Output 'Benchmark packaged paths passed: native preflight without output, caller-relative nested JSONL receipt, three offline strategies, synthetic vision, explicit-path preservation.'
} finally {
    Set-Location -LiteralPath $previousLocation
}
