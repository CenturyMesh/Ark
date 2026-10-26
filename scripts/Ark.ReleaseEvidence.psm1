Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Write-ArkEvidenceJson([string]$Path, $Value) {
    $text = $Value | ConvertTo-Json -Depth 40
    [IO.File]::WriteAllText($Path, $text + "`n", (New-Object Text.UTF8Encoding($false)))
}

function Get-ArkEvidenceHash([string]$Path) {
    (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Get-ArkSourceInventory([string]$Repository) {
    $commit = & git -C $Repository rev-parse HEAD
    if ($LASTEXITCODE -ne 0) { throw 'Cannot identify source commit.' }
    $tracked = @(& git -C $Repository -c core.quotepath=false ls-files --cached --others --exclude-standard)
    if ($LASTEXITCODE -ne 0) { throw 'Cannot inventory tracked source.' }
    $files = @(foreach ($relative in $tracked | Sort-Object -CaseSensitive) {
        $path = Join-Path $Repository $relative
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Source inventory file is missing: $relative" }
        if ((Get-Item -LiteralPath $path -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Source link requires explicit review: $relative" }
        if ($relative -match '(^|/)(\.env(?:\..*)?|auth\.json|studio\.json)$|\.(dpapi|pem|pfx|key)$') { throw "Sensitive source path: $relative" }
        @{ path=$relative; sha256=(Get-ArkEvidenceHash $path) }
    })
    $digestText = ($files | ForEach-Object { $_.path + ':' + $_.sha256 }) -join "`n"
    $sha = [Security.Cryptography.SHA256]::Create()
    try { $digest = -join ($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($digestText)) | ForEach-Object { $_.ToString('x2') }) } finally { $sha.Dispose() }
    $dirty = @(& git -C $Repository status --porcelain --untracked-files=normal).Count -gt 0
    @{ schema='ark.source-inventory.v1'; commit=$commit.Trim(); dirty=$dirty; source_tree_sha256=$digest; files=$files; scope='Tracked plus nonignored untracked working-tree bytes. Ignored user drawings, profiles and build outputs are excluded; dirty is explicit. The tree digest does not prove which bytes the compiler consumed.' }
}

function Get-ArkPackageInventory([string]$PackageDirectory) {
    $root = [IO.Path]::GetFullPath($PackageDirectory).TrimEnd([IO.Path]::DirectorySeparatorChar,[IO.Path]::AltDirectorySeparatorChar)
    if (-not (Test-Path -LiteralPath $root -PathType Container)) { throw 'Package directory is missing.' }
    if ((Get-Item -LiteralPath $root -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Package root cannot be a link.' }
    $records = @(foreach ($item in Get-ChildItem -LiteralPath $root -Recurse -Force) {
        $relative = $item.FullName.Substring($root.Length+1).Replace('\','/')
        if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) {
            # Linux OCCT shared-library SONAME links may only point inside lib/.
            if ($relative -notmatch '^lib/[^/]+\.so(?:\.[0-9]+)*$') { throw "Unapproved package link: $relative" }
            $target = [string]$item.Target
            if ([IO.Path]::IsPathRooted($target) -or $target -match '[/\\]' -or $target -notmatch '^[^/]+\.so(?:\.[0-9]+)*$' -or -not (Test-Path -LiteralPath (Join-Path $item.DirectoryName $target) -PathType Leaf)) { throw "Escaping/broken package link: $relative" }
            @{ path=$relative; link=$target }; continue
        }
        if ($item.PSIsContainer) { continue }
        $approved = $relative -match '^(ark-(studio|cli|mcp|occt-worker|benchmark)\.exe|[^/]+\.dll|app-version\.json|ARK\.cmd|Install-ARK\.(ps1|cmd)|GETTING_STARTED\.txt|README\.txt|LICENSE|NOTICE|UPDATES\.md|AGENT_BENCHMARK\.md)$' -or
            $relative -match '^bin/ark-(cli|mcp|occt-worker)$' -or $relative -match '^lib/[^/]+\.so(?:\.[0-9]+)*$' -or
            $relative -match '^licenses/(LICENSE|[^/]+\.(txt|md|h))$' -or
            $relative -match '^updates/(ARK\.cmd|ark-launch\.ps1|ark-update\.ps1|Ark\.Update\.psm1)$' -or
            $relative -match '^assets/ark-ui\.arklib$' -or
            $relative -match '^assets/memory/(model_quantized\.onnx|dense\.safetensors|vocab\.txt|onnxruntime(?:_providers_shared)?\.dll|MODEL-(LICENSE\.txt|README\.md|ORIGINAL-README\.md|NOTICE\.txt)|ONNXRUNTIME-(LICENSE|ThirdPartyNotices)\.txt|manifest\.json)$' -or
            $relative -match '^assets/ui/(ark-(app-mark|authoring-atlas-v1|detail-atlas-v1|domain-atlas-v1|tool-atlas-v1|wordmark)\.png|ark-app\.ico|README\.md)$' -or
            $relative -match '^assets/contracts/(BOM\.md|AGENT_ACTIVITY\.md|MEASUREMENT\.md|ark-bom-v1\.schema\.json)$' -or
            $relative -match '^benchmarks/(agent_briefs_v1|offline|ollama-qwen-q3|ollama-qwen-27b)\.json$' -or
            $relative -match '^examples/(pneumatic-cylinder|fourbar-suspension)\.arkops$' -or
            $relative -match '^release-evidence/(sbom\.cdx\.json|source-inventory\.json|package-inventory\.json)$'
        if (-not $approved) { throw "Unapproved package content: $relative" }
        # Evidence files are excluded from their own recursive hash census.
        if ($relative.StartsWith('release-evidence/')) { continue }
        @{ path=$relative; size=$item.Length; sha256=(Get-ArkEvidenceHash $item.FullName) }
    })
    @{ schema='ark.package-inventory.v1'; files=@($records | Sort-Object { $_.path }); scope='All payload files and approved internal SONAME links; generated evidence excluded from its own census.' }
}

function New-ArkReleaseEvidence([string]$Repository,[string]$PackageDirectory,[string]$OutputDirectory,[switch]$UseRuntimeIdentity) {
    $release = Get-Content -LiteralPath (Join-Path $Repository 'release.json') -Raw | ConvertFrom-Json
    $metadata = Get-Content -LiteralPath (Join-Path $PackageDirectory 'app-version.json') -Raw | ConvertFrom-Json
    if ($metadata.product -cne 'ARK' -or $metadata.version -cnotmatch '^[0-9]+\.[0-9]+\.[0-9]+$' -or $metadata.iteration_code -cnotmatch '^[0-9]{3}[A-G]$' -or $metadata.phase -cnotmatch '^[A-G]$' -or [string]$metadata.sequence -cnotmatch '^[1-9][0-9]{0,2}$' -or $metadata.iteration_code -cne ('{0:D3}{1}' -f [int]$metadata.sequence,$metadata.phase)) { throw 'Invalid package release identity.' }
    $sourceRelease = $release
    if ($UseRuntimeIdentity) { $release = @{version=$metadata.version;code=$metadata.iteration_code;sequence=$metadata.sequence;phase=$metadata.phase} }
    elseif ($metadata.version -ne $release.version -or $metadata.iteration_code -ne $release.code -or $metadata.sequence -ne $release.sequence -or $metadata.phase -ne $release.phase) { throw 'Package identity differs from release.json.' }
    foreach ($notice in @('LICENSE','NOTICE','licenses/mapbox-earcut-ISC.txt','licenses/stb-image-LICENSE.txt','licenses/sqlite-provenance.txt','licenses/zstd-LICENSE.txt','licenses/LICENSE_LGPL_21.txt','licenses/OCCT_LGPL_EXCEPTION.txt')) {
        if (-not (Test-Path -LiteralPath (Join-Path $PackageDirectory $notice) -PathType Leaf)) { throw "Required package notice missing: $notice" }
    }
    $inventory = Get-ArkPackageInventory $PackageDirectory
    $source = Get-ArkSourceInventory $Repository
    $source.repository_release_identity = @{code=$sourceRelease.code;version=$sourceRelease.version}
    $source.packaged_runtime_identity = @{code=$release.code;version=$release.version}
    $source.runtime_identity_mode = if ($UseRuntimeIdentity) { 'Retained compiled runtime; current packaging-source inventory does not attest its compiled source.' } else { 'Release identity agrees; source inventory does not attest compiler inputs.' }
    $definitions = @(
        @('mapbox-earcut','2.2.4','ISC','third_party/mapbox/earcut.hpp','https://github.com/mapbox/earcut.hpp/tree/v2.2.4'),
        @('stb_image','2.30','MIT','third_party/stb/stb_image.h','https://github.com/nothings/stb/tree/013ac3beddff3dbffafd5177e7972067cd2b5083'),
        @('SQLite','3.54.0','LicenseRef-SQLite-Public-Domain','third_party/sqlite/sqlite3.c','https://www.sqlite.org/2026/sqlite-amalgamation-3540000.zip'),
        @('zstd','1.5.7','BSD-3-Clause','third_party/zstd/lib/zstd.h','https://github.com/facebook/zstd/tree/v1.5.7'),
        @('Open-CASCADE-Technology','8.0.1','LGPL-2.1-only WITH OCCT-exception-1.0','','https://github.com/Open-Cascade-SAS/OCCT/tree/V8_0_1')
    )
    $components = @(foreach ($definition in $definitions) {
        $license = if ($definition[2].StartsWith('LicenseRef-')) { @{name='SQLite public domain dedication'} } elseif ($definition[0] -eq 'Open-CASCADE-Technology') { @{name=$definition[2]} } else { @{id=$definition[2]} }
        $component = @{type='library'; 'bom-ref'=$definition[0]; name=$definition[0]; version=$definition[1]; licenses=@(@{license=$license}); externalReferences=@(@{type='vcs';url=$definition[4]})}
        if ($definition[3]) { $component.hashes=@(@{alg='SHA-256';content=(Get-ArkEvidenceHash (Join-Path $Repository $definition[3]))}) }
        else { $component.hashes=@(@{alg='SHA-256';content='7c033d917ee8f040c0512d289dcc5f02c148889d5bac17c3e25639accb44f0da'}); $component.properties=@(@{name='ark:hash-scope';value='Pinned OCCT source archive; individual runtime binary hashes are in package-inventory.json.'}) }
        if($definition[0] -eq 'Open-CASCADE-Technology'){
            $patchNotice=Join-Path $PackageDirectory 'licenses/OCCT-ARK-patches.txt'
            if(Test-Path -LiteralPath $patchNotice -PathType Leaf){$component.properties+=@(@{name='ark:modified-source-notice';value='licenses/OCCT-ARK-patches.txt'},@{name='ark:modified-source-notice-sha256';value=(Get-ArkEvidenceHash $patchNotice)},@{name='ark:modified-source-scope';value='Bundled exact patch/source and SDK build declarations; not independent binary-source attestation or geometric qualification.'})}
        }
        $component.properties = @($component['properties'] | Where-Object { $null -ne $_ }) + @{name='ark:source-association';value=$source.runtime_identity_mode}
        $component
    })
    $memoryManifestPath = Join-Path $PackageDirectory 'assets/memory/manifest.json'
    if (Test-Path -LiteralPath $memoryManifestPath) {
        $memory = Get-Content -LiteralPath $memoryManifestPath -Raw | ConvertFrom-Json
        $components += @{type='library';'bom-ref'='ark-memory-model';name=$memory.model;version=$memory.revision;licenses=@(@{license=@{id='Apache-2.0'}});properties=@(@{name='ark:component-kind';value='Local semantic model, original quantized ONNX weights plus pinned sentence projection'},@{name='ark:projection-revision';value=$memory.projection_revision},@{name='ark:asset-manifest-sha256';value=(Get-ArkEvidenceHash $memoryManifestPath)},@{name='ark:license-evidence';value='licenses/memory-MODEL-LICENSE.txt; licenses/memory-MODEL-NOTICE.txt'})}
        $components += @{type='library';'bom-ref'='onnxruntime-cpu';name='ONNX Runtime CPU';version='1.23.2';licenses=@(@{license=@{id='MIT'}});externalReferences=@(@{type='vcs';url='https://github.com/microsoft/onnxruntime/tree/v1.23.2'});properties=@(@{name='ark:license-evidence';value='licenses/memory-ONNXRUNTIME-LICENSE.txt; licenses/memory-ONNXRUNTIME-ThirdPartyNotices.txt'})}
    }
    # Runtime binaries are components too. Classify every bundled DLL/shared
    # object without inventing a source version from its filename or tool path.
    $runtimeCount = 0
    foreach ($file in @($inventory.files | Where-Object { $_.path -match '\.dll$|\.so(?:\.[0-9]+)*$' -and $_.ContainsKey('sha256') })) {
        $runtimeCount++
        $leaf = [IO.Path]::GetFileName($file.path)
        $family = 'Unclassified bundled runtime'
        $dependency = $null
        if ($leaf -match '^(lib)?TK[^/]+\.(dll|so)(\.[0-9]+)*$') { $family='Open CASCADE runtime'; $dependency='Open-CASCADE-Technology' }
        elseif ($leaf -match '^onnxruntime(?:_providers_shared)?[.]dll$') { $family='ONNX Runtime CPU' }
        elseif ($leaf -eq 'libc++.dll') { $family='LLVM libc++' }
        elseif ($leaf -eq 'libunwind.dll') { $family='LLVM libunwind' }
        elseif ($leaf -eq 'libwinpthread-1.dll') { $family='MinGW-w64 winpthreads' }
        elseif ($leaf -match '^(vcruntime|msvcp|concrt|vccorlib)[0-9_]+.*\.dll$|^api-ms-win-crt-.*\.dll$') { $family='Microsoft Visual C++ / UCRT runtime' }
        $binary = @{type='library';'bom-ref'=('runtime:'+$file.path);name=$leaf;hashes=@(@{alg='SHA-256';content=$file.sha256});properties=@(@{name='ark:payload-path';value=$file.path},@{name='ark:runtime-family';value=$family},@{name='ark:license-evidence';value='Original package licenses/ notices; no license inferred from binary name.'})}
        if ($dependency) { $binary.properties += @(@{name='ark:declared-source-component';value=$dependency},@{name='ark:declared-source-version';value='8.0.1'},@{name='ark:declared-source-scope';value='Current packaging-source declaration; not attested binary revision.'}) }
        $binary.properties += @{name='ark:classification-basis';value='Payload filename family and declared build source association, not binary source attestation.'}
        $observedVersion = [Diagnostics.FileVersionInfo]::GetVersionInfo((Join-Path $PackageDirectory $file.path)).FileVersion
        if ($observedVersion) { $binary.version=$observedVersion; $binary.properties += @{name='ark:version-scope';value='Observed binary FileVersion resource, not source revision.'} }
        else { $binary.properties += @{name='ark:version-scope';value='Not determined from binary; source revision not claimed.'} }
        $components += $binary
    }
    $sbom = @{bomFormat='CycloneDX';specVersion='1.5';version=1;metadata=@{component=@{type='application';'bom-ref'='ARK';name='ARK';version=$release.version;licenses=@(@{license=@{id='Apache-2.0'}})};properties=@(@{name='ark:iteration';value=$release.code},@{name='ark:source-commit';value=$source.commit},@{name='ark:source-tree-sha256';value=$source.source_tree_sha256},@{name='ark:scope';value='Direct source dependencies plus every bundled DLL/shared object by hash and observed family/version. Transitive source dependencies, OS and GPU drivers excluded; unknown versions remain explicit. No vulnerability-free or signing claim.'})};components=$components;dependencies=@(@{ref='ARK';dependsOn=@($components | ForEach-Object { $_.'bom-ref' })})}
    [IO.Directory]::CreateDirectory($OutputDirectory) | Out-Null
    Write-ArkEvidenceJson (Join-Path $OutputDirectory 'source-inventory.json') $source
    Write-ArkEvidenceJson (Join-Path $OutputDirectory 'package-inventory.json') $inventory
    Write-ArkEvidenceJson (Join-Path $OutputDirectory 'sbom.cdx.json') $sbom
    return @{iteration_code=$release.code; version=$release.version; source_commit=$source.commit; source_dirty=$source.dirty; source_tree_sha256=$source.source_tree_sha256; payload_files=$inventory.files.Count; runtime_components=$runtimeCount}
}

function New-ArkArtifactReceipt([string]$Archive,[string]$EvidenceDirectory,[string]$OutputPath) {
    $file=Get-Item -LiteralPath $Archive
    $receipt=@{schema='ark.artifact-receipt.v1';artifact=@{name=$file.Name;size=$file.Length;sha256=(Get-ArkEvidenceHash $Archive)};evidence=@(foreach($name in @('sbom.cdx.json','source-inventory.json','package-inventory.json')){@{name=$name;sha256=(Get-ArkEvidenceHash (Join-Path $EvidenceDirectory $name))}});publisher_signature='absent';scope='Local integrity/provenance receipt; not independent attestation, publisher signing or hosted execution evidence.'}
    Write-ArkEvidenceJson $OutputPath $receipt
}
Export-ModuleMember -Function Get-ArkPackageInventory,Get-ArkSourceInventory,New-ArkReleaseEvidence,New-ArkArtifactReceipt
