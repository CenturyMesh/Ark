using System.Diagnostics;
using System.Globalization;
using System.IO.Compression;
using System.Security.Cryptography;
using System.Text;
using ArkSetup.Core;

if (args.Contains("--owned-fixture"))
{
    Console.WriteLine("ready");
    while (Console.ReadLine() != null) { }
    return 0;
}

var fixture = Path.Combine(Path.GetTempPath(), "ark-installer-process-070a-" + Guid.NewGuid().ToString("N"));
var install = Path.Combine(fixture, "owned install");
var runtime = Path.Combine(install, "versions", "synthetic");
var sibling = Path.Combine(fixture, "owned install sibling");
Directory.CreateDirectory(runtime);
Directory.CreateDirectory(sibling);
Environment.SetEnvironmentVariable("TEMP", fixture);
Environment.SetEnvironmentVariable("TMP", fixture);
int checks = 0;
void Check(bool value, string message) { checks++; if (!value) throw new Exception(message); }
Process StartOwned(string directory)
{
    foreach (var file in Directory.GetFiles(AppContext.BaseDirectory))
        File.Copy(file, Path.Combine(directory, Path.GetFileName(file)), true);
    var start = new ProcessStartInfo(Path.Combine(directory, "ark-mcp.exe"), "--owned-fixture")
    { RedirectStandardInput = true, RedirectStandardOutput = true, RedirectStandardError = true, UseShellExecute = false, CreateNoWindow = true };
    var process = Process.Start(start) ?? throw new Exception("Cannot start owned process fixture");
    if (process.StandardOutput.ReadLine() != "ready") throw new Exception("Owned fixture did not start");
    return process;
}
void StopOwned(Process process)
{
    process.StandardInput.Close();
    if (!process.WaitForExit(5000)) { process.Kill(); process.WaitForExit(); throw new Exception("Owned fixture did not exit on EOF"); }
}
try
{
    Check(ArkProcesses.FindRunningFrom(install) == null, "An unrelated live ARK process blocked this isolated installation");
    using var outside = StartOwned(sibling);
    try
    {
        Check(ArkProcesses.FindRunningFrom(install) == null, "Prefix-sibling process was counted inside the installation");
        using var inside = StartOwned(runtime);
        try
        {
            var match = ArkProcesses.FindRunningFrom(install);
            Check(match != null && match.Pid == inside.Id && match.Name == "ark-mcp.exe" && match.Background, "Background MCP process identity was not returned");
            Check(ArkProcesses.RunningFrom(install), "Standalone/headless MCP no longer blocks installation");
            Check(match != null && Paths.Same(match.Path, Path.Combine(runtime, "ark-mcp.exe")), "Matched process path differs from owned executable");
            foreach (string culture in new[] { "en-US", "pt-PT" })
            {
                CultureInfo.CurrentUICulture = CultureInfo.GetCultureInfo(culture);
                S.Force(culture == "pt-PT" ? "pt" : "en");
                string message = S.ArkRunningProcess(match!);
                Check(message.Contains("ark-mcp.exe") && message.Contains(inside.Id.ToString()), "Installer message omitted executable or PID");
                Check(message.Contains(culture == "en-US" ? "background" : "segundo plano"), "Installer message omitted background explanation");
                string visible = S.ArkRunningProcess(new ArkRunningProcess("ark-studio.exe", 12345, "synthetic", false));
                Check(visible.Contains("ark-studio.exe") && visible.Contains("12345"), "Visible Studio diagnostic omitted process identity");
                string worker = S.ArkRunningProcess(new ArkRunningProcess("ark-occt-worker.exe", 23456, "synthetic", true));
                Check(worker.Contains("ark-occt-worker.exe") && worker.Contains(culture == "en-US" ? "background" : "segundo plano"), "Background task diagnostic omitted identity or scope");
            }
            Check(!inside.HasExited, "Detection or diagnostic generation closed a process");
        }
        finally { StopOwned(inside); }
        Check(ArkProcesses.FindRunningFrom(install) == null, "Exited fixture still blocks installation");
        Check(!outside.HasExited, "Detection affected a process outside this install");
    }
    finally { StopOwned(outside); }
    // the payload is read through one handle: renaming the setup and putting another file at its path changes nothing
    byte[] stub = File.ReadAllBytes(Path.Combine(AppContext.BaseDirectory, "ark-mcp.exe"));
    PeLayout layout;
    using (var head = new MemoryStream(stub)) layout = PeLayout.Read(head) ?? throw new Exception("The fixture exe is not a PE file");
    if (layout.CertSize != 0) throw new Exception("The fixture exe is signed; the synthetic setup needs an unsigned stub");
    byte[] Package(string text)
    {
        using var buffer = new MemoryStream();
        using (var zip = new ZipArchive(buffer, ZipArchiveMode.Create, leaveOpen: true))
        using (var writer = new StreamWriter(zip.CreateEntry("app-version.json").Open()))
            writer.Write(text);
        return buffer.ToArray();
    }
    void WriteSetup(string path, byte[] package)
    {
        var footer = new byte[Payload.FooterSize];
        Encoding.ASCII.GetBytes(Payload.Magic).CopyTo(footer, 0);
        BitConverter.TryWriteBytes(footer.AsSpan(8), (long)stub.Length);
        BitConverter.TryWriteBytes(footer.AsSpan(16), (long)package.Length);
        BitConverter.TryWriteBytes(footer.AsSpan(24), layout.Checksum);
        File.WriteAllBytes(path, stub.Concat(package).Concat(footer).ToArray());
    }
    string setupFile = Path.Combine(fixture, "payload setup.exe");
    byte[] original = Package("original");
    WriteSetup(setupFile, original);
    var payload = Payload.TryOpen(setupFile) ?? throw new Exception("Synthetic payload footer not found");
    string digest = Convert.ToHexString(SHA256.HashData(original)).ToLowerInvariant();
    Check(payload.Offset == stub.Length && payload.Length == original.Length && payload.Sha256(null, CancellationToken.None) == digest, "Synthetic payload located or hashed wrongly");
    bool writable;
    try { using var writer = new FileStream(setupFile, FileMode.Open, FileAccess.Write, FileShare.ReadWrite | FileShare.Delete); writable = true; }
    catch (IOException) { writable = false; }
    Check(!writable, "Another handle could write to the setup while its payload was open");
    File.Move(setupFile, setupFile + ".moved");
    WriteSetup(setupFile, Package("substituted"));
    Check(payload.Sha256(null, CancellationToken.None) == digest, "The payload digest read a file put at the setup's path");
    using (var archive = payload.OpenArchive())
    using (var reader = new StreamReader(archive.GetEntry("app-version.json")!.Open()))
        Check(reader.ReadToEnd() == "original", "The payload extraction read a file put at the setup's path");
    string uninstaller = Path.Combine(fixture, "uninstall.exe");
    payload.WriteStub(uninstaller);
    Check(File.ReadAllBytes(uninstaller).SequenceEqual(stub), "uninstall.exe was not the stub of the opened setup");
    // a Reinstall that finds this version damaged copies it again beside the damaged copy: the folder still needs room
    Directory.CreateDirectory(Path.Combine(install, "versions", ArkInfo.Version));
    var plan = SpacePlan.For(install, 1000, 10);
    Check(plan.TargetNeed == 1010 && plan.TempNeed == 1000, $"Disk space plan for an existing version folder needs {plan.TargetNeed}, not 1010");
    Console.WriteLine($"Installer process 070A: {checks} checks passed; {fixture}");
    return 0;
}
catch (Exception error) { Console.Error.WriteLine(error); return 1; }
