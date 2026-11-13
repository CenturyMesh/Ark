param(
    [string]$OutputDirectory = 'out/installer-engine',
    # Also sign a private setup with a temporary self-signed code-signing certificate created in CurrentUser\My
    # (no trust store is changed) and removed afterwards.
    [switch]$Signing,
    [string]$Dotnet = 'dotnet'
)
# ARK Setup's install engine around a synthetic payload: placeholder runtime files with the repository's real
# Install-ARK.ps1 and updates\ module, so it needs no native build and runs on a hosted Windows runner. Every run is
# isolated (its own MeshSphere.ARK.Test.<id> entry, no shortcuts, folders under the fixture); the user's real
# registrations, shortcuts and launcher pointer are compared before and after.
# Covers: Inno switches and /LOG, silent errors without dialogs (including the trailing-backslash case), a public setup
# refusing the self-test renderer, non-empty foreign folders, reinstall repair of a damaged or missing runtime, a repair
# that dies or fails part-way (a package whose update module fails on purpose), downgrade refusal and
# --allow-downgrade (also over a prepared newer version), the Installed apps version after the launcher's activation, failed activation and rollback,
# interrupted activation, uninstall under update.lock, uninstall.exe /VERYSILENT, self-delete from a
# folder with % ! & in its name, payload tampering, discovery before a (synthetic) certificate table, leftover
# temporary folders and native library caches, and with -Signing a real Authenticode signature of setup and uninstaller.
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$repository = Split-Path $PSScriptRoot -Parent
Import-Module (Join-Path $repository 'updates/Ark.Update.psm1') -Force
Import-Module (Join-Path $repository 'scripts/Ark.SetupPayload.psm1') -Force
if (-not [IO.Path]::IsPathRooted($OutputDirectory)) { $OutputDirectory = Join-Path $repository $OutputDirectory }
$fixture = [IO.Path]::GetFullPath((Join-Path $OutputDirectory ('engine-' + [guid]::NewGuid().ToString('N'))))
$testId = 'engine-' + [guid]::NewGuid().ToString('N').Substring(0, 10)
$started = Get-Date
$checks = 0
function Assert-True([bool]$Value, [string]$Name) { if (-not $Value) { throw "FAILED: $Name" }; $script:checks++ }

$uninstallRoot = 'HKCU:/Software/Microsoft/Windows/CurrentVersion/Uninstall'
$registry = "$uninstallRoot/MeshSphere.ARK.Test.$testId"
function Get-Registration([string]$Name) {
    $path = "$uninstallRoot/$Name"
    if (-not (Test-Path -LiteralPath $path)) { return '' }
    return ((Get-ItemProperty -LiteralPath $path).PSObject.Properties | Where-Object { $_.Name -notlike 'PS*' } | Sort-Object Name | ForEach-Object { "$($_.Name)=$($_.Value)" }) -join ';'
}
$realRegistrations = @{}
foreach ($name in @('MeshSphere.ARK', 'MeshSphere.ARK_is1')) { $realRegistrations[$name] = Get-Registration $name }
$protected = @((Join-Path ([Environment]::GetFolderPath('Desktop')) 'ARK.lnk'), (Join-Path ([Environment]::GetFolderPath('Programs')) 'ARK/ARK.lnk'), (Join-Path $env:LOCALAPPDATA 'ARK/app/current.json')) | Where-Object { Test-Path -LiteralPath $_ }
$before = @{}
foreach ($path in $protected) { $before[$path] = (Get-FileHash -LiteralPath $path).Hash }

