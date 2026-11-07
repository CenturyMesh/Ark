using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.IO.Compression;
using System.Text;
using System.Text.Json.Nodes;
using System.Text.RegularExpressions;

namespace ArkSetup.Core;

/// <summary>Process exit codes (also written to the log).</summary>
public static class ExitCodes
{
    public const int Ok = 0;
    public const int Failed = 1;
    public const int BadArguments = 2;
    public const int ArkRunning = 3;
    public const int NoPayload = 4;
    public const int NotInstalled = 5;
    public const int AlreadyRunning = 6;
    public const int PrepareFailed = 7;
    public const int Cancelled = 10;
}

/// <summary>A failure with a message for the user and an exit code for the caller.</summary>
public sealed class SetupException : Exception
{
    public int ExitCode { get; }
    public SetupException(string message, int exitCode = ExitCodes.Failed, Exception? inner = null)
        : base(message, inner) => ExitCode = exitCode;
}

public enum InstallPhase { Extracting, Preparing, Finishing }

public sealed record InstallProgress(double Fraction, InstallPhase Phase, long BytesDone, long BytesTotal);

public sealed class InstallOptions
{
    public required string Dir { get; init; }
    public bool DesktopShortcut { get; init; } = true;
    public bool StartMenuShortcut { get; init; } = true;
}

public sealed record InstallResult(string Dir, string Version, bool Desktop, bool StartMenu);

/// <summary>
/// Installs ARK the way the Inno Setup installer did: the packaged runtime is extracted to a temporary folder and
/// its own Install-ARK.ps1 (Initialize-ArkInstall in updates\Ark.Update.psm1) builds versions\&lt;version&gt;,
/// launcher, ARK.cmd, current.json and update.lock. Setup adds only uninstall.exe, uninstall-notices.txt,
/// ark-setup.json, the shortcuts and the "Installed apps" entry. Nothing else under %LOCALAPPDATA%\ARK or in the
/// user's documents is touched.
/// </summary>
public static class InstallEngine
{
    /// <summary>Share of the bar for extraction; Install-ARK.ps1 runs up to <see cref="PrepareEnd"/>.</summary>
    public const double ExtractShare = 0.72;
    public const double PrepareEnd = 0.96;
    const int MaxEntries = 4096;
    const long MaxExpandedBytes = 2L * 1024 * 1024 * 1024;
    static readonly TimeSpan PrepareTimeout = TimeSpan.FromMinutes(10);

    public static InstallResult Install(Payload payload, InstallOptions opt, IProgress<InstallProgress>? progress, CancellationToken ct)
    {
        string dir = Paths.Normalize(opt.Dir);
        if (Paths.Problem(dir) is string why)
            throw new SetupException(S.FolderRefused(why), ExitCodes.BadArguments);
        if (ArkProcesses.FindRunningFrom(dir) is { } running)
            throw new SetupException(S.ArkRunningProcess(running), ExitCodes.ArkRunning);
        Log.Info($"installing {ArkInfo.Title} ({ArkInfo.Version}) into {dir}");

        string temp = Path.Combine(Path.GetTempPath(), "ark-setup-" + Guid.NewGuid().ToString("N"));
        string runtime = Path.Combine(temp, "runtime");
        try
        {
            // 1. the portable package, to a temporary folder
            long total = Extract(payload, runtime, progress, ct);
            string version = RuntimeVersion(runtime);
            if (version != ArkInfo.Version)
                Log.Warn($"payload runtime {version} differs from the setup identity {ArkInfo.Version}");
            ct.ThrowIfCancellationRequested();

            // 2. ARK's own installer script builds the layout
            RunInstallScript(runtime, dir, total, progress);
            if (!File.Exists(Path.Combine(dir, "current.json"))
                || !File.Exists(Path.Combine(dir, "versions", version, ArkInfo.StudioExe))
                || !File.Exists(Shortcuts.LauncherScript(dir)))
                throw new SetupException(S.PrepareIncomplete, ExitCodes.PrepareFailed);

            // 3. uninstaller, notices, shortcuts, registry, manifest
            progress?.Report(new InstallProgress(0.97, InstallPhase.Finishing, total, total));
            WriteUninstaller(payload, dir);
            try { Notices.WriteTo(dir); }
            catch (Exception e) { Log.Warn($"could not write {ArkInfo.NoticesName}: {e.Message}"); }

            var shortcuts = new List<string>();
            bool desktop = opt.DesktopShortcut && !ArkInfo.Isolated;
            bool start = opt.StartMenuShortcut && !ArkInfo.Isolated;
            if (desktop && TryShortcut(Shortcuts.DesktopPath, dir, version))
                shortcuts.Add(Shortcuts.DesktopPath);
            if (start && TryShortcut(Shortcuts.StartMenuPath, dir, version))
                shortcuts.Add(Shortcuts.StartMenuPath);
            if (ArkInfo.Isolated)
                Log.Info("test isolation: no shortcuts");

            UninstallEntry.ReplaceInno(dir);
            long size = Paths.SizeOf(Path.Combine(dir, "versions")) + Paths.SizeOf(Path.Combine(dir, "launcher"))
                        + new FileInfo(Path.Combine(dir, ArkInfo.UninstallerName)).Length;
            UninstallEntry.Write(dir, version, size);
            new SetupManifest
            {
                Version = version,
                Code = ArkInfo.Code,
                InstalledAt = DateTime.UtcNow.ToString("yyyy-MM-ddTHH:mm:ssZ", CultureInfo.InvariantCulture),
                UninstallKey = ArkInfo.UninstallKeyName,
                Shortcuts = shortcuts,
            }.Write(dir);
            progress?.Report(new InstallProgress(1.0, InstallPhase.Finishing, total, total));
            Log.Info("install finished");
            return new InstallResult(dir, version, shortcuts.Contains(Shortcuts.DesktopPath), shortcuts.Contains(Shortcuts.StartMenuPath));
        }
        finally
        {
            DeleteTemp(temp);
        }
    }

