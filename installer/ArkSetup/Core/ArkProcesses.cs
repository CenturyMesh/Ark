using System.Diagnostics;
using System.IO;

namespace ArkSetup.Core;

public sealed record ArkRunningProcess(string Name, int Pid, string Path, bool Background);

/// <summary>ARK processes started from this install's versions folder.</summary>
public static class ArkProcesses
{
    public static bool RunningFrom(string dir) => FindRunningFrom(dir) != null;

    public static ArkRunningProcess? FindRunningFrom(string dir)
    {
        string versions = System.IO.Path.Combine(Paths.Normalize(dir), "versions") + System.IO.Path.DirectorySeparatorChar;
        foreach (string name in ArkInfo.RuntimeProcesses)
        {
            var processes = Process.GetProcessesByName(name);
            try
            {
                foreach (var p in processes)
                {
                    try
                    {
                        string? path = p.MainModule?.FileName;
                        if (path != null && System.IO.Path.GetFullPath(path).StartsWith(versions, StringComparison.OrdinalIgnoreCase))
                        {
                            bool background = true;
                            try { background = p.MainWindowHandle == IntPtr.Zero; } catch (Exception) { }
                            var result = new ArkRunningProcess(name + ".exe", p.Id, path, background);
                            Log.Info($"{result.Name} (pid {result.Pid}, background={result.Background}) is running from {path}");
                            return result;
                        }
                    }
                    catch (Exception) { }
                }
            }
            finally { foreach (var p in processes) p.Dispose(); }
        }
        return null;
    }
}
