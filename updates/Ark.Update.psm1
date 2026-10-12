Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$script:ArkReleaseApi = 'https://api.github.com/repos/CenturyMesh/Ark/releases/latest'
$script:ArkMaxPackageBytes = 512MB
$script:ArkMaxExpandedBytes = 2GB
$script:ArkMaxEntries = 4096

function Get-ArkInstallRoot { Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) 'ARK\app' }

function ConvertTo-ArkVersion([string]$Version) {
    if ($Version -cnotmatch '^(0|[1-9][0-9]{0,8})\.(0|[1-9][0-9]{0,8})\.(0|[1-9][0-9]{0,8})$') { throw 'Invalid ARK stable version.' }
    return [version]$Version
}

function Test-ArkNewerVersion([string]$Candidate, [string]$Current) {
    return (ConvertTo-ArkVersion $Candidate) -gt (ConvertTo-ArkVersion $Current)
}

function ConvertFrom-ArkLaunchPayload([string]$Base64) {
    if ($Base64.Length -gt 45000) { throw 'ARK launch arguments exceed the size limit.' }
    $decoded = [Text.Encoding]::UTF8.GetString([Convert]::FromBase64String($Base64)) | ConvertFrom-Json
    if ($decoded.workingDirectory -isnot [string] -or $decoded.arguments -isnot [Array]) { throw 'Invalid ARK launch payload.' }
    foreach ($argument in $decoded.arguments) { if ($argument -isnot [string]) { throw 'Invalid ARK launch argument.' } }
    return $decoded
}

function ConvertTo-ArkWindowsArgument([string]$Argument) {
    $quoted = [regex]::Replace($Argument, '(\\*)"', '$1$1\"')
    $quoted = [regex]::Replace($quoted, '(\\+)$', '$1$1')
    return '"' + $quoted + '"'
}

function Assert-ArkChildPath([string]$Root, [string]$Path) {
    $rootPath = [IO.Path]::GetFullPath($Root).TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
    $fullPath = [IO.Path]::GetFullPath($Path)
    if (-not $fullPath.StartsWith($rootPath, [StringComparison]::OrdinalIgnoreCase)) { throw 'Path escapes the ARK installation.' }
    return $fullPath
}

function Assert-ArkRelativePath([string]$Path) {
    if (-not $Path -or $Path.Length -gt 240 -or $Path -match '[\\:\x00-\x1f]' -or $Path.StartsWith('/') -or $Path.Contains('//')) { throw 'Unsafe package path.' }
    foreach ($segment in $Path.TrimEnd('/').Split('/')) {
        if ($segment -in @('', '.', '..') -or $segment -match '[<>"|?*]' -or $segment.EndsWith('.') -or $segment.EndsWith(' ') -or $segment -match '^(?i:CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\.|$)') { throw 'Unsafe package path.' }
    }
}

