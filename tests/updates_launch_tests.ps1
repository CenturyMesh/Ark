$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$repository = Split-Path $PSScriptRoot -Parent
Import-Module (Join-Path $repository 'updates/Ark.Update.psm1') -Force
$script:passed = 0
function Assert-LaunchEqual($Actual, $Expected, [string]$Name) {
    if ($Actual -cne $Expected) { throw "$Name`: expected [$Expected], got [$Actual]" }
    $script:passed++
}
function New-LaunchRuntime([string]$Path, [string]$Version, [string]$Executable) {
    [IO.Directory]::CreateDirectory($Path) | Out-Null
    Copy-Item -LiteralPath $Executable -Destination (Join-Path $Path 'ark-studio.exe')
    foreach ($name in @('ark-cli.exe','ark-mcp.exe','ark-occt-worker.exe','TKernel.dll','TKBRep.dll','TKDESTEP.dll','assets/ui/ark-wordmark.png','assets/ui/ark-tool-atlas-v1.png','assets/ui/ark-detail-atlas-v1.png','assets/ui/ark-domain-atlas-v1.png')) {
        $target = Join-Path $Path $name
        [IO.Directory]::CreateDirectory((Split-Path $target -Parent)) | Out-Null
        [IO.File]::WriteAllText($target, "isolated launch fixture $Version $name")
    }
    [IO.Directory]::CreateDirectory((Join-Path $Path 'updates')) | Out-Null
    foreach ($name in @('ark-update.ps1','ark-launch.ps1','Ark.Update.psm1')) {
        Copy-Item -LiteralPath (Join-Path $repository "updates/$name") -Destination (Join-Path $Path "updates/$name")
    }
    Write-ArkAtomicJson (Join-Path $Path 'app-version.json') @{ schema=1; product='ARK'; version=$Version; platform='windows-x64'; entrypoint='ark-studio.exe' }
}
function New-LaunchPayload([string[]]$Arguments, [string]$WorkingDirectory) {
    $value = @{ arguments=@($Arguments); workingDirectory=$WorkingDirectory } | ConvertTo-Json -Depth 4 -Compress
    [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($value))
}
function Wait-LaunchReceipt([string]$Version, [int]$Count) {
    $watch = [Diagnostics.Stopwatch]::StartNew()
    do {
        $files = @(Get-ChildItem -LiteralPath $script:receiptDirectory -File | Where-Object { $_.Name.StartsWith("launch-$Version-", [StringComparison]::Ordinal) })
        if ($files.Count -ge $Count) {
            $latest = $files | Sort-Object LastWriteTimeUtc,Name | Select-Object -Last 1
            return [xml][IO.File]::ReadAllText($latest.FullName)
        }
        Start-Sleep -Milliseconds 50
    } while ($watch.Elapsed.TotalSeconds -lt 5)
    throw "The real child for $Version did not produce receipt $Count."
}
function Get-ReceiptText([xml]$Receipt, [string]$Name) { $Receipt.SelectSingleNode("/launch/$Name").InnerText }
function Assert-LaunchArguments([xml]$Receipt, [string[]]$Expected, [string]$Name) {
    $actual = @($Receipt.SelectNodes('/launch/arguments/argument'))
    Assert-LaunchEqual $actual.Count $Expected.Count "$Name count"
    for ($index=0; $index -lt $Expected.Count; $index++) {
        Assert-LaunchEqual $actual[$index].InnerText $Expected[$index] "$Name argument $index"
    }
}
function Assert-CallerEnvironment {
    Assert-LaunchEqual $env:ARK_UPDATE_ACK_FILE $script:callerAck 'Caller ACK path restored'
    Assert-LaunchEqual $env:ARK_UPDATE_ACK_NONCE 'caller-sentinel-nonce' 'Caller ACK nonce restored'
    Assert-LaunchEqual $env:ARK_UPDATE_LAUNCHED 'caller-sentinel-marker' 'Caller redirect marker restored'
}
$temporary = Assert-ArkChildPath ([IO.Path]::GetTempPath()) (Join-Path ([IO.Path]::GetTempPath()) ('ark-launch-tests-' + [guid]::NewGuid().ToString('N')))
$savedEnvironment = @{}
foreach ($name in @('ARK_LAUNCH_TEST_RECEIPTS','ARK_LAUNCH_TEST_FAIL_VERSION','ARK_LAUNCH_TEST_SLOW_ACK_VERSION','ARK_UPDATE_ACK_FILE','ARK_UPDATE_ACK_NONCE','ARK_UPDATE_LAUNCHED')) {
    $savedEnvironment[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
}
[IO.Directory]::CreateDirectory($temporary) | Out-Null
try {
    # A genuine WinExe with no forms or visible windows. It records the OS-parsed
    # argv/CWD and acknowledges startup only for the non-failing fixture version.
    $source = Join-Path $temporary 'LaunchFixture.cs'
    $executable = Join-Path $temporary 'LaunchFixture.exe'
    [IO.File]::WriteAllText($source, @'
using System;
using System.Diagnostics;
using System.IO;
using System.Reflection;
using System.Text;
using System.Threading;
using System.Xml;
public static class LaunchFixture {
    [STAThread] public static int Main(string[] args) {
        try {
            string version = Path.GetFileName(Path.GetDirectoryName(Assembly.GetExecutingAssembly().Location));
            string nonce = Environment.GetEnvironmentVariable("ARK_UPDATE_ACK_NONCE") ?? "";
            string ack = Environment.GetEnvironmentVariable("ARK_UPDATE_ACK_FILE") ?? "";
            string directory = Environment.GetEnvironmentVariable("ARK_LAUNCH_TEST_RECEIPTS");
            bool failure = version == Environment.GetEnvironmentVariable("ARK_LAUNCH_TEST_FAIL_VERSION");
            bool slowAck = version == Environment.GetEnvironmentVariable("ARK_LAUNCH_TEST_SLOW_ACK_VERSION");
            string destination = Path.Combine(directory, "launch-" + version + "-" + Process.GetCurrentProcess().Id + ".xml");
            using (XmlWriter writer = XmlWriter.Create(destination + ".tmp")) {
                writer.WriteStartElement("launch");
                writer.WriteElementString("version", version);
                writer.WriteElementString("cwd", Environment.CurrentDirectory);
                writer.WriteElementString("ack_file", ack);
                writer.WriteElementString("ack_nonce", nonce);
                writer.WriteElementString("marker", Environment.GetEnvironmentVariable("ARK_UPDATE_LAUNCHED") ?? "");
                writer.WriteElementString("action", failure ? "intentional startup failure" : "initialized");
                writer.WriteElementString("delayed_ack", slowAck ? "true" : "false");
                writer.WriteElementString("pid", Process.GetCurrentProcess().Id.ToString());
                writer.WriteStartElement("arguments");
                foreach (string argument in args) writer.WriteElementString("argument", argument);
                writer.WriteEndElement();
                writer.WriteEndElement();
            }
            File.Move(destination + ".tmp", destination);
            if (failure) return 33;
            if (ack.Length > 0 && nonce.Length == 32 && nonce.Trim("0123456789abcdef".ToCharArray()).Length == 0) {
                if (slowAck) {
                    using (FileStream stream = new FileStream(ack, FileMode.Create, FileAccess.Write, FileShare.None)) {
                        Thread.Sleep(300);
                        byte[] bytes = Encoding.ASCII.GetBytes(nonce);
                        stream.Write(bytes, 0, bytes.Length);
                    }
                } else File.WriteAllText(ack, nonce);
            }
            return 0;
        } catch { return 44; }
    }
}
'@, (New-Object Text.UTF8Encoding($false)))
    $framework = Join-Path ([Environment]::GetFolderPath('Windows')) 'Microsoft.NET/Framework64/v4.0.30319/csc.exe'
    if (-not (Test-Path -LiteralPath $framework)) { $framework = Join-Path ([Environment]::GetFolderPath('Windows')) 'Microsoft.NET/Framework/v4.0.30319/csc.exe' }
    if (-not (Test-Path -LiteralPath $framework)) { throw 'The Windows .NET Framework C# compiler is required for the isolated launch fixture.' }
    & $framework /nologo /target:winexe /optimize+ /reference:System.Xml.dll "/out:$executable" $source
    if ($LASTEXITCODE -ne 0) { throw 'Could not compile the isolated launch fixture.' }
    $script:receiptDirectory = Join-Path $temporary 'receipts'
    $workspace = Join-Path $temporary 'workspace with spaces'
    [IO.Directory]::CreateDirectory($script:receiptDirectory) | Out-Null
    [IO.Directory]::CreateDirectory($workspace) | Out-Null
    $env:ARK_LAUNCH_TEST_RECEIPTS = $script:receiptDirectory
    $env:ARK_LAUNCH_TEST_FAIL_VERSION = ''
    $env:ARK_LAUNCH_TEST_SLOW_ACK_VERSION = ''
    $script:callerAck = Join-Path $temporary 'caller-ack-sentinel.tmp'
    $env:ARK_UPDATE_ACK_FILE = $script:callerAck
    $env:ARK_UPDATE_ACK_NONCE = 'caller-sentinel-nonce'
    $env:ARK_UPDATE_LAUNCHED = 'caller-sentinel-marker'
    $baseline = Join-Path $temporary 'baseline'
    $candidate = Join-Path $temporary 'candidate'
    $failure = Join-Path $temporary 'failing-candidate'
    $slowCandidate = Join-Path $temporary 'slow-ack-candidate'
    $install = Join-Path $temporary 'isolated install'
    New-LaunchRuntime $baseline '0.9.0' $executable
    New-LaunchRuntime $candidate '0.9.1' $executable
    New-LaunchRuntime $failure '0.9.2' $executable
    New-LaunchRuntime $slowCandidate '0.9.3' $executable
    Initialize-ArkInstall $baseline $install | Out-Null
    $launcher = Join-Path $install 'launcher/ark-launch.ps1'
    $unicode = [string][char]0x00e7 + [char]0x00e3 + [char]0x03c0
    $arguments = @('--load', 'relative drawing.ark', "unicode-$unicode", '', 'embedded "quoted" text', 'C:\folder with spaces\', 'slashes\\"quote', '$(Write-Output SHOULD_STAY_LITERAL)', 'last\')
    $payload = New-LaunchPayload $arguments $workspace

    & $launcher -InstallRoot $install -ArgumentsBase64 $payload -NoDialogs
    $first = Wait-LaunchReceipt '0.9.0' 1
    Assert-LaunchArguments $first $arguments 'Baseline actual argv'
    Assert-LaunchEqual (Get-ReceiptText $first 'cwd') $workspace 'Baseline child preserves caller workspace'
    Assert-LaunchEqual (Get-ReceiptText $first 'marker') '1' 'Baseline child bypasses direct-runtime redirect'
    Assert-LaunchEqual (Get-ArkState $install).current '0.9.0' 'Ordinary launch retains selected baseline'
    Assert-CallerEnvironment

    Initialize-ArkInstall $candidate $install | Out-Null
    Assert-LaunchEqual (Get-ArkState $install).pending '0.9.1' 'Candidate prepared before actual launch'
    & $launcher -InstallRoot $install -ArgumentsBase64 $payload -NoDialogs
    $accepted = Wait-LaunchReceipt '0.9.1' 1
    Assert-LaunchArguments $accepted $arguments 'Activated child actual argv'
    Assert-LaunchEqual (Get-ReceiptText $accepted 'cwd') $workspace 'Activated child preserves caller workspace'
    Assert-LaunchEqual (Get-ReceiptText $accepted 'marker') '1' 'Activated child bypasses direct-runtime redirect'
    Assert-LaunchEqual (Get-ReceiptText $accepted 'ack_nonce').Length 32 'Activated child receives actual ACK nonce'
    $ackFile = Assert-ArkChildPath $install (Get-ReceiptText $accepted 'ack_file')
    Assert-LaunchEqual (Test-Path -LiteralPath $ackFile) $false 'Accepted startup ACK is removed'
    $state = Get-ArkState $install
    Assert-LaunchEqual $state.current '0.9.1' 'Real ACK commits selected candidate'
    Assert-LaunchEqual $state.previous '0.9.0' 'Successful launch retains rollback runtime'
    Assert-LaunchEqual $state.activating '' 'Successful launch clears activation marker'
    Assert-LaunchEqual $state.pending '' 'Successful launch consumes pending update'
    Assert-CallerEnvironment

    & $launcher -InstallRoot $install -ArgumentsBase64 (New-LaunchPayload @('') $workspace) -NoDialogs
    $empty = Wait-LaunchReceipt '0.9.1' 2
    Assert-LaunchArguments $empty @('') 'Single empty actual argument'
    Assert-CallerEnvironment

    $env:ARK_LAUNCH_TEST_FAIL_VERSION = '0.9.2'
    Initialize-ArkInstall $failure $install | Out-Null
    & $launcher -InstallRoot $install -ArgumentsBase64 $payload -NoDialogs
    $rejected = Wait-LaunchReceipt '0.9.2' 1
    $fallback = Wait-LaunchReceipt '0.9.1' 3
    Assert-LaunchEqual (Get-ReceiptText $rejected 'action') 'intentional startup failure' 'Actual candidate exits before startup ACK'
    Assert-LaunchArguments $fallback $arguments 'Recovered child actual argv'
    Assert-LaunchEqual (Get-ReceiptText $fallback 'cwd') $workspace 'Fallback preserves caller workspace'
    Assert-LaunchEqual (Get-ReceiptText $fallback 'marker') '1' 'Fallback child bypasses direct-runtime redirect'
    $state = Get-ArkState $install
    Assert-LaunchEqual $state.current '0.9.1' 'Actual failed process activates previous runtime'
    Assert-LaunchEqual $state.previous '' 'Automatic recovery clears failed rollback pointer'
    Assert-LaunchEqual $state.activating '' 'Failed launch clears activation marker'
    Assert-LaunchEqual $state.pending '' 'Failed launch clears pending update'
    Assert-LaunchEqual (@(Get-ChildItem -LiteralPath $install -Filter 'ack-*.tmp' -File)).Count 0 'No activation ACK files remain'
    Assert-CallerEnvironment
    Assert-LaunchEqual (Test-Path -LiteralPath $script:callerAck) $false 'Inherited invalid caller nonce never writes caller ACK path'
    # Acknowledgement creation and close are separate filesystem events. A
    # transient sharing violation must not turn a valid startup into rollback.
    $env:ARK_LAUNCH_TEST_SLOW_ACK_VERSION = '0.9.3'
    Initialize-ArkInstall $slowCandidate $install | Out-Null
    & $launcher -InstallRoot $install -ArgumentsBase64 $payload -NoDialogs
    $slow = Wait-LaunchReceipt '0.9.3' 1
    Assert-LaunchEqual (Get-ReceiptText $slow 'delayed_ack') 'true' 'Actual child holds its ACK file open before completion'
    Assert-LaunchEqual (Get-ReceiptText $slow 'marker') '1' 'Delayed ACK child retains redirect marker'
    $state = Get-ArkState $install
    Assert-LaunchEqual $state.current '0.9.3' 'Transient ACK sharing violation still confirms startup'
    Assert-LaunchEqual $state.previous '0.9.1' 'Delayed ACK retains previous healthy runtime'
    Assert-LaunchEqual $state.activating '' 'Delayed ACK clears activation marker'
    Assert-LaunchEqual (@(Get-ChildItem -LiteralPath $install -Filter 'ack-*.tmp' -File)).Count 0 'Delayed ACK file is cleaned up'
    Assert-CallerEnvironment
    Write-Output "ARK OTA real-launch checks: $script:passed passed. Isolated WinExe fixtures only; no network, visible windows, user drawings or global installation."
} finally {
    foreach ($name in $savedEnvironment.Keys) { [Environment]::SetEnvironmentVariable($name, $savedEnvironment[$name], 'Process') }
    Remove-Item -LiteralPath (Assert-ArkChildPath ([IO.Path]::GetTempPath()) $temporary) -Recurse -Force
}