    /// <summary>
    /// Extracts the package with the same rules as Expand-ArkPackage: relative, safe names only, no links, bounded
    /// entry count and size, each entry exactly its declared length. Returns the total bytes.
    /// </summary>
    static long Extract(Payload payload, string target, IProgress<InstallProgress>? progress, CancellationToken ct)
    {
        Directory.CreateDirectory(target);
        using var zip = payload.OpenArchive();
        if (zip.Entries.Count > MaxEntries)
            throw new SetupException(S.BadPayload("too many entries"), ExitCodes.NoPayload);
        long total = Math.Max(1, zip.Entries.Sum(e => e.Length));
        if (total > MaxExpandedBytes)
            throw new SetupException(S.BadPayload("too large"), ExitCodes.NoPayload);
        var seen = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        long done = 0;
        var clock = Stopwatch.StartNew();
        long lastReport = -1000;
        var buffer = new byte[1 << 20];
        foreach (var entry in zip.Entries)
        {
            ct.ThrowIfCancellationRequested();
            string name = entry.FullName;
            if (!SafeEntryName(name))
                throw new SetupException(S.BadPayload($"unsafe path {name}"), ExitCodes.NoPayload);
            if (!seen.Add(name.TrimEnd('/')))
                throw new SetupException(S.BadPayload($"duplicate path {name}"), ExitCodes.NoPayload);
            int unixType = (entry.ExternalAttributes >> 16) & 0xF000;
            if (unixType == 0xA000 || (entry.ExternalAttributes & 0x400) != 0)
                throw new SetupException(S.BadPayload($"link {name}"), ExitCodes.NoPayload);
            string dest = Path.GetFullPath(Path.Combine(target, name.Replace('/', Path.DirectorySeparatorChar)));
            if (!Paths.IsInside(dest, target) || Paths.Same(dest, target))
                throw new SetupException(S.BadPayload($"unsafe path {name}"), ExitCodes.NoPayload);
            if (name.EndsWith('/'))
            {
                Directory.CreateDirectory(dest);
                continue;
            }
            Directory.CreateDirectory(Path.GetDirectoryName(dest)!);
            long written = 0;
            using (var src = entry.Open())
            using (var dst = new FileStream(dest, FileMode.CreateNew, FileAccess.Write, FileShare.None, 1 << 16))
            {
                int n;
                while ((n = src.Read(buffer, 0, buffer.Length)) > 0)
                {
                    ct.ThrowIfCancellationRequested();
                    written += n;
                    if (written > entry.Length)
                        throw new SetupException(S.BadPayload($"size of {name}"), ExitCodes.NoPayload);
                    dst.Write(buffer, 0, n);
                    if (clock.ElapsedMilliseconds - lastReport >= 40)
                    {
                        lastReport = clock.ElapsedMilliseconds;
                        progress?.Report(new InstallProgress((double)(done + written) / total * ExtractShare, InstallPhase.Extracting, done + written, total));
                    }
                }
            }
            if (written != entry.Length)
                throw new SetupException(S.BadPayload($"size of {name}"), ExitCodes.NoPayload);
            done += written;
        }
        progress?.Report(new InstallProgress(ExtractShare, InstallPhase.Extracting, total, total));
        Log.Info($"extracted {zip.Entries.Count} entries, {Paths.FormatSize(done)} in {clock.Elapsed.TotalSeconds:0.0} s");
        return total;
    }

