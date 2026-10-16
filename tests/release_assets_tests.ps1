$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$repository = Split-Path $PSScriptRoot -Parent
$windows = [Environment]::OSVersion.Platform -eq [PlatformID]::Win32NT
$bash = if ($windows) { Join-Path $env:ProgramFiles 'Git/bin/bash.exe' } else { (Get-Command bash -ErrorAction Stop).Source }
if (-not (Test-Path -LiteralPath $bash -PathType Leaf)) { throw 'Release attachment checks require Bash (Git Bash on Windows).' }

# Exercise the real publisher's asset/checksum block with synthetic files, then
# flatten them exactly as GitHub release downloads do. No network or CAD build.
$workflow = [IO.File]::ReadAllLines((Join-Path $repository '.github/workflows/release-package.yml'))
$begin = -1
$end = -1
for ($i = 0; $i -lt $workflow.Length; $i++) {
    if ($workflow[$i].TrimStart().StartsWith('assets=(')) { $begin = $i }
    if ($workflow[$i].TrimStart().StartsWith('gh release create ')) { $end = $i; break }
}
if ($begin -lt 0 -or $end -le $begin) { throw 'Cannot locate the release attachment block.' }
$body = ($workflow[$begin..($end - 1)] -replace '^ {10}', '') -join "`n"
$temporaryParent = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd([IO.Path]::DirectorySeparatorChar)
$fixture = [IO.Path]::GetFullPath((Join-Path $temporaryParent ('ark-release-assets-' + [guid]::NewGuid().ToString('N'))))
$expectedFixture = $fixture
[IO.Directory]::CreateDirectory((Join-Path $fixture 'linux-release-evidence')) | Out-Null
$files = @('ark-windows-x64-0.9.33.zip', 'ark-linux-x64-headless-0.9.33.tar.gz', 'OCCT-V8_0_1-source.zip', 'ark-update.json', 'ark-windows-x64-0.9.33.zip.provenance.json', 'ark-linux-x64-headless-0.9.33.tar.gz.provenance.json', 'windows-dependency-audit.json', 'linux-release-evidence/sbom.cdx.json', 'linux-release-evidence/artifact-provenance.json', 'ARK-032D-windows-x64-setup.exe')
$utf8 = New-Object Text.UTF8Encoding($false)
try {
    foreach ($file in $files) { [IO.File]::WriteAllText((Join-Path $fixture $file), ('Synthetic attachment: ' + $file), $utf8) }
    $script = "set -euo pipefail`n" + $body + @'

mkdir -p downloaded
for asset in "${assets[@]}"; do cp -- "$asset" downloaded/; done
(cd downloaded && sha256sum -c SHA256SUMS.txt)
'@
    [IO.File]::WriteAllText((Join-Path $fixture 'check.sh'), $script, $utf8)
    Push-Location $fixture
    try {
        & $bash --noprofile --norc ./check.sh
        if ($LASTEXITCODE -ne 0) { throw 'Downloaded release attachments did not match their checksums.' }
        $checksums = [IO.File]::ReadAllText((Join-Path $fixture 'downloaded/SHA256SUMS.txt'))
        foreach ($file in $files) {
            $name = [IO.Path]::GetFileName($file)
            if ($checksums -notmatch ('(?m)^[a-f0-9]{64} [ *]' + [regex]::Escape($name) + '\r?$')) { throw "Missing downloaded attachment checksum: $name" }
        }
        # Two distinct source paths cannot silently become the same attachment.
        [IO.File]::WriteAllText((Join-Path $fixture 'linux-release-evidence/windows-dependency-audit.json'), 'collision', $utf8)
        $previousPreference = $ErrorActionPreference
        try { $ErrorActionPreference = 'Continue'; $rejected = & $bash --noprofile --norc ./check.sh 2>&1; $rejectedExit = $LASTEXITCODE } finally { $ErrorActionPreference = $previousPreference }
        if ($rejectedExit -eq 0 -or ($rejected -join "`n") -notmatch 'Duplicate release attachment name') { throw 'Duplicate attachment names must reject publication.' }
        # GitHub's pwsh wrapper propagates LASTEXITCODE. The nonzero exit above
        # is an asserted negative case, not this test script's final outcome.
        $global:LASTEXITCODE = 0
    } finally { Pop-Location }
    Write-Output 'Release attachments: flattened SHA-256 verification, update manifest, optional installer and duplicate-name rejection passed.'
} finally {
    $resolvedFixture = [IO.Path]::GetFullPath($fixture)
    if ($resolvedFixture -cne $expectedFixture -or [IO.Path]::GetDirectoryName($resolvedFixture) -cne $temporaryParent) { throw 'Refusing cleanup outside the owned release fixture.' }
    if (Test-Path -LiteralPath $resolvedFixture) { Remove-Item -LiteralPath $resolvedFixture -Recurse -Force }
}
