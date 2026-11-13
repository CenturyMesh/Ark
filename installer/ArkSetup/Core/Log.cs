using System.IO;
using System.Text;

namespace ArkSetup.Core;

/// <summary>Plain text log at %TEMP%\ARK-Setup.log, appended by every run (cleared when it grows big).</summary>
public static class Log
{
    static readonly object Gate = new();
    public static string FilePath { get; } = Path.Combine(Path.GetTempPath(), "ARK-Setup.log");

    /// <summary>A second file that receives the same lines (/LOG=FILE, as Inno Setup's switch).</summary>
    public static string? MirrorPath { get; set; }

    public static void Start(string[] args)
    {
        try
        {
            var info = new FileInfo(FilePath);
            if (info.Exists && info.Length > 4 * 1024 * 1024)
                info.Delete();
        }
        catch (IOException) { }
        catch (UnauthorizedAccessException) { }
        Write("----", "");
        Info($"ARK Setup {ArkInfo.Title} ({ArkInfo.Version}), pid {Environment.ProcessId}, exe {Environment.ProcessPath}");
        Info($"args: {string.Join(' ', args.Select(a => a.Contains(' ') ? $"\"{a}\"" : a))}");
        if (ArkInfo.Isolated)
            Info($"test isolation {ArkInfo.IsolationId}: uninstall key {ArkInfo.UninstallKeyName}, no shortcuts");
    }

    public static void Info(string message) => Write("INFO ", message);
    public static void Warn(string message) => Write("WARN ", message);
    public static void Error(string message) => Write("ERROR", message);
    public static void Error(string message, Exception e) => Write("ERROR", $"{message}: {e}");

    static void Write(string level, string message)
    {
        string line = level == "----"
            ? new string('-', 72)
            : $"[{DateTime.Now:yyyy-MM-dd HH:mm:ss.fff}] {level} {message}";
        lock (Gate)
        {
            foreach (string? path in new[] { FilePath, MirrorPath })
            {
                if (path == null)
                    continue;
                try { File.AppendAllText(path, line + Environment.NewLine, Encoding.UTF8); }
                catch (IOException) { }
                catch (UnauthorizedAccessException) { }
            }
        }
    }
}