    static bool SafeEntryName(string path)
    {
        if (string.IsNullOrEmpty(path) || path.Length > 240 || path.StartsWith('/') || path.Contains("//")
            || Regex.IsMatch(path, @"[\\:\x00-\x1f]"))
            return false;
        foreach (string segment in path.TrimEnd('/').Split('/'))
        {
            if (segment is "" or "." or ".." || Regex.IsMatch(segment, "[<>\"|?*]") || segment.EndsWith('.') || segment.EndsWith(' ')
                || Regex.IsMatch(segment, @"^(?i:CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\.|$)"))
                return false;
        }
        return true;
    }

    /// <summary>The runtime version from app-version.json (the same checks as Get-ArkRuntimeVersion's header).</summary>
    static string RuntimeVersion(string runtime)
    {
        string meta = Path.Combine(runtime, "app-version.json");
        string script = Path.Combine(runtime, "Install-ARK.ps1");
        if (!File.Exists(meta) || !File.Exists(script) || !File.Exists(Path.Combine(runtime, "updates", "Ark.Update.psm1")))
            throw new SetupException(S.BadPayload("not an ARK package"), ExitCodes.NoPayload);
        var node = JsonNode.Parse(File.ReadAllText(meta)) as JsonObject;
        string? version = node?["version"]?.GetValue<string>();
        if (node?["product"]?.GetValue<string>() != "ARK" || node?["platform"]?.GetValue<string>() != "windows-x64"
            || version == null || !Regex.IsMatch(version, @"^(0|[1-9][0-9]{0,8})\.(0|[1-9][0-9]{0,8})\.(0|[1-9][0-9]{0,8})$"))
            throw new SetupException(S.BadPayload("runtime identity"), ExitCodes.NoPayload);
        return version;
    }