function Invoke-Setup([string]$Exe, [string]$Arguments, [int]$TimeoutSeconds = 300) {
    # One verbatim command line, as a script or the Installed apps entry would pass it. A dialog would block: timeout.
    $process = Start-Process -FilePath $Exe -ArgumentList $Arguments -WindowStyle Hidden -PassThru
    $null = $process.Handle
    if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
        & taskkill.exe /PID $process.Id /T /F | Out-Null
        throw "Timeout (a dialog may be waiting): $([IO.Path]::GetFileName($Exe)) $Arguments"
    }
    return $process.ExitCode
}
function Wait-Gone([string]$Path, [int]$Seconds = 60) {
    # uninstall.exe removes itself from a helper that runs after it exits
    $watch = [Diagnostics.Stopwatch]::StartNew()
    while (Test-Path -LiteralPath $Path) {
        if ($watch.Elapsed.TotalSeconds -gt $Seconds) { return $false }
        Start-Sleep -Milliseconds 250
    }
    return $true
}
function Get-Hash([string]$Path) { (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant() }
function Write-Fixture([string]$Path, [string]$Value = 'synthetic ARK runtime placeholder') {
    [IO.Directory]::CreateDirectory((Split-Path $Path -Parent)) | Out-Null
    [IO.File]::WriteAllText($Path, $Value)
}
function New-SyntheticPackage([string]$Version, [int]$Sequence, [switch]$Faulty) {
    $code = '{0:D3}A' -f $Sequence
    $suffix = if ($Faulty) { '-faulty' } else { '' }
    $runtime = Join-Path $fixture "package-$Version$suffix"
    foreach ($name in @('ark-studio.exe','ark-cli.exe','ark-mcp.exe','ark-occt-worker.exe','TKernel.dll','TKBRep.dll','TKDESTEP.dll','assets/ui/ark-wordmark.png','assets/ui/ark-tool-atlas-v1.png','assets/ui/ark-detail-atlas-v1.png','assets/ui/ark-domain-atlas-v1.png','licenses/LICENSE_LGPL_21.txt','licenses/OCCT_LGPL_EXCEPTION.txt','licenses/THIRD_PARTY_NOTICES.md','licenses/ARK-Apache-2.0.txt','licenses/ARK-NOTICE.txt')) { Write-Fixture (Join-Path $runtime $name) "synthetic $name $Version" }
    foreach ($name in @('Ark.Update.psm1','ark-launch.ps1','ark-update.ps1','ARK.cmd')) { Write-Fixture (Join-Path $runtime "updates/$name") ''; Copy-Item -LiteralPath (Join-Path $repository "updates/$name") -Destination (Join-Path $runtime "updates/$name") -Force }
    if ($Faulty) {
        # Setup's repair imports the package's own module: this one's runtime copy dies (ARK_ENGINE_TEST_FAULT=kill) or
        # fails after a partial copy, so the repair stops part-way
        $module = Join-Path $runtime 'updates/Ark.Update.psm1'
        $fault = "function Copy-ArkLocalRuntime([string]`$RuntimeDirectory, [string]`$Root) {`r`n    if (`$env:ARK_ENGINE_TEST_FAULT -ceq 'kill') { Stop-Process -Id `$PID -Force }`r`n    `$target = Join-Path `$Root ('versions\' + (Get-ArkRuntimeVersion `$RuntimeDirectory))`r`n    [IO.Directory]::CreateDirectory(`$target) | Out-Null`r`n    [IO.File]::WriteAllText((Join-Path `$target 'partial.txt'), 'partial copy')`r`n    throw 'Synthetic failure after a partial copy.'`r`n}`r`n`r`n"
        $text = [IO.File]::ReadAllText($module)
        if (-not $text.Contains('Export-ModuleMember -Function')) { throw 'The update module has no Export-ModuleMember line.' }
        [IO.File]::WriteAllText($module, $text.Replace('Export-ModuleMember -Function', $fault + 'Export-ModuleMember -Function'))
    }
    Copy-Item -LiteralPath (Join-Path $repository 'scripts/install-portable.ps1') -Destination (Join-Path $runtime 'Install-ARK.ps1')
    foreach ($name in @('Install-ARK.cmd','GETTING_STARTED.txt')) { Copy-Item -LiteralPath (Join-Path $repository "packaging/$name") -Destination $runtime }
    Write-ArkAtomicJson (Join-Path $runtime 'app-version.json') @{schema=1; product='ARK'; version=$Version; iteration_code=$code; sequence=$Sequence; phase='A'; milestones=@(); platform='windows-x64'; entrypoint='ark-studio.exe'}
    $zipDirectory = Join-Path $fixture "zip$suffix"
    [IO.Directory]::CreateDirectory($zipDirectory) | Out-Null
    $zip = Join-Path $zipDirectory "ark-windows-x64-$Version.zip"
    Compress-ArkPackage $runtime $zip
    return $zip
}
function Build-Setup([string]$Package, [string]$Output, [string[]]$Extra) {
    $arguments = @{PackagePath=$Package; OutputDirectory=$Output; UseRuntimeIdentity=$true; Dotnet=$Dotnet}
    foreach ($e in $Extra) { if ($e -eq 'public') { $arguments.AllowDirtyTree = $true } elseif ($e -like 'sign=*') { $arguments.SignScript = $e.Substring(5) } }
    if ($Extra -notcontains 'public') { $arguments.TestIsolationId = $testId }
    & (Join-Path $repository 'scripts/build-installer.ps1') @arguments | Out-Host
    $exe = @(Get-ChildItem -LiteralPath $Output -Filter 'ARK-*-windows-x64-setup*.exe')
    if ($exe.Count -ne 1) { throw "Expected one setup in $Output." }
    return $exe[0].FullName
}
function Assert-Installed([string]$Root, [string]$Version, [string]$Setup) {
    $state = Get-ArkState $Root
    $selected = if ($state.pending) { $state.pending } else { $state.current }
    Assert-True ($selected -ceq $Version) "launcher selects $Version in $Root (found $selected)"
    Assert-ArkInstalledRuntime $Root $Version | Out-Null
    $entry = Get-ItemProperty -LiteralPath $registry
    Assert-True ($entry.DisplayVersion -ceq $Version) "DisplayVersion $Version (found $($entry.DisplayVersion))"
    Assert-True ($entry.DisplayIcon -ceq ((Join-Path $Root 'uninstall.exe') + ',0')) "DisplayIcon is uninstall.exe,0 (found $($entry.DisplayIcon))"
    Assert-True ((Get-Hash (Join-Path $Root 'uninstall.exe')) -ceq (Get-ArkSetupStubHash $Setup (Find-ArkSetupPayload $Setup))) 'uninstall.exe is the stub with its own PE header fields'
    Assert-True (@(Get-ChildItem -LiteralPath $Root -Directory -Filter 'stage-*').Count -eq 0) 'no repair copy left behind'
}
function Uninstall-Root([string]$Root, [string]$Arguments = '--uninstall --quiet') {
    $uninstaller = Join-Path $Root 'uninstall.exe'
    Assert-True ((Invoke-Setup $uninstaller $Arguments) -eq 0) "uninstall.exe $Arguments in $Root"
    Assert-True (Wait-Gone $uninstaller) "uninstall.exe removed itself from $Root"
    Assert-True (-not (Test-Path -LiteralPath $registry)) 'the isolated entry is removed'
}

$certificate = $null
$passed = $false
try {
    [IO.Directory]::CreateDirectory($fixture) | Out-Null
    $new = New-SyntheticPackage '0.0.2' 991
    $old = New-SyntheticPackage '0.0.1' 990
    $setup = Build-Setup $new (Join-Path $fixture 'setup-new') @()
    $older = Build-Setup $old (Join-Path $fixture 'setup-old') @()
    $public = Build-Setup $new (Join-Path $fixture 'setup-public') @('public')
    $faulty = Build-Setup (New-SyntheticPackage '0.0.2' 991 -Faulty) (Join-Path $fixture 'setup-faulty') @()
    Assert-True ((Find-ArkSetupPayload $setup).Length -eq (Get-Item -LiteralPath $new).Length) 'footer locates the unchanged package'

    # --- arguments: no dialog ever blocks a silent run; the public setup has no self-test renderer
    $captures = Join-Path $fixture 'captures'
    Assert-True ((Invoke-Setup $public ('--silent --screenshot "' + $captures + '"') 60) -eq 2 -and -not (Test-Path -LiteralPath $captures)) 'a public setup rejects the self-test page renderer'
    Assert-True ((Invoke-Setup $setup '--silent --bogus' 60) -eq 2) 'an unknown argument fails without a dialog'
    $trailing = Join-Path $fixture 'trailing'
    Assert-True ((Invoke-Setup $setup ('--dir "' + $trailing + '\" /VERYSILENT') 60) -eq 2 -and -not (Test-Path -LiteralPath $trailing)) 'a backslash before the closing quote is refused without a dialog'
    Assert-True ((Invoke-Setup $setup '--silent --dir relative\folder' 60) -eq 2) 'a relative --dir is refused'
    Assert-True ((Invoke-Setup $setup '/VERYSILENT /ALLUSERS' 60) -eq 2) '/ALLUSERS is refused'
    $foreign = Join-Path $fixture 'foreign folder'
    Write-Fixture (Join-Path $foreign 'notes.txt') 'not ARK'
    Assert-True ((Invoke-Setup $setup ('--silent --dir "' + $foreign + '"')) -eq 2 -and @(Get-ChildItem -LiteralPath $foreign -Force).Count -eq 1) 'a non-empty folder that is not an ARK install is refused and left alone'

    # --- Inno switches, /LOG, install
    $root = Join-Path $fixture 'install %TEMP% !x! & more'
    $log = Join-Path $fixture 'inno.log'
    Assert-True ((Invoke-Setup $setup ('/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /SP- /CURRENTUSER /NOICONS /DIR="' + $root + '" /LOG="' + $log + '"')) -eq 0) 'Inno-style silent install'
    Assert-True ((Test-Path -LiteralPath $log) -and (Get-Content -LiteralPath $log -Raw) -match 'install finished') '/LOG=FILE receives the log'
    Assert-Installed $root '0.0.2' $setup
    Assert-True ((Read-ArkJson (Join-Path $root 'ark-setup.json') 64KB).shortcuts.Count -eq 0) 'no shortcuts in isolation'

    # --- reinstall repairs a damaged runtime
    $runtime = Join-Path $root 'versions/0.0.2'
    $state = [IO.File]::ReadAllText((Join-Path $root 'current.json'))
    Write-Fixture (Join-Path $runtime 'ark-cli.exe') 'damaged'
    Write-Fixture (Join-Path $runtime 'unexpected.dll') 'unexpected'
    $damaged = $false; try { Assert-ArkInstalledRuntime $root '0.0.2' | Out-Null } catch { $damaged = $true }
    Assert-True $damaged 'the damage is detectable'
    Assert-True ((Invoke-Setup $setup ('--silent --dir "' + $root + '"')) -eq 0) 'reinstall'
    Assert-Installed $root '0.0.2' $setup
    Assert-True (-not (Test-Path -LiteralPath (Join-Path $runtime 'unexpected.dll')) -and [IO.File]::ReadAllText((Join-Path $root 'current.json')) -ceq $state) 'reinstall rebuilt the runtime and kept the selection'

    # --- reinstall copies the selected runtime again when its folder is missing
    Remove-Item -LiteralPath $runtime -Recurse -Force
    Assert-True ((Invoke-Setup $setup ('--silent --dir "' + $root + '"')) -eq 0) 'reinstall with the selected runtime folder missing'
    Assert-Installed $root '0.0.2' $setup
    Assert-True ([IO.File]::ReadAllText((Join-Path $root 'current.json')) -ceq $state) 'the missing runtime was copied again and the selection kept'

    # --- a repair that dies or fails part-way never deletes the only copy of the selected runtime
    Write-Fixture (Join-Path $runtime 'ark-cli.exe') 'damaged'
    Write-Fixture (Join-Path $runtime 'unexpected.dll') 'unexpected'
    $env:ARK_ENGINE_TEST_FAULT = 'kill'
    try { $code = Invoke-Setup $faulty ('--silent --dir "' + $root + '"') } finally { Remove-Item -LiteralPath Env:ARK_ENGINE_TEST_FAULT -ErrorAction SilentlyContinue }
    Assert-True ($code -eq 7) "a repair killed part-way fails (exit $code)"
    Assert-True ((Test-Path -LiteralPath (Join-Path $runtime 'unexpected.dll')) -and @(Get-ChildItem -LiteralPath $root -Directory -Filter 'stage-*').Count -eq 0) 'the damaged copy is moved back, not deleted'
    $code = Invoke-Setup $faulty ('--silent --dir "' + $root + '"')
    $kept = @(Get-ChildItem -LiteralPath $root -Directory -Filter 'stage-*')
    Assert-True ($code -eq 7 -and $kept.Count -eq 1 -and (Test-Path -LiteralPath (Join-Path $runtime 'partial.txt'))) "a repair that fails after a partial copy fails (exit $code, $($kept.Count) aside)"
    Assert-True ((Test-Path -LiteralPath (Join-Path $kept[0].FullName 'unexpected.dll')) -and (Test-Path -LiteralPath (Join-Path $kept[0].FullName 'ark-studio.exe'))) 'the damaged copy is kept beside the partial one, not deleted'
    Assert-True ([IO.File]::ReadAllText((Join-Path $root 'current.json')) -ceq $state -and (Get-ItemProperty -LiteralPath $registry).DisplayVersion -ceq '0.0.2') 'the failed repairs kept the selection and the entry'
    Remove-Item -LiteralPath $kept[0].FullName -Recurse -Force
    Assert-True ((Invoke-Setup $setup ('--silent --dir "' + $root + '"')) -eq 0) 'reinstall repairs the partial copy'
    Assert-Installed $root '0.0.2' $setup

    # --- downgrade: refused, then explicit; then the newer setup updates again
    Assert-True ((Invoke-Setup $older ('--silent --dir "' + $root + '"')) -eq 8) 'an older setup refuses to downgrade'
    Assert-True ([IO.File]::ReadAllText((Join-Path $root 'current.json')) -ceq $state -and (Get-ItemProperty -LiteralPath $registry).DisplayVersion -ceq '0.0.2') 'the refused downgrade changed nothing'
    Assert-True ((Invoke-Setup $older ('--silent --allow-downgrade --dir "' + $root + '"')) -eq 0) '--allow-downgrade'
    Assert-Installed $root '0.0.1' $older
    $downgraded = Get-ArkState $root
    Assert-True ($downgraded.current -ceq '0.0.1' -and $downgraded.previous -ceq '0.0.2' -and -not $downgraded.pending) 'the downgrade keeps the newer version as previous'
    Assert-True ((Invoke-Setup $setup ('--silent --dir "' + $root + '"')) -eq 0) 'update after the downgrade'
    Assert-Installed $root '0.0.2' $setup
    # a prepared (pending) newer version also refuses the downgrade, and --allow-downgrade keeps it as previous
    Write-ArkAtomicJson (Join-Path $root 'current.json') @{schema=1; current='0.0.1'; previous=''; pending='0.0.2'; activating=''}
    Assert-True ((Invoke-Setup $older ('--silent --dir "' + $root + '"')) -eq 8) 'a prepared newer version refuses the downgrade'
    Assert-True ((Invoke-Setup $older ('--silent --allow-downgrade --dir "' + $root + '"')) -eq 0) '--allow-downgrade over a prepared newer version'
    Assert-Installed $root '0.0.1' $older
    $downgraded = Get-ArkState $root
    Assert-True ($downgraded.current -ceq '0.0.1' -and $downgraded.previous -ceq '0.0.2' -and -not $downgraded.pending) "the prepared newer version is kept as previous (current $($downgraded.current), previous $($downgraded.previous), pending $($downgraded.pending))"
    Assert-True ((Invoke-Setup $setup ('--silent --dir "' + $root + '"')) -eq 0) 'prepare 0.0.2 again after the downgrade'
    Assert-Installed $root '0.0.2' $setup

    # --- the launcher's activation, a failed activation and a rollback keep the Installed apps version in step
    #     (Ark.Update.psm1 is the module the synthetic package installed as launcher\Ark.Update.psm1)
    $displayed = { [string](Get-ItemProperty -LiteralPath $registry).DisplayVersion }
    Start-ArkActivation $root | Out-Null
    Complete-ArkActivation $root $false | Out-Null
    Assert-True ((Get-ArkState $root).current -ceq '0.0.1' -and (& $displayed) -ceq '0.0.1') "a failed activation shows the restored version (found $(& $displayed))"
    Assert-True ((Invoke-Setup $setup ('--silent --dir "' + $root + '"')) -eq 0) 'prepare 0.0.2 again'
    Start-ArkActivation $root | Out-Null
    Complete-ArkActivation $root $true | Out-Null
    Assert-True ((& $displayed) -ceq '0.0.2') 'an acknowledged activation shows the new version'
    Assert-True ((Undo-ArkUpdate $root) -ceq '0.0.1' -and (& $displayed) -ceq '0.0.1') "a rollback shows the restored version (found $(& $displayed))"
    Assert-True ((Invoke-Setup $setup ('--silent --dir "' + $root + '"')) -eq 0) 'update after the rollback'
    Assert-Installed $root '0.0.2' $setup

    # --- an interrupted activation is reported, not overwritten
    $current = Get-ArkState $root
    $saved = [IO.File]::ReadAllText((Join-Path $root 'current.json'))
    Write-ArkAtomicJson (Join-Path $root 'current.json') @{schema=1; current=$current.current; previous=''; pending=''; activating=$current.current}
    Assert-True ((Invoke-Setup $setup ('--silent --dir "' + $root + '"')) -eq 3) 'an interrupted activation stops Setup'
    [IO.File]::WriteAllText((Join-Path $root 'current.json'), $saved)

    # --- update.lock: install and uninstall fail early while a launch or update holds it
    $lock = [IO.File]::Open((Join-Path $root 'update.lock'), 'OpenOrCreate', 'ReadWrite', 'None')
    try {
        Assert-True ((Invoke-Setup $setup ('--silent --dir "' + $root + '"')) -eq 3) 'install waits for update.lock'
        Assert-True ((Invoke-Setup (Join-Path $root 'uninstall.exe') '--uninstall --quiet') -eq 3) 'uninstall fails early under update.lock'
        Assert-True ((Test-Path -LiteralPath (Join-Path $root 'versions')) -and (Test-Path -LiteralPath (Join-Path $root 'launcher')) -and (Test-Path -LiteralPath $registry)) 'nothing was removed under the lock'
    } finally { $lock.Dispose() }

    # --- leftovers of killed runs; a folder in use stays
    $temp = [IO.Path]::GetTempPath()
    $leftover = Join-Path $temp ('ark-setup-' + [guid]::NewGuid().ToString('N'))
    $busy = Join-Path $temp ('ark-setup-' + [guid]::NewGuid().ToString('N'))
    Write-Fixture (Join-Path $leftover 'runtime/app-version.json') '{}'
    Write-Fixture (Join-Path $busy 'setup.lock') ''
    $hold = [IO.File]::Open((Join-Path $busy 'setup.lock'), 'Open', 'ReadWrite', 'None')
    try {
        # uninstall.exe /VERYSILENT uninstalls quietly (Inno's unins000.exe semantics) and cleans leftovers first
        Uninstall-Root $root '/VERYSILENT /SUPPRESSMSGBOXES'
        Assert-True (-not (Test-Path -LiteralPath $leftover) -and -not (Test-Path -LiteralPath ($leftover + '.stale'))) 'a killed run''s temporary folder is removed'
        Assert-True (Test-Path -LiteralPath $busy) 'a temporary folder in use is kept'
    } finally { $hold.Dispose(); if (Test-Path -LiteralPath $busy) { Remove-Item -LiteralPath $busy -Recurse -Force } }
    Assert-True (Wait-Gone $root) 'the folder with % ! & in its name is removed once empty'

    # --- tampering and certificate tables
    $copies = Join-Path $fixture 'copies'
    [IO.Directory]::CreateDirectory($copies) | Out-Null
    $payload = Find-ArkSetupPayload $setup
    $tampered = Join-Path $copies "ark-engine-tamper-$testId.exe"
    Copy-Item -LiteralPath $setup -Destination $tampered
    $stream = [IO.File]::Open($tampered, 'Open', 'ReadWrite')
    try { $at = $payload.Offset + [long]($payload.Length / 2); [void]$stream.Seek($at, 'Begin'); $b = $stream.ReadByte(); [void]$stream.Seek($at, 'Begin'); $stream.WriteByte(($b -bxor 0xFF)) } finally { $stream.Dispose() }
    $tamperRoot = Join-Path $fixture 'tampered'
    Assert-True ((Invoke-Setup $tampered ('--silent --dir "' + $tamperRoot + '"')) -eq 4 -and -not (Test-Path -LiteralPath $tamperRoot)) 'a changed package byte is refused before anything is written'

    # a signed setup ends with its certificate table: a synthetic one (security directory patched) after alignment
    $signedCopy = Join-Path $copies "ark-engine-signed-$testId.exe"
    Copy-Item -LiteralPath $setup -Destination $signedCopy
    $layout = Get-ArkPeLayout $signedCopy
    $append = [IO.File]::Open($signedCopy, 'Append', 'Write')
    try {
        $pad = (8 - ($append.Length % 8)) % 8
        $append.Write((New-Object byte[] $pad), 0, $pad)
        $certOffset = $append.Length
        $blob = New-Object byte[] 1032
        [BitConverter]::GetBytes([uint32]1032).CopyTo($blob, 0); [BitConverter]::GetBytes([uint16]0x0200).CopyTo($blob, 4); [BitConverter]::GetBytes([uint16]0x0002).CopyTo($blob, 6)
        (New-Object Random 7).NextBytes(($rest = New-Object byte[] 1024)); $rest.CopyTo($blob, 8)
        $append.Write($blob, 0, $blob.Length)
    } finally { $append.Dispose() }
    Set-ArkPeField $signedCopy $layout.SecurityDirPos ([uint32]$certOffset); Set-ArkPeField $signedCopy ($layout.SecurityDirPos + 4) ([uint32]1032)
    $found = Find-ArkSetupPayload $signedCopy
    Assert-True ($found -and $found.Signed -and $found.Offset -eq $payload.Offset) 'the footer is found before the certificate table'
    $signedRoot = Join-Path $fixture 'signed copy'
    Assert-True ((Invoke-Setup $signedCopy ('--silent --dir "' + $signedRoot + '"')) -eq 0) 'a setup with a certificate table installs'
    Assert-True ((Get-Hash (Join-Path $signedRoot 'uninstall.exe')) -ceq (Get-ArkSetupStubHash $setup $payload)) 'its uninstall.exe does not point at the setup''s certificate table'
    Assert-True ((Get-ArkPeLayout (Join-Path $signedRoot 'uninstall.exe')).CertSize -eq 0) 'the unsigned stub keeps an empty security directory'
    Uninstall-Root $signedRoot
    $appended = Join-Path $copies "ark-engine-appended-$testId.exe"
    Copy-Item -LiteralPath $signedCopy -Destination $appended
    $append = [IO.File]::Open($appended, 'Append', 'Write'); try { $append.Write((New-Object byte[] 16), 0, 16) } finally { $append.Dispose() }
    Assert-True ((Invoke-Setup $appended ('--silent --dir "' + (Join-Path $fixture 'appended') + '"') 60) -eq 4) 'data after the certificate table is refused'

    if ($Signing) {
        # a real Authenticode signature with a temporary untrusted certificate: verifies hashes and layout, not trust
        $certificate = New-SelfSignedCertificate -Type CodeSigningCert -Subject "CN=ARK installer test $testId (temporary)" -CertStoreLocation 'Cert:\CurrentUser\My' -NotAfter (Get-Date).AddDays(1)
        $signScript = Join-Path $fixture 'sign.ps1'
        [IO.File]::WriteAllText($signScript, "param([string]`$Path)`r`n`$certificate = Get-Item -LiteralPath 'Cert:\CurrentUser\My\$($certificate.Thumbprint)'`r`n`$result = Set-AuthenticodeSignature -LiteralPath `$Path -Certificate `$certificate -HashAlgorithm SHA256`r`nif (-not `$result.SignerCertificate) { throw ('Signing failed: ' + `$result.StatusMessage) }`r`n")
        $signed = Build-Setup $new (Join-Path $fixture 'setup-signed') @("sign=$signScript")
        $signature = Get-AuthenticodeSignature -LiteralPath $signed
        Assert-True ($signature.SignerCertificate.Thumbprint -ceq $certificate.Thumbprint -and [string]$signature.Status -cne 'HashMismatch') "the setup carries the test signature ($($signature.Status))"
        Assert-True ((Find-ArkSetupPayload $signed).Signed) 'the signed setup keeps a discoverable payload'
        $signedInstall = Join-Path $fixture 'signed install'
        Assert-True ((Invoke-Setup $signed ('--silent --dir "' + $signedInstall + '"')) -eq 0) 'the signed setup installs'
        $uninstallSignature = Get-AuthenticodeSignature -LiteralPath (Join-Path $signedInstall 'uninstall.exe')
        Assert-True ($uninstallSignature.SignerCertificate.Thumbprint -ceq $certificate.Thumbprint -and [string]$uninstallSignature.Status -cne 'HashMismatch') "uninstall.exe keeps its own valid signature ($($uninstallSignature.Status))"
        Assert-Installed $signedInstall '0.0.2' $signed
        Uninstall-Root $signedInstall
    }

    # --- native library caches (%TEMP%\.net\<exe name>\<hash>): a run removes what other ARK setup builds left (never its
    #     own, which may still be loading libraries while it exits), and uninstall.exe removes its own after it is gone
    $caches = Join-Path $temp '.net'
    Assert-True ((Invoke-Setup $setup '--uninstall --quiet --dir "C:\ARK engine test not installed"' 60) -eq 5) 'a folder without ARK is not uninstalled'
    foreach ($name in @([IO.Path]::GetFileNameWithoutExtension($older), [IO.Path]::GetFileNameWithoutExtension($public))) {
        $cache = Join-Path $caches $name
        $watch = [Diagnostics.Stopwatch]::StartNew()
        while ((Test-Path -LiteralPath $cache) -and @(Get-ChildItem -LiteralPath $cache -Directory).Count -gt 0 -and $watch.Elapsed.TotalSeconds -lt 30) { Start-Sleep -Milliseconds 250 }
        Assert-True (-not (Test-Path -LiteralPath $cache) -or @(Get-ChildItem -LiteralPath $cache -Directory).Count -eq 0) "the native library cache of the other build $name is removed"
    }
    $watch = [Diagnostics.Stopwatch]::StartNew()
    do {
        $uninstallCaches = @(if (Test-Path -LiteralPath (Join-Path $caches 'uninstall')) { Get-ChildItem -LiteralPath (Join-Path $caches 'uninstall') -Directory | Where-Object { $_.CreationTime -gt $started } })
        if ($uninstallCaches.Count -gt 0) { Start-Sleep -Milliseconds 250 }
    } while ($uninstallCaches.Count -gt 0 -and $watch.Elapsed.TotalSeconds -lt 30)
    Assert-True ($uninstallCaches.Count -eq 0) "uninstall.exe's own native library cache is removed ($($uninstallCaches.Count) left)"
    $passed = $true
} finally {
    if ($certificate) { Remove-Item -LiteralPath ('Cert:\CurrentUser\My\' + $certificate.Thumbprint) -DeleteKey -ErrorAction SilentlyContinue; if (Test-Path -LiteralPath ('Cert:\CurrentUser\My\' + $certificate.Thumbprint)) { Write-Warning 'The temporary test certificate could not be removed.' } }
    foreach ($leftRoot in @(Get-ChildItem -LiteralPath $fixture -Directory -ErrorAction SilentlyContinue | Where-Object { Test-Path -LiteralPath (Join-Path $_.FullName 'uninstall.exe') })) {
        try { Invoke-Setup (Join-Path $leftRoot.FullName 'uninstall.exe') '--uninstall --quiet' | Out-Null } catch { Write-Warning $_.Exception.Message }
    }
    if (Test-Path -LiteralPath $registry) { Remove-Item -LiteralPath $registry -Recurse -Force }
    foreach ($name in @(Get-ChildItem -LiteralPath (Join-Path ([IO.Path]::GetTempPath()) '.net') -Directory -ErrorAction SilentlyContinue | Where-Object { $_.Name -like "*$testId" })) { Remove-Item -LiteralPath $name.FullName -Recurse -Force -ErrorAction SilentlyContinue }
}
foreach ($path in $protected) { if ((Get-FileHash -LiteralPath $path).Hash -cne $before[$path]) { throw "Preservation check failed: $path" } }
foreach ($name in $realRegistrations.Keys) { if ((Get-Registration $name) -cne $realRegistrations[$name]) { throw "The real registration $name changed." } }
if ($passed) { Remove-Item -LiteralPath $fixture -Recurse -Force } # kept for inspection after a failure
Write-Output "Installer engine tests: $checks checks passed with a synthetic payload. Signing exercised: $([bool]$Signing)."
