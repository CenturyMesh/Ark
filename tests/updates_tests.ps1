$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$repository = Split-Path $PSScriptRoot -Parent
Import-Module (Join-Path $repository 'updates/Ark.Update.psm1') -Force
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
$script:passed = 0
function Assert-Equal($Actual, $Expected, [string]$Name) {
    if ($Actual -cne $Expected) { throw "$Name`: expected [$Expected], got [$Actual]" }
    $script:passed++
}
function Assert-Rejected([scriptblock]$Operation, [string]$Name) {
    $rejected = $false
    try { & $Operation | Out-Null } catch { $rejected = $true }
    if (-not $rejected) { throw "$Name`: operation should have been rejected" }
    $script:passed++
}
function New-Fixture([string]$Path, [string]$Version) {
    [IO.Directory]::CreateDirectory($Path) | Out-Null
    $required = @('ark-studio.exe','ark-cli.exe','ark-mcp.exe','ark-occt-worker.exe','TKernel.dll','TKBRep.dll','TKDESTEP.dll','assets/ui/ark-wordmark.png','assets/ui/ark-tool-atlas-v1.png','assets/ui/ark-detail-atlas-v1.png','assets/ui/ark-domain-atlas-v1.png','updates/ark-update.ps1','updates/ark-launch.ps1','updates/Ark.Update.psm1')
    foreach ($name in $required) {
        $target = Join-Path $Path $name
        [IO.Directory]::CreateDirectory((Split-Path $target -Parent)) | Out-Null
        [IO.File]::WriteAllText($target, "test fixture $Version $name")
    }
    Write-ArkAtomicJson (Join-Path $Path 'app-version.json') @{ schema=1; product='ARK'; version=$Version; platform='windows-x64'; entrypoint='ark-studio.exe' }
}
function New-Manifest([string]$Archive, [string]$Version) {
    [pscustomobject]@{ schema=1; product='ARK'; version=$Version; platform='windows-x64'; package=[pscustomobject]@{ name="ark-windows-x64-$Version.zip"; url="https://github.com/CenturyMesh/Ark/releases/download/v$Version/ark-windows-x64-$Version.zip"; size=(Get-Item -LiteralPath $Archive).Length; sha256=(Get-FileHash -LiteralPath $Archive -Algorithm SHA256).Hash.ToLowerInvariant() } }
}
function New-AdversarialZip([string]$Path, [string[]]$Names) {
    $zip = [IO.Compression.ZipFile]::Open($Path, 'Create')
    try { foreach ($name in $Names) { $entry=$zip.CreateEntry($name); $stream=$entry.Open(); $stream.WriteByte(65); $stream.Dispose() } }
    finally { $zip.Dispose() }
}
$temporary = Assert-ArkChildPath ([IO.Path]::GetTempPath()) (Join-Path ([IO.Path]::GetTempPath()) ('ark-update-tests-' + [guid]::NewGuid().ToString('N')))
[IO.Directory]::CreateDirectory($temporary) | Out-Null
try {
    Assert-Equal (Test-ArkNewerVersion '0.10.0' '0.9.9') $true 'Numeric version ordering'
    Assert-Equal (Test-ArkNewerVersion '0.9.1' '0.9.1') $false 'Equal version is not an update'
    Assert-Equal (Test-ArkNewerVersion '0.9.0' '0.9.1') $false 'Downgrade rejected'
    foreach ($invalid in @('v1.2.3','01.2.3','1.2','1.2.3-beta','1.2.3;calc','999999999999.0.0')) { Assert-Rejected { ConvertTo-ArkVersion $invalid } "Reject version $invalid" }
    foreach ($invalid in @('../escape','/absolute','C:/absolute','foo\bar','folder/../escape','folder/file:stream','NUL.txt','folder/CON','file.','file ','folder//file')) { Assert-Rejected { Assert-ArkRelativePath $invalid } "Reject path $invalid" }
    Assert-Rejected { Assert-ArkAssetUrl 'https://github.com/Other/Ark/releases/download/v1.2.3/ark-update.json' '1.2.3' 'ark-update.json' } 'Reject other repository'
    Assert-ArkAssetUrl 'https://github.com/CenturyMesh/Ark/releases/download/v1.2.3/ark-update.json' '1.2.3' 'ark-update.json'
    $script:passed++
    Assert-ArkTransferUrl ([uri]'https://api.github.com/repos/CenturyMesh/Ark/releases/latest') $true
    $script:passed++
    Assert-ArkTransferUrl ([uri]'https://github.com/CenturyMesh/Ark/releases/download/v1.2.3/ark-windows-x64-1.2.3.zip')
    $script:passed++
    Assert-Rejected { Assert-ArkAssetUrl 'https://github.com/CenturyMesh/Ark-Releases/releases/download/v1.2.3/ark-update.json' '1.2.3' 'ark-update.json' } 'Reject retired placeholder asset channel'
    Assert-Rejected { Assert-ArkTransferUrl ([uri]'https://api.github.com/repos/CenturyMesh/Ark-Releases/releases/latest') $true } 'Reject retired placeholder API'
    Assert-Rejected { Assert-ArkTransferUrl ([uri]'https://github.com/CenturyMesh/Ark-Releases/releases/download/v1.2.3/a.zip') } 'Reject retired placeholder transfer channel'
    Assert-Rejected { Assert-ArkTransferUrl ([uri]'https://github.com/CenturyMesh/Ark-Other/releases/download/v1.2.3/a.zip') } 'Reject similarly prefixed repository'
    Assert-Rejected { Assert-ArkAssetUrl 'https://github.com/CenturyMesh/Ark/releases/download/v1.2.3/ark-update.json?source=other' '1.2.3' 'ark-update.json' } 'Reject non-exact asset URL'
    Assert-Rejected { Assert-ArkTransferUrl ([uri]'https://api.github.com/repos/CenturyMesh/Ark/releases/latest?other=true') $true } 'Reject non-exact API URL'
    Assert-Rejected { Assert-ArkTransferUrl ([uri]'http://github.com/CenturyMesh/Ark/releases/download/v1.2.3/a.zip') } 'Reject HTTP'
    Assert-Rejected { Assert-ArkTransferUrl ([uri]'https://evil.example/a.zip') $false $true } 'Reject external redirect'
    Assert-Rejected { Assert-ArkChildPath $temporary (Join-Path $temporary '..\outside') } 'Reject install path escape'
    $launchArguments = @('--load', 'C:\drawings\peça com espaços.ark', 'literal "quote"', 'ends\', '', '--exit-after-ms', '3000')
    $payload = @{ arguments=$launchArguments; workingDirectory='C:\' } | ConvertTo-Json -Compress
    $encoded = [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($payload))
    $decoded = ConvertFrom-ArkLaunchPayload $encoded
    Assert-Equal $decoded.workingDirectory 'C:\' 'Base64 preserves root working directory'
    for ($index=0; $index -lt $launchArguments.Count; $index++) { Assert-Equal $decoded.arguments[$index] $launchArguments[$index] 'Base64 preserves exact argv' }
    Assert-Equal (ConvertTo-ArkWindowsArgument 'ends\') '"ends\\"' 'Windows argument doubles trailing backslash'
    Assert-Equal (ConvertTo-ArkWindowsArgument 'literal "quote"') '"literal \"quote\""' 'Windows argument escapes embedded quotes'
    Assert-Equal (ConvertTo-ArkWindowsArgument '') '""' 'Windows empty argument preserved'
    $baseline = Join-Path $temporary 'baseline'
    $candidate = Join-Path $temporary 'candidate'
    $install = Join-Path $temporary 'install'
    New-Fixture $baseline '0.9.0'
    New-Fixture $candidate '0.9.1'
    [IO.File]::WriteAllText((Join-Path $baseline 'Important.ark'), 'keep original user document')
    Initialize-ArkInstall $baseline $install | Out-Null
    Assert-Equal (Get-ArkState $install).current '0.9.0' 'Baseline registered'
    Assert-Equal (Test-Path -LiteralPath (Join-Path $install 'versions/0.9.0/Important.ark')) $false 'User drawing excluded from runtime'
    Assert-Equal ([IO.File]::ReadAllText((Join-Path $baseline 'Important.ark'))) 'keep original user document' 'Source drawing preserved'
    $archive = Join-Path $temporary 'valid.zip'
    Compress-ArkPackage $candidate $archive
    $manifest = New-Manifest $archive '0.9.1'
    Assert-ArkManifest $manifest '0.9.1'
    $badManifest = New-Manifest $archive '0.9.1'; $badManifest.package.sha256='0' * 64
    Assert-Rejected { Install-ArkStagedPackage $archive $badManifest $install } 'Reject SHA256 mismatch'
    Assert-Equal (Get-ArkState $install).pending '' 'Failed download leaves pointer unchanged'
    Install-ArkStagedPackage $archive $manifest $install | Out-Null
    Assert-Equal (Get-ArkState $install).current '0.9.0' 'Stage leaves running version unchanged'
    Assert-Equal (Get-ArkState $install).pending '0.9.1' 'Stage records next launch'
    Assert-Rejected { Install-ArkStagedPackage $archive (New-Manifest $archive '0.9.0') $install } 'Reject older or equal package'
    $activation = Start-ArkActivation $install
    Assert-Equal $activation.current '0.9.1' 'Activate candidate'
    Assert-Equal $activation.activating '0.9.1' 'Unconfirmed activation recorded'
    Complete-ArkActivation $install $false | Out-Null
    Assert-Equal (Get-ArkState $install).current '0.9.0' 'Failed start rolls back'
    Install-ArkStagedPackage $archive $manifest $install | Out-Null
    Assert-Equal (Get-ArkState $install).pending '0.9.1' 'Verified retained version can be staged again after rollback'
    $state=Get-ArkState $install; $state.pending='0.9.1'; Write-ArkAtomicJson (Join-Path $install 'current.json') $state
    Start-ArkActivation $install | Out-Null
    Complete-ArkActivation $install $true | Out-Null
    Assert-Equal (Get-ArkState $install).current '0.9.1' 'Confirmed start retained'
    Assert-Equal (Get-ArkState $install).previous '0.9.0' 'Previous runtime retained'
    Undo-ArkUpdate $install | Out-Null
    Assert-Equal (Get-ArkState $install).current '0.9.0' 'Manual rollback switches pointer'
    $state=Get-ArkState $install; $state.pending='0.9.1'; Write-ArkAtomicJson (Join-Path $install 'current.json') $state
    Start-ArkActivation $install | Out-Null
    Assert-Equal (Start-ArkActivation $install).current '0.9.0' 'Interrupted activation recovers'
    $state=Get-ArkState $install; $state.pending='0.9.1'; Write-ArkAtomicJson (Join-Path $install 'current.json') $state
    [IO.File]::AppendAllText((Join-Path $install 'versions/0.9.1/ark-studio.exe'), 'corruption')
    Assert-Rejected { Assert-ArkInstalledRuntime $install '0.9.1' } 'Detect staged disk corruption'
    Assert-Equal (Start-ArkActivation $install -WarningAction SilentlyContinue).current '0.9.0' 'Corrupt staged runtime retains baseline'
    Assert-Equal (Get-ArkState $install).pending '' 'Corrupt pending pointer cleared'
    $extra=Join-Path $install 'versions/0.9.0/injected.dll'
    [IO.File]::WriteAllText($extra, 'unexpected runtime file')
    Assert-Rejected { Assert-ArkInstalledRuntime $install '0.9.0' } 'Unexpected runtime file rejected'
    Remove-Item -LiteralPath $extra
    $lock = Enter-ArkUpdateLock $install
    try { Assert-Rejected { Enter-ArkUpdateLock $install } 'Concurrent updates rejected' }
    finally { $lock.Dispose() }
    foreach ($names in @(@('../escape.txt'), @('FILE.txt','file.txt'), @('dir/file.txt','dir/../file.txt'))) {
        $malicious = Join-Path $temporary ([guid]::NewGuid().ToString('N') + '.zip')
        New-AdversarialZip $malicious $names
        $target = Join-Path $temporary ([guid]::NewGuid().ToString('N'))
        Assert-Rejected { Expand-ArkPackage $malicious $target } 'Archive path/collision rejection'
    }
    Assert-Equal (Test-Path -LiteralPath (Join-Path $temporary 'escape.txt')) $false 'Archive extraction never escapes staging'
    $lying=Join-Path $temporary 'lying-size.zip'
    New-AdversarialZip $lying @('file.txt')
    $bytes=[IO.File]::ReadAllBytes($lying)
    for ($offset=0; $offset -lt $bytes.Length-28; $offset++) {
        if ($bytes[$offset] -eq 0x50 -and $bytes[$offset+1] -eq 0x4b -and $bytes[$offset+2] -eq 1 -and $bytes[$offset+3] -eq 2) {
            foreach ($delta in 24..27) { $bytes[$offset+$delta]=0 }
            break
        }
    }
    [IO.File]::WriteAllBytes($lying,$bytes)
    Assert-Rejected { Expand-ArkPackage $lying (Join-Path $temporary 'lying-target') } 'Actual extracted bytes cannot exceed declared ZIP size'
    $missing = Join-Path $temporary 'missing'
    New-Fixture $missing '0.9.2'
    Remove-Item -LiteralPath (Join-Path $missing 'ark-occt-worker.exe')
    Assert-Rejected { Get-ArkRuntimeVersion $missing } 'Incomplete native CAD runtime rejected'
    $mixedMsvc = Join-Path $temporary 'mixed-msvc'
    New-Fixture $mixedMsvc '0.9.2'
    [IO.File]::WriteAllText((Join-Path $mixedMsvc 'libTKBRep.dll'), 'foreign naming family')
    Assert-Rejected { Get-ArkRuntimeVersion $mixedMsvc } 'MSVC family rejects a partial foreign OCCT family'
    $llvm = Join-Path $temporary 'llvm'
    New-Fixture $llvm '0.9.2'
    foreach ($name in @('TKernel.dll','TKBRep.dll','TKDESTEP.dll')) {
        Move-Item -LiteralPath (Join-Path $llvm $name) -Destination (Join-Path $llvm ('lib' + $name))
    }
    foreach ($name in @('libc++.dll','libunwind.dll')) { [IO.File]::WriteAllText((Join-Path $llvm $name), 'matching synthetic runtime') }
    Assert-Equal (Get-ArkRuntimeVersion $llvm) '0.9.2' 'Complete LLVM-MinGW runtime accepted with actual DLL names'
    $llvmArchive = Join-Path $temporary 'llvm.zip'
    Compress-ArkPackage $llvm $llvmArchive
    Install-ArkStagedPackage $llvmArchive (New-Manifest $llvmArchive '0.9.2') $install | Out-Null
    Assert-Equal (Get-ArkState $install).pending '0.9.2' 'LLVM-MinGW package stages through the same verified updater'
    Assert-ArkInstalledRuntime $install '0.9.2' | Out-Null
    $script:passed++
    [IO.File]::WriteAllText((Join-Path $llvm 'TKBRep.dll'), 'wrong naming family')
    Assert-Rejected { Get-ArkRuntimeVersion $llvm } 'Mixed OCCT runtime families rejected'
    Remove-Item -LiteralPath (Join-Path $llvm 'TKBRep.dll')
    Remove-Item -LiteralPath (Join-Path $llvm 'libunwind.dll')
    Assert-Rejected { Get-ArkRuntimeVersion $llvm } 'Missing LLVM runtime dependency rejected'
    Remove-Item -LiteralPath (Join-Path $llvm 'libTKBRep.dll')
    Assert-Rejected { Get-ArkRuntimeVersion $llvm } 'Partial MinGW OCCT runtime rejected'
    # Completed baseline without state simulates a crash between move and pointer write.
    $recovered = Join-Path $temporary 'recovered'
    [IO.Directory]::CreateDirectory((Join-Path $recovered 'versions')) | Out-Null
    Copy-Item -LiteralPath (Join-Path $install 'versions/0.9.0') -Destination (Join-Path $recovered 'versions/0.9.0') -Recurse
    Initialize-ArkInstall $baseline $recovered | Out-Null
    Assert-Equal (Get-ArkState $recovered).current '0.9.0' 'Interrupted baseline registration recovers'
    Remove-Item -LiteralPath (Join-Path $recovered 'launcher/ark-launch.ps1')
    Initialize-ArkInstall $baseline $recovered | Out-Null
    Assert-Equal (Test-Path -LiteralPath (Join-Path $recovered 'launcher/ark-launch.ps1')) $true 'Interrupted launcher copy recovers'
    Initialize-ArkInstall $candidate $recovered | Out-Null
    Assert-Equal (Get-ArkState $recovered).pending '0.9.1' 'Explicit newer portable runtime stages over installed version'
    $malicious=Join-Path $temporary 'symlink.zip'
    $zip=[IO.Compression.ZipFile]::Open($malicious, 'Create')
    try { $entry=$zip.CreateEntry('link'); $entry.ExternalAttributes=-1610612736 }
    finally { $zip.Dispose() }
    Assert-Rejected { Expand-ArkPackage $malicious (Join-Path $temporary 'symlink-target') } 'Archive symbolic links rejected'
    $many = Join-Path $temporary 'many.zip'
    New-AdversarialZip $many (0..4096 | ForEach-Object { "file$_.txt" })
    Assert-Rejected { Expand-ArkPackage $many (Join-Path $temporary 'many-target') } 'Archive entry count is bounded'
    Write-Output "ARK OTA offline checks: $script:passed passed. No network or application launch used."
} finally { Remove-Item -LiteralPath (Assert-ArkChildPath ([IO.Path]::GetTempPath()) $temporary) -Recurse -Force }