    /// <summary>
    /// powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File &lt;tmp&gt;\Install-ARK.ps1 -InstallRoot &lt;dir&gt;,
    /// exactly as Inno ran it. Its output goes to the log; a failure surfaces its exit code and first error line.
    /// </summary>
    static void RunInstallScript(string runtime, string dir, long total, IProgress<InstallProgress>? progress)
    {
        var psi = new ProcessStartInfo(ArkInfo.PowerShellPath)
        {
            WorkingDirectory = runtime,
            UseShellExecute = false,
            CreateNoWindow = true,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            StandardOutputEncoding = ConsoleEncoding(),
            StandardErrorEncoding = ConsoleEncoding(),
        };
        foreach (string a in new[] { "-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass", "-File", Path.Combine(runtime, "Install-ARK.ps1"), "-InstallRoot", dir })
            psi.ArgumentList.Add(a);
        var output = new List<string>();
        var errors = new List<string>();
        Log.Info($"running {psi.FileName} {string.Join(' ', psi.ArgumentList.Select(a => a.Contains(' ') ? $"\"{a}\"" : a))}");
        progress?.Report(new InstallProgress(ExtractShare, InstallPhase.Preparing, total, total));
        Process process;
        try
        {
            process = Process.Start(psi) ?? throw new InvalidOperationException("no process");
        }
        catch (Exception e)
        {
            throw new SetupException(S.PowerShellMissing, ExitCodes.PrepareFailed, e);
        }
        using (process)
        {
            process.OutputDataReceived += (_, e) => { if (e.Data != null) lock (output) output.Add(e.Data); };
            process.ErrorDataReceived += (_, e) => { if (e.Data != null) lock (errors) errors.Add(e.Data); };
            process.BeginOutputReadLine();
            process.BeginErrorReadLine();
            var clock = Stopwatch.StartNew();
            while (!process.WaitForExit(120))
            {
                // the script reports no progress: creep towards the end of its share of the bar
                double t = clock.Elapsed.TotalSeconds;
                progress?.Report(new InstallProgress(ExtractShare + (PrepareEnd - ExtractShare) * (1 - Math.Exp(-t / 12.0)), InstallPhase.Preparing, total, total));
                if (clock.Elapsed > PrepareTimeout)
                {
                    try { process.Kill(entireProcessTree: true); } catch (Exception) { }
                    throw new SetupException(S.PrepareTimedOut, ExitCodes.PrepareFailed);
                }
            }
            process.WaitForExit(); // drain the redirected streams
            foreach (string line in output)
                Log.Info($"Install-ARK.ps1: {line}");
            foreach (string line in errors)
                Log.Warn($"Install-ARK.ps1 stderr: {line}");
            Log.Info($"Install-ARK.ps1 exited with {process.ExitCode} after {clock.Elapsed.TotalSeconds:0.0} s");
            if (process.ExitCode != 0)
            {
                string detail = errors.Concat(output).Select(l => l.Trim()).FirstOrDefault(l => l.Length > 0) ?? "";
                if (detail.Length > 300)
                    detail = detail[..300] + "…";
                string message = S.PrepareFailed(process.ExitCode) + (detail.Length > 0 ? "\n" + detail : "");
                throw new SetupException(message, ExitCodes.PrepareFailed);
            }
        }
        progress?.Report(new InstallProgress(PrepareEnd, InstallPhase.Preparing, total, total));
    }

    /// <summary>Windows PowerShell writes redirected text in the OEM code page.</summary>
    static Encoding ConsoleEncoding()
    {
        try
        {
            Encoding.RegisterProvider(CodePagesEncodingProvider.Instance);
            return Encoding.GetEncoding(CultureInfo.CurrentCulture.TextInfo.OEMCodePage);
        }
        catch (Exception)
        {
            return Encoding.UTF8;
        }
    }

    static void WriteUninstaller(Payload payload, string dir)
    {
        string stub = Path.Combine(dir, ArkInfo.UninstallerName);
        string temp = stub + ".new";
        try
        {
            payload.WriteStub(temp);
            Retry(() => File.Move(temp, stub, overwrite: true), ArkInfo.UninstallerName);
        }
        catch (Exception e)
        {
            Log.Error($"could not write {ArkInfo.UninstallerName}", e);
            try { File.Delete(temp); } catch (Exception) { }
            if (!File.Exists(stub))
                throw new SetupException(S.FileLocked(ArkInfo.UninstallerName), ExitCodes.Failed, e);
        }
    }

    static bool TryShortcut(string lnk, string dir, string version)
    {
        try
        {
            Shortcuts.CreateLauncher(lnk, dir, version);
            Log.Info($"shortcut {lnk}");
            return true;
        }
        catch (Exception e)
        {
            Log.Warn($"could not create shortcut {lnk}: {e.Message}");
            return false;
        }
    }

    /// <summary>Retries a file operation for a few seconds while another process holds the file.</summary>
    static void Retry(Action action, string what)
    {
        for (int attempt = 1; ; attempt++)
        {
            try
            {
                action();
                return;
            }
            catch (IOException e) when (attempt < 12 && e is not FileNotFoundException && e is not DirectoryNotFoundException)
            {
                Log.Warn($"{what} is busy ({e.Message}), retry {attempt}");
                Thread.Sleep(350);
            }
            catch (Exception e) when (e is IOException or UnauthorizedAccessException)
            {
                throw new SetupException(S.FileLocked(what), ExitCodes.Failed, e);
            }
        }
    }

    static void DeleteTemp(string temp)
    {
        try
        {
            if (Directory.Exists(temp) && Paths.IsInside(temp, Path.GetTempPath()) && Path.GetFileName(temp).StartsWith("ark-setup-", StringComparison.Ordinal))
                Directory.Delete(temp, recursive: true);
        }
        catch (Exception e)
        {
            Log.Warn($"could not remove the temporary folder {temp}: {e.Message}");
        }
    }

    /// <summary>Starts ARK through its launcher, exactly as the shortcuts do (no console window).</summary>
    public static void OpenArk(string dir)
    {
        var psi = new ProcessStartInfo(ArkInfo.PowerShellPath)
        {
            WorkingDirectory = Shortcuts.WorkingDirectory,
            UseShellExecute = false,
            CreateNoWindow = true,
            Arguments = Shortcuts.LauncherArguments(Paths.Normalize(dir)),
        };
        Log.Info($"opening ARK: {psi.FileName} {psi.Arguments}");
        Process.Start(psi)?.Dispose();
    }
}