function Assert-ArkNoReparsePoint([string]$Root, [string]$Path) {
    $cursor = [IO.Path]::GetFullPath($Path)
    $boundary = [IO.Path]::GetFullPath($Root).TrimEnd('\', '/')
    while ($cursor.Length -ge $boundary.Length) {
        if (Test-Path -LiteralPath $cursor) {
            if ((Get-Item -LiteralPath $cursor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'ARK installation cannot contain links or junctions.' }
        }
        if ($cursor -eq $boundary) { break }
        $cursor = Split-Path $cursor -Parent
        if (-not $cursor) { break }
    }
}

function Read-ArkJson([string]$Path, [long]$MaxBytes = 1MB) {
    if ((Get-Item -LiteralPath $Path).Length -gt $MaxBytes) { throw 'ARK metadata exceeds the size limit.' }
    return (Get-Content -LiteralPath $Path -Raw -Encoding UTF8 | ConvertFrom-Json)
}

function Write-ArkAtomicJson([string]$Path, $Value) {
    $temporary = $Path + '.' + [guid]::NewGuid().ToString('N') + '.tmp'
    try {
        [IO.File]::WriteAllText($temporary, ($Value | ConvertTo-Json -Depth 12), (New-Object Text.UTF8Encoding($false)))
        if (Test-Path -LiteralPath $Path) { [IO.File]::Replace($temporary, $Path, [System.Management.Automation.Language.NullString]::Value) }
        else { [IO.File]::Move($temporary, $Path) }
    } finally { if (Test-Path -LiteralPath $temporary) { Remove-Item -LiteralPath $temporary -Force } }
}

function Enter-ArkUpdateLock([string]$Root) {
    [IO.Directory]::CreateDirectory($Root) | Out-Null
    Assert-ArkNoReparsePoint $Root $Root
    try { return [IO.File]::Open((Join-Path $Root 'update.lock'), 'OpenOrCreate', 'ReadWrite', 'None') }
    catch { throw 'Another ARK update or launch is in progress. Try again after it completes.' }
}

function Assert-ArkAssetUrl([string]$Url, [string]$Version, [string]$Name) {
    $expected = "https://github.com/CenturyMesh/Ark/releases/download/v$Version/$Name"
    if ($Url -cne $expected) { throw 'Release asset is outside the trusted CenturyMesh/Ark channel.' }
}

function Assert-ArkTransferUrl([uri]$Url, [bool]$Api = $false, [bool]$Redirect = $false) {
    if ($Url.Scheme -cne 'https' -or $Url.Port -ne 443 -or $Url.UserInfo -or $Url.Fragment) { throw 'Only trusted HTTPS release downloads are accepted.' }
    if ($Api) {
        if ($Url.AbsoluteUri -cne $script:ArkReleaseApi) { throw 'Untrusted release API.' }
    } elseif ($Redirect) {
        if ($Url.DnsSafeHost -cne 'release-assets.githubusercontent.com' -and $Url.DnsSafeHost -cne 'objects.githubusercontent.com') { throw 'Untrusted release download redirect.' }
    } elseif ($Url.DnsSafeHost -cne 'github.com' -or -not $Url.AbsolutePath.StartsWith('/CenturyMesh/Ark/releases/download/', [StringComparison]::Ordinal)) {
        throw 'Untrusted release download.'
    }
}

function Receive-ArkFile([string]$Url, [string]$Destination, [long]$MaxBytes, [switch]$Api) {
    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
    $uri = [uri]$Url
    $watch = [Diagnostics.Stopwatch]::StartNew()
    for ($redirect = 0; $redirect -le 4; $redirect++) {
        Assert-ArkTransferUrl $uri ([bool]$Api) ($redirect -gt 0)
        $request = [Net.HttpWebRequest]::Create($uri)
        $request.AllowAutoRedirect = $false
        $request.Timeout = 15000
        $request.ReadWriteTimeout = 15000
        $request.UserAgent = 'ARK-Updates/1'
        if ($Api) { $request.Accept = 'application/vnd.github+json'; $request.Headers.Add('X-GitHub-Api-Version', '2026-03-10') }
        $response = $request.GetResponse()
        try {
            $status = [int]$response.StatusCode
            if ($status -in @(301, 302, 303, 307, 308)) {
                if ($Api -or $redirect -eq 4 -or -not $response.Headers['Location']) { throw 'Unexpected release redirect.' }
                $uri = New-Object Uri($uri, $response.Headers['Location'])
                continue
            }
            if ($status -ne 200 -or $response.ContentLength -gt $MaxBytes) { throw 'Release download exceeds the size limit or failed.' }
            $stream = $response.GetResponseStream()
            $output = [IO.File]::Open($Destination, 'CreateNew', 'Write', 'None')
            try {
                $buffer = New-Object byte[] 65536
                [long]$total = 0
                while (($count = $stream.Read($buffer, 0, $buffer.Length)) -gt 0) {
                    $total += $count
                    if ($total -gt $MaxBytes -or $watch.Elapsed.TotalSeconds -gt 240) { throw 'Release download exceeds the size or time limit.' }
                    $output.Write($buffer, 0, $count)
                }
                $output.Flush()
            } finally { $output.Dispose(); $stream.Dispose() }
            return
        } finally { $response.Dispose() }
    }
    throw 'Too many release download redirects.'
}

function Assert-ArkManifest($Manifest, [string]$ReleaseVersion) {
    ConvertTo-ArkVersion $Manifest.version | Out-Null
    if ($Manifest.schema -ne 1 -or $Manifest.product -cne 'ARK' -or $Manifest.platform -cne 'windows-x64' -or $Manifest.version -cne $ReleaseVersion) { throw 'Incompatible ARK update manifest.' }
    $name = "ark-windows-x64-$ReleaseVersion.zip"
    if ($Manifest.package.name -cne $name -or $Manifest.package.sha256 -cnotmatch '^[a-f0-9]{64}$' -or $Manifest.package.size -isnot [ValueType] -or $Manifest.package.size -lt 1 -or $Manifest.package.size -gt $script:ArkMaxPackageBytes -or [long]$Manifest.package.size -ne $Manifest.package.size) { throw 'Invalid ARK package metadata.' }
    Assert-ArkAssetUrl $Manifest.package.url $ReleaseVersion $name
}

function Get-ArkAvailableUpdate([string]$CurrentVersion) {
    ConvertTo-ArkVersion $CurrentVersion | Out-Null
    $temporary = Join-Path ([IO.Path]::GetTempPath()) ('ark-release-' + [guid]::NewGuid().ToString('N'))
    [IO.Directory]::CreateDirectory($temporary) | Out-Null
    try {
        try { Receive-ArkFile $script:ArkReleaseApi (Join-Path $temporary 'release.json') 1MB -Api }
        catch [Net.WebException] {
            if ($_.Exception.Response -and [int]$_.Exception.Response.StatusCode -eq 404) { return [pscustomobject]@{ status='not-published'; message='ARK self-update is prepared. The update channel is not configured or published yet.'; manifest=$null } }
            throw
        }
        $release = Read-ArkJson (Join-Path $temporary 'release.json')
        if ($release.draft -or $release.prerelease -or $release.tag_name -cnotmatch '^v(.+)$') { throw 'Only published stable ARK releases are accepted.' }
        $version = $Matches[1]
        ConvertTo-ArkVersion $version | Out-Null
        if (-not (Test-ArkNewerVersion $version $CurrentVersion)) { return [pscustomobject]@{ status='current'; message="ARK $CurrentVersion is up to date."; manifest=$null } }
        $assets = @($release.assets | Where-Object { $_.name -ceq 'ark-update.json' })
        if ($assets.Count -ne 1) { return [pscustomobject]@{ status='not-ready'; message='The latest ARK release has no compatible Windows update package yet.'; manifest=$null } }
        Assert-ArkAssetUrl $assets[0].browser_download_url $version 'ark-update.json'
        Receive-ArkFile $assets[0].browser_download_url (Join-Path $temporary 'manifest.json') 64KB
        $manifest = Read-ArkJson (Join-Path $temporary 'manifest.json') 64KB
        Assert-ArkManifest $manifest $version
        $packages = @($release.assets | Where-Object { $_.name -ceq $manifest.package.name })
        if ($packages.Count -ne 1 -or $packages[0].size -ne $manifest.package.size -or $packages[0].browser_download_url -cne $manifest.package.url) { throw 'ARK release package does not match its manifest.' }
        return [pscustomobject]@{ status='available'; message="ARK $version is available."; manifest=$manifest }
    } finally { Remove-Item -LiteralPath (Assert-ArkChildPath ([IO.Path]::GetTempPath()) $temporary) -Recurse -Force }
}

function Assert-ArkPackageHash([string]$Path, $Manifest) {
    if ((Get-Item -LiteralPath $Path).Length -ne $Manifest.package.size -or (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant() -cne $Manifest.package.sha256) { throw 'ARK update package failed SHA-256 verification.' }
}

function Compress-ArkPackage([string]$RuntimeDirectory, [string]$Archive) {
    Add-Type -AssemblyName System.IO.Compression
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $files = @(Get-ChildItem -LiteralPath $RuntimeDirectory -Recurse -File -Force | Sort-Object FullName)
    if ($files.Count -gt $script:ArkMaxEntries) { throw 'ARK package has too many entries.' }
    $zip = [IO.Compression.ZipFile]::Open($Archive, 'Create')
    try {
        foreach ($file in $files) {
            Assert-ArkNoReparsePoint $RuntimeDirectory $file.FullName
            $relative = $file.FullName.Substring($RuntimeDirectory.TrimEnd('\', '/').Length + 1).Replace('\', '/')
            Assert-ArkRelativePath $relative
            [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip, $file.FullName, $relative, [IO.Compression.CompressionLevel]::Optimal) | Out-Null
        }
    } finally { $zip.Dispose() }
}

function Expand-ArkPackage([string]$Archive, [string]$Destination) {
    Add-Type -AssemblyName System.IO.Compression
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    if (Test-Path -LiteralPath $Destination) { throw 'Package staging destination must be new.' }
    [IO.Directory]::CreateDirectory($Destination) | Out-Null
    $zip = [IO.Compression.ZipFile]::OpenRead($Archive)
    try {
        if ($zip.Entries.Count -gt $script:ArkMaxEntries) { throw 'ARK package has too many entries.' }
        $seen = New-Object 'Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
        [long]$expanded = 0
        [long]$written = 0
        $watch = [Diagnostics.Stopwatch]::StartNew()
        foreach ($entry in $zip.Entries) {
            Assert-ArkRelativePath $entry.FullName
            if (-not $seen.Add($entry.FullName.TrimEnd('/'))) { throw 'Duplicate or conflicting package path.' }
            # Unix links and Windows reparse attributes are rejected rather than followed.
            $unixType = ($entry.ExternalAttributes -shr 16) -band 0xF000
            if ($unixType -eq 0xA000 -or ($entry.ExternalAttributes -band 0x400)) { throw 'Package links are not accepted.' }
            $expanded += $entry.Length
            if ($expanded -gt $script:ArkMaxExpandedBytes -or $entry.Length -gt $script:ArkMaxPackageBytes) { throw 'ARK package expands beyond the size limit.' }
            $target = Assert-ArkChildPath $Destination (Join-Path $Destination $entry.FullName)
            if ($entry.FullName.EndsWith('/')) { [IO.Directory]::CreateDirectory($target) | Out-Null; continue }
            [IO.Directory]::CreateDirectory((Split-Path $target -Parent)) | Out-Null
            $input = $entry.Open()
            $output = [IO.File]::Open($target, 'CreateNew', 'Write', 'None')
            try {
                $buffer = New-Object byte[] 65536
                [long]$entryWritten = 0
                while (($count = $input.Read($buffer, 0, $buffer.Length)) -gt 0) {
                    $entryWritten += $count; $written += $count
                    if ($entryWritten -gt $entry.Length -or $entryWritten -gt $script:ArkMaxPackageBytes -or $written -gt $script:ArkMaxExpandedBytes -or $watch.Elapsed.TotalSeconds -gt 240) { throw 'ARK package expands beyond its declared size or extraction limit.' }
                    $output.Write($buffer, 0, $count)
                }
                if ($entryWritten -ne $entry.Length) { throw 'ARK package entry size does not match its metadata.' }
            } finally { $output.Dispose(); $input.Dispose() }
        }
    } finally { $zip.Dispose() }
}

function Get-ArkRuntimeVersion([string]$RuntimeDirectory) {
    $metadata = Read-ArkJson (Join-Path $RuntimeDirectory 'app-version.json') 4KB
    ConvertTo-ArkVersion $metadata.version | Out-Null
    if ($metadata.schema -ne 1 -or $metadata.product -cne 'ARK' -or $metadata.platform -cne 'windows-x64' -or $metadata.entrypoint -cne 'ark-studio.exe') { throw 'Invalid ARK runtime metadata.' }
    foreach ($name in @('ark-studio.exe', 'ark-cli.exe', 'ark-mcp.exe', 'ark-occt-worker.exe', 'TKernel.dll', 'TKBRep.dll', 'TKDESTEP.dll', 'assets/ui/ark-wordmark.png', 'assets/ui/ark-tool-atlas-v1.png', 'assets/ui/ark-detail-atlas-v1.png', 'assets/ui/ark-domain-atlas-v1.png', 'updates/ark-update.ps1', 'updates/ark-launch.ps1', 'updates/Ark.Update.psm1')) {
        $file = Join-Path $RuntimeDirectory $name
        Assert-ArkNoReparsePoint $RuntimeDirectory $file
        if (-not (Test-Path -LiteralPath $file -PathType Leaf)) { throw "ARK runtime is missing $name." }
    }
    return $metadata.version
}

function Write-ArkRuntimeReceipt([string]$RuntimeDirectory, [string]$Version) {
    $files = @(Get-ChildItem -LiteralPath $RuntimeDirectory -Recurse -File -Force | Where-Object { $_.Name -cne '.ark-install.json' } | ForEach-Object {
        Assert-ArkNoReparsePoint $RuntimeDirectory $_.FullName
        $relative = $_.FullName.Substring($RuntimeDirectory.TrimEnd('\', '/').Length + 1).Replace('\', '/')
        Assert-ArkRelativePath $relative
        [pscustomobject]@{ path=$relative; size=$_.Length; sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant() }
    })
    if ($files.Count -gt $script:ArkMaxEntries) { throw 'Runtime has too many files.' }
    Write-ArkAtomicJson (Join-Path $RuntimeDirectory '.ark-install.json') @{ schema=1; version=$Version; files=$files }
}

function Assert-ArkInstalledRuntime([string]$Root, [string]$Version) {
    ConvertTo-ArkVersion $Version | Out-Null
    $runtime = Assert-ArkChildPath $Root (Join-Path $Root "versions\$Version")
    Assert-ArkNoReparsePoint $Root $runtime
    if ((Get-ArkRuntimeVersion $runtime) -cne $Version) { throw 'Installed ARK version does not match the launcher state.' }
    $receipt = Read-ArkJson (Join-Path $runtime '.ark-install.json')
    if ($receipt.schema -ne 1 -or $receipt.version -cne $Version -or @($receipt.files).Count -gt $script:ArkMaxEntries -or @($receipt.files).Count -lt 1) { throw 'Invalid installed ARK receipt.' }
    $actual = @(Get-ChildItem -LiteralPath $runtime -Recurse -File -Force | Where-Object { $_.Name -cne '.ark-install.json' })
    if ($actual.Count -ne @($receipt.files).Count) { throw 'Installed ARK runtime contains unexpected files.' }
    $seen = New-Object 'Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
    foreach ($file in $receipt.files) {
        Assert-ArkRelativePath $file.path
        if (-not $seen.Add($file.path)) { throw 'Duplicate runtime receipt path.' }
        $path = Assert-ArkChildPath $runtime (Join-Path $runtime $file.path)
        Assert-ArkNoReparsePoint $Root $path
        if ((Get-Item -LiteralPath $path).Length -ne $file.size -or (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() -cne $file.sha256) { throw 'Installed ARK runtime failed integrity verification.' }
    }
    return $runtime
}

function Get-ArkState([string]$Root) {
    $state = Read-ArkJson (Join-Path $Root 'current.json') 4KB
    if ($state.schema -ne 1) { throw 'Unsupported ARK launcher state.' }
    ConvertTo-ArkVersion $state.current | Out-Null
    foreach ($key in @('previous','pending','activating')) { if ($state.$key) { ConvertTo-ArkVersion $state.$key | Out-Null } }
    return $state
}

function Copy-ArkLocalRuntime([string]$RuntimeDirectory, [string]$Root) {
    $runtime = [IO.Path]::GetFullPath($RuntimeDirectory).TrimEnd('\', '/')
    $version = Get-ArkRuntimeVersion $runtime
        $target = Assert-ArkChildPath $Root (Join-Path $Root "versions\$version")
        Assert-ArkNoReparsePoint $Root $target
        if (Test-Path -LiteralPath $target) {
            # Recover a completed baseline copy if the pointer write was interrupted.
            Assert-ArkInstalledRuntime $Root $version | Out-Null
        } else {
        $baseline = Assert-ArkChildPath $Root (Join-Path $Root ('baseline-' + [guid]::NewGuid().ToString('N')))
        [IO.Directory]::CreateDirectory($baseline) | Out-Null
        try {
        # Copy only distributable runtime files. User documents and repository files are excluded.
        foreach ($name in @('ark-studio.exe', 'ark-cli.exe', 'ark-mcp.exe', 'ark-occt-worker.exe', 'app-version.json')) {
            if (Test-Path -LiteralPath (Join-Path $runtime $name)) { Copy-Item -LiteralPath (Join-Path $runtime $name) -Destination $baseline }
        }
        Get-ChildItem -LiteralPath $runtime -Filter '*.dll' -File | ForEach-Object { Assert-ArkNoReparsePoint $runtime $_.FullName; Copy-Item -LiteralPath $_.FullName -Destination $baseline }
        foreach ($directory in @('assets', 'updates', 'licenses')) {
            $source = Join-Path $runtime $directory
            if (Test-Path -LiteralPath $source) {
                Get-ChildItem -LiteralPath $source -Recurse -Force | ForEach-Object { Assert-ArkNoReparsePoint $runtime $_.FullName }
                Copy-Item -LiteralPath $source -Destination $baseline -Recurse
            }
        }
        Write-ArkRuntimeReceipt $baseline $version
        [IO.Directory]::CreateDirectory((Split-Path $target -Parent)) | Out-Null
        [IO.Directory]::Move($baseline, $target)
        Assert-ArkInstalledRuntime $Root $version | Out-Null
        } finally { if (Test-Path -LiteralPath $baseline) { Remove-Item -LiteralPath (Assert-ArkChildPath $Root $baseline) -Recurse -Force } }
        }
    return $version
}

function Initialize-ArkInstall([string]$RuntimeDirectory, [string]$Root = (Get-ArkInstallRoot)) {
    $runtime = [IO.Path]::GetFullPath($RuntimeDirectory).TrimEnd('\', '/')
    $version = Get-ArkRuntimeVersion $runtime
    [IO.Directory]::CreateDirectory($Root) | Out-Null
    Assert-ArkNoReparsePoint $Root $Root
    if (-not (Test-Path -LiteralPath (Join-Path $Root 'current.json'))) {
        Copy-ArkLocalRuntime $runtime $Root | Out-Null
        Write-ArkAtomicJson (Join-Path $Root 'current.json') @{ schema=1; current=$version; previous=''; pending=''; activating='' }
    } else {
        $state = Get-ArkState $Root
        if ((Test-ArkNewerVersion $version $state.current) -and -not $state.activating -and (-not $state.pending -or (Test-ArkNewerVersion $version $state.pending))) {
            # An explicit launch from a newly extracted local package may prepare that package.
            Copy-ArkLocalRuntime $runtime $Root | Out-Null
            $state.pending = $version
            Write-ArkAtomicJson (Join-Path $Root 'current.json') $state
        }
    }
    # The stable launcher is trusted local code, never taken from a manifest field.
    $launcher = Join-Path $Root 'launcher'
    [IO.Directory]::CreateDirectory($launcher) | Out-Null
    Assert-ArkNoReparsePoint $Root $launcher
    foreach ($name in @('Ark.Update.psm1', 'ark-launch.ps1')) {
        $destination = Join-Path $launcher $name
        Assert-ArkNoReparsePoint $Root $destination
        if (-not (Test-Path -LiteralPath $destination)) {
            $temporary = Join-Path $launcher ([guid]::NewGuid().ToString('N') + '.tmp')
            try { Copy-Item -LiteralPath (Join-Path $runtime "updates\$name") -Destination $temporary; [IO.File]::Move($temporary, $destination) }
            finally { if (Test-Path -LiteralPath $temporary) { Remove-Item -LiteralPath $temporary -Force } }
        }
    }
    $cmd = '@echo off' + "`r`n" + '"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File "%~dp0launcher\ark-launch.ps1" -InstallRoot "%~dp0." %*' + "`r`n"
    [IO.File]::WriteAllText((Join-Path $Root 'ARK.cmd'), $cmd, (New-Object Text.UTF8Encoding($false)))
    return $Root
}

function Install-ArkStagedPackage([string]$Archive, $Manifest, [string]$Root) {
    Assert-ArkManifest $Manifest $Manifest.version
    Assert-ArkPackageHash $Archive $Manifest
    $state = Get-ArkState $Root
    if (-not (Test-ArkNewerVersion $Manifest.version $state.current)) { throw 'ARK does not stage an older or equal version.' }
    if ($state.activating) { throw 'Another ARK version is still being activated.' }
    $temporary = Assert-ArkChildPath $Root (Join-Path $Root ('stage-' + [guid]::NewGuid().ToString('N')))
    $target = Assert-ArkChildPath $Root (Join-Path $Root "versions\$($Manifest.version)")
    Assert-ArkNoReparsePoint $Root $target
    try {
        Expand-ArkPackage $Archive $temporary
        if ((Get-ArkRuntimeVersion $temporary) -cne $Manifest.version) { throw 'Package runtime version differs from the release manifest.' }
        Write-ArkRuntimeReceipt $temporary $Manifest.version
        if (Test-Path -LiteralPath $target) {
            Assert-ArkInstalledRuntime $Root $Manifest.version | Out-Null
            $installed = (Read-ArkJson (Join-Path $target '.ark-install.json')).files | Sort-Object path | Select-Object path,size,sha256 | ConvertTo-Json -Depth 4 -Compress
            $candidate = (Read-ArkJson (Join-Path $temporary '.ark-install.json')).files | Sort-Object path | Select-Object path,size,sha256 | ConvertTo-Json -Depth 4 -Compress
            if ($installed -cne $candidate) { throw 'A different package already owns this version. Publish a new version number.' }
        } else {
            [IO.Directory]::CreateDirectory((Split-Path $target -Parent)) | Out-Null
            [IO.Directory]::Move($temporary, $target)
        }
        $state.pending = $Manifest.version
        Write-ArkAtomicJson (Join-Path $Root 'current.json') $state
        return $Manifest.version
    } finally { if (Test-Path -LiteralPath $temporary) { Remove-Item -LiteralPath (Assert-ArkChildPath $Root $temporary) -Recurse -Force } }
}

function Save-ArkUpdate($Manifest, [string]$RuntimeDirectory, [string]$Root = (Get-ArkInstallRoot)) {
    $lock = Enter-ArkUpdateLock $Root
    $archive = Assert-ArkChildPath $Root (Join-Path $Root ('download-' + [guid]::NewGuid().ToString('N') + '.zip'))
    try {
        Initialize-ArkInstall $RuntimeDirectory $Root | Out-Null
        Assert-ArkManifest $Manifest $Manifest.version
        Receive-ArkFile $Manifest.package.url $archive $Manifest.package.size
        return Install-ArkStagedPackage $archive $Manifest $Root
    } finally { if (Test-Path -LiteralPath $archive) { Remove-Item -LiteralPath $archive -Force }; $lock.Dispose() }
}

function Start-ArkActivation([string]$Root) {
    $state = Get-ArkState $Root
    # A launcher interrupted during startup never makes an unacknowledged version permanent.
    if ($state.activating) {
        if (-not $state.previous) { throw 'Interrupted ARK activation has no previous version.' }
        Assert-ArkInstalledRuntime $Root $state.previous | Out-Null
        $state.current = $state.previous; $state.previous = ''; $state.pending = ''; $state.activating = ''
        Write-ArkAtomicJson (Join-Path $Root 'current.json') $state
    }
    if ($state.pending) {
        try { Assert-ArkInstalledRuntime $Root $state.pending | Out-Null }
        catch {
            $state.pending = ''
            Write-ArkAtomicJson (Join-Path $Root 'current.json') $state
            Write-Warning ('The prepared ARK update failed integrity verification; keeping the current version. ' + $_.Exception.Message)
            return $state
        }
        $state.previous = $state.current
        $state.current = $state.pending
        $state.activating = $state.pending
        $state.pending = ''
        Write-ArkAtomicJson (Join-Path $Root 'current.json') $state
    }
    return Get-ArkState $Root
}

function Complete-ArkActivation([string]$Root, [bool]$Healthy) {
    $state = Get-ArkState $Root
    if (-not $state.activating) { return $state }
    if (-not $Healthy) {
        Assert-ArkInstalledRuntime $Root $state.previous | Out-Null
        $state.current = $state.previous
        $state.previous = ''
    }
    $state.activating = ''; $state.pending = ''
    Write-ArkAtomicJson (Join-Path $Root 'current.json') $state
    return $state
}

function Undo-ArkUpdate([string]$Root) {
    $lock = Enter-ArkUpdateLock $Root
    try {
        $state = Get-ArkState $Root
        if (-not $state.previous -or $state.activating) { throw 'No previous ARK version is available for rollback.' }
        Assert-ArkInstalledRuntime $Root $state.previous | Out-Null
        $old = $state.current; $state.current = $state.previous; $state.previous = $old; $state.pending = ''; $state.activating = ''
        Write-ArkAtomicJson (Join-Path $Root 'current.json') $state
        return $state.current
    } finally { $lock.Dispose() }
}

Export-ModuleMember -Function *-Ark*
