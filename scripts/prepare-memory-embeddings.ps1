param([string]$OutputDirectory)
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$repoRoot = Split-Path $PSScriptRoot -Parent
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $repoRoot 'out/deps/memory' }
$assetRoot = [IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Force -Path $assetRoot | Out-Null
$modelPin = 'cad454171d918d9873a2701ba245054b6c1760dd'
$projectionPin = 'bfe45d0732ca50787611c0fe107ba278c7f3f889'
$baseUrl = "https://huggingface.co/Xenova/distiluse-base-multilingual-cased-v2/resolve/$modelPin"
$projectionUrl = "https://huggingface.co/sentence-transformers/distiluse-base-multilingual-cased-v2/resolve/$projectionPin"
function Assert-Digest([string]$Path, [string]$Digest) {
    if ((Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant() -ne $Digest) { throw "SHA-256 mismatch: $Path" }
}
function Fetch-Pinned([string]$Url, [string]$Name, [string]$Digest) {
    $target = Join-Path $assetRoot $Name
    if (-not (Test-Path -LiteralPath $target)) { Invoke-WebRequest -UseBasicParsing $Url -OutFile $target }
    Assert-Digest $target $Digest
}
Fetch-Pinned "$baseUrl/onnx/model_quantized.onnx" 'model_quantized.onnx' '1724c10ba3b33b58afb6ce73bbf3e0528921bd4d4e1bf5a569b6d73f234e72bf'
Fetch-Pinned "$baseUrl/vocab.txt" 'vocab.txt' 'fe0fda7c425b48c516fc8f160d594c8022a0808447475c1a7c6d6479763f310c'
Fetch-Pinned "$baseUrl/README.md" 'MODEL-README.md' 'a0d322377a448a886c99624e43dab4e6d27dce633b6219cfdb101c83758902a1'
Fetch-Pinned "$projectionUrl/2_Dense/model.safetensors" 'dense.safetensors' '0a21b1ce908e772ebf09f93c20ca09524c32706e9918d9c0169a3f0663b191ed'
Fetch-Pinned "$projectionUrl/README.md" 'MODEL-ORIGINAL-README.md' 'e5d0a1b739640c457c5ab9a17d1b97ad13a0b96cff95d2d4e9b40b033b19776f'
$runtimeVersion = '1.23.2'
$cacheRoot = Join-Path (Split-Path $assetRoot -Parent) 'memory-downloads'
New-Item -ItemType Directory -Force -Path $cacheRoot | Out-Null
$archive = Join-Path $cacheRoot "onnxruntime-win-x64-$runtimeVersion.zip"
if (-not (Test-Path -LiteralPath $archive)) { Invoke-WebRequest -UseBasicParsing "https://github.com/microsoft/onnxruntime/releases/download/v$runtimeVersion/onnxruntime-win-x64-$runtimeVersion.zip" -OutFile $archive }
Assert-Digest $archive '0b38df9af21834e41e73d602d90db5cb06dbd1ca618948b8f1d66d607ac9f3cd'
Add-Type -AssemblyName System.IO.Compression.FileSystem
$zip = [IO.Compression.ZipFile]::OpenRead($archive)
try {
    foreach ($mapping in @{
        'lib/onnxruntime.dll'='onnxruntime.dll';
        'lib/onnxruntime_providers_shared.dll'='onnxruntime_providers_shared.dll';
        'LICENSE'='ONNXRUNTIME-LICENSE.txt';
        'ThirdPartyNotices.txt'='ONNXRUNTIME-ThirdPartyNotices.txt'
    }.GetEnumerator()) {
        $entry = $zip.GetEntry("onnxruntime-win-x64-$runtimeVersion/$($mapping.Key)")
        if (-not $entry) { throw "Missing pinned archive entry: $($mapping.Key)" }
        [IO.Compression.ZipFileExtensions]::ExtractToFile($entry, (Join-Path $assetRoot $mapping.Value), $true)
    }
} finally { $zip.Dispose() }
Assert-Digest (Join-Path $assetRoot 'onnxruntime.dll') 'dec964ab1ee36cc9b0ae247d13b376627992fc57dec0454354017ab8fd84f1ea'
Assert-Digest (Join-Path $assetRoot 'onnxruntime_providers_shared.dll') 'a2b3a50956aa75a9879c8472bc7df4f7a8072bcd2db19a1b7d988e7688f293ef'
# The pinned original model card declares Apache-2.0. Preserve both model cards
# alongside the standard Apache-2.0 license text and explicit attribution.
Copy-Item -LiteralPath (Join-Path $repoRoot 'LICENSE') -Destination (Join-Path $assetRoot 'MODEL-LICENSE.txt') -Force
$notice = @'
ARK local semantic memory model
Model: Xenova/distiluse-base-multilingual-cased-v2
Revision: cad454171d918d9873a2701ba245054b6c1760dd
Source: https://huggingface.co/Xenova/distiluse-base-multilingual-cased-v2/tree/cad454171d918d9873a2701ba245054b6c1760dd
Original sentence-transformer and Dense projection: https://huggingface.co/sentence-transformers/distiluse-base-multilingual-cased-v2/tree/bfe45d0732ca50787611c0fe107ba278c7f3f889
Base architecture: multilingual DistilBERT, 768 hidden dimensions, cased WordPiece, complete Dense/Tanh projection to 512 dimensions
License: Apache-2.0 as declared in MODEL-ORIGINAL-README.md
Weights: upstream ONNX int8 quantization and original float32 Dense safetensors, unmodified
ARK preprocessing: original cased/accent-preserving WordPiece, masked mean pooling, original Dense/Tanh projection and L2 normalization
Application window length: 128 tokens including CLS/SEP, matching the original sentence-transformer configuration
Long records: all tokens processed in overlapping windows (16-token overlap), novel-token-weighted average of normalized window vectors, final L2 normalization
These embeddings retrieve untrusted historical context and do not establish current geometry or source authority.

Inference runtime: Microsoft ONNX Runtime 1.23.2, CPU, Windows x64
Source: https://github.com/microsoft/onnxruntime/tree/v1.23.2
License: MIT; bundled third-party notices preserved separately.
Runtime makes no network calls and requires no account, API key or paid model.
'@
[IO.File]::WriteAllText((Join-Path $assetRoot 'MODEL-NOTICE.txt'), $notice.Replace("`r`n","`n")+"`n", [Text.UTF8Encoding]::new($false))
$names = @('model_quantized.onnx','vocab.txt','dense.safetensors','onnxruntime.dll','onnxruntime_providers_shared.dll','MODEL-LICENSE.txt','MODEL-README.md','MODEL-ORIGINAL-README.md','MODEL-NOTICE.txt','ONNXRUNTIME-LICENSE.txt','ONNXRUNTIME-ThirdPartyNotices.txt')
$files = foreach ($name in $names) { $file=Get-Item -LiteralPath (Join-Path $assetRoot $name); [ordered]@{path=$name;bytes=$file.Length;sha256=(Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()} }
$manifest = [ordered]@{schema=1;model='Xenova/distiluse-base-multilingual-cased-v2';revision=$modelPin;projection_revision=$projectionPin;license='Apache-2.0';runtime='onnxruntime-cpu-1.23.2-windows-x64';dimensions=512;window_tokens=128;window_overlap=16;long_text='all tokens; novel-token-weighted window average and L2';files=@($files)}
[IO.File]::WriteAllText((Join-Path $assetRoot 'manifest.json'), ($manifest | ConvertTo-Json -Depth 8)+"`n", [Text.UTF8Encoding]::new($false))
Write-Output "Prepared pinned offline semantic memory assets: $assetRoot"
Write-Output (Get-Content -LiteralPath (Join-Path $assetRoot 'manifest.json') -Raw)
