using System.Diagnostics;
using System.Globalization;
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
    Console.WriteLine($"Installer process 070A: {checks} checks passed; {fixture}");
    return 0;
}
catch (Exception error) { Console.Error.WriteLine(error); return 1; }
