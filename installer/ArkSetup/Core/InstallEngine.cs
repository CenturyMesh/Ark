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
    public const int Downgrade = 8;
    public const int NoSpace = 9;
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
    /// <summary>--allow-downgrade: install and select this runtime although the folder holds a newer one.</summary>
    public bool AllowDowngrade { get; init; }
}

public sealed record InstallResult(string Dir, string Version, bool Desktop, bool StartMenu);

/// <summary>
/// Installs ARK the way the Inno Setup installer did: the packaged runtime is extracted to a temporary folder and
/// its own Install-ARK.ps1 (Initialize-ArkInstall in updates\Ark.Update.psm1) builds versions\&lt;version&gt;,
/// launcher, ARK.cmd, current.json and update.lock. Setup adds only uninstall.exe, uninstall-notices.txt,
/// ark-setup.json, the shortcuts and the "Installed apps" entry. Nothing else under %LOCALAPPDATA%\ARK or in the
/// user's documents is touched.
///
/// When the folder already has this version, Setup first verifies it against its receipt and rebuilds it from the
/// package when it is damaged or, if the launcher selects it, missing (Reinstall repairs). A folder whose launcher runs
/// or has prepared a newer version is refused unless <see cref="InstallOptions.AllowDowngrade"/>, which installs this
/// version and selects it, keeping the version the launcher would have started as "previous" for ARK's own rollback.
/// </summary>
public static class InstallEngine
{
    /// <summary>Share of the bar for checking the package digest, then for extraction; Install-ARK.ps1 runs up to <see cref="PrepareEnd"/>.</summary>
    public const double DigestShare = 0.08;
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
        if (TargetProblem(dir) is string target)
            throw new SetupException(target, ExitCodes.BadArguments);
        if (ArkProcesses.FindRunningFrom(dir) is { } running)
            throw new SetupException(S.ArkRunningProcess(running), ExitCodes.ArkRunning);
        UpdateLock.Probe(dir);
        string digest = ArkInfo.PayloadSha256 ?? throw new SetupException(S.BadPayload("no package digest"), ExitCodes.NoPayload);
        string version = ArkInfo.Version;
        Log.Info($"installing {ArkInfo.Title} ({version}) into {dir}");

        // the launcher state decides between install, update, reinstall and downgrade before anything is written
        var state = UninstallEntry.ReadState(dir);
        bool select = false;
        if (state != null)
        {
            Log.Info($"installed: current {state.Current}, pending {state.Pending ?? "-"}, activating {state.Activating ?? "-"}");
            if (state.Activating != null)
                throw new SetupException(S.ActivationPending, ExitCodes.ArkRunning);
            if (System.Version.Parse(state.Newest) > System.Version.Parse(version))
            {
                if (!opt.AllowDowngrade)
                    throw new SetupException(S.Downgrade(state.Newest, version), ExitCodes.Downgrade);
                select = true;
                Log.Info($"downgrade allowed: {version} replaces {state.Newest} as the selected version");
            }
        }
        long expanded;
        try { expanded = payload.UncompressedSize(); }
        catch (Exception e) when (e is InvalidDataException or IOException) { throw new SetupException(S.BadPayload(e.Message), ExitCodes.NoPayload, e); }
        var space = SpacePlan.For(dir, expanded, payload.Offset);
        Log.Info(space.ToString());
        if (space.Problem() is string shortage)
            throw new SetupException(shortage, ExitCodes.NoSpace);

        using var temp = SetupTemp.Create();
        string runtime = Path.Combine(temp.Dir, "runtime");
        // 1. the package, checked against the digest stamped at build time, to a temporary folder
        VerifyDigest(payload, digest, expanded, progress, ct);
        long total = Extract(payload, runtime, progress, ct);
        string runtimeVersion = RuntimeVersion(runtime);
        if (runtimeVersion != version)
            throw new SetupException(S.BadPayload($"runtime {runtimeVersion} is not {version}"), ExitCodes.NoPayload);
        ct.ThrowIfCancellationRequested();

        // 2. a new folder is marked as ARK's before anything else goes in, so a retry after an interruption is accepted
        if (state == null && !File.Exists(SetupManifest.PathIn(dir)))
        {
            Directory.CreateDirectory(dir);
            new SetupManifest { Code = ArkInfo.Code, UninstallKey = ArkInfo.UninstallKeyName }.Write(dir);
        }

        // 3. this version's runtime, if the folder has it or the launcher selects it, is verified and repaired (copied
        //    again when its folder is missing); a downgrade selects it
        if (select || state?.Selected == version || Directory.Exists(Path.Combine(dir, "versions", version)))
            RunRepairScript(temp.Dir, runtime, dir, select, total, progress);

        // 4. ARK's own installer script builds or completes the layout
        RunInstallScript(runtime, dir, total, progress);
        string? selected = UninstallEntry.InstalledVersion(dir);
        if (selected != version
            || !File.Exists(Path.Combine(dir, "versions", version, ArkInfo.StudioExe))
            || !File.Exists(Shortcuts.LauncherScript(dir)))
        {
            Log.Error($"after Install-ARK.ps1 the launcher selects {selected ?? "nothing"}, not {version}");
            throw new SetupException(S.PrepareIncomplete, ExitCodes.PrepareFailed);
        }

        // 5. uninstaller, notices, shortcuts, registry, manifest
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

        long size = Paths.SizeOf(Path.Combine(dir, "versions")) + Paths.SizeOf(Path.Combine(dir, "launcher"))
                    + new FileInfo(Path.Combine(dir, ArkInfo.UninstallerName)).Length;
        // our entry first: a failed write leaves the Inno Setup entry (if any) in place, never no entry at all
        UninstallEntry.Write(dir, version, size);
        try { UninstallEntry.ReplaceInno(dir); }
        catch (Exception e) { Log.Warn($"could not replace the Inno Setup entry: {e.Message}"); }
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

    /// <summary>
    /// Why ARK may not go into <paramref name="dir"/> although the place itself is acceptable (null: it may). Uninstall
    /// removes "versions" and "launcher" from the install folder, so a folder that already holds other things must
    /// be an ARK install: current.json or ark-setup.json, or the folder of the Inno Setup ARK registered for it.
    /// Missing and empty folders are fine. The result is a message for the user.
    /// </summary>
    public static string? TargetProblem(string dir)
    {
        string d = Paths.Normalize(dir);
        if (File.Exists(d))
            return S.FolderRefused("a file");
        if (!Directory.Exists(d))
            return null;
        if ((File.GetAttributes(d) & FileAttributes.ReparsePoint) != 0)
            return S.FolderRefused("a link");
        if (!Directory.EnumerateFileSystemEntries(d).Any())
            return null;
        if (File.Exists(Path.Combine(d, "current.json")) || File.Exists(SetupManifest.PathIn(d)) || UninstallEntry.InnoOwns(d))
            return null;
        return S.FolderNotEmpty;
    }

    /// <summary>Compares the package bytes in this exe with the SHA-256 that build-installer.ps1 stamped into it.</summary>
    static void VerifyDigest(Payload payload, string digest, long expanded, IProgress<InstallProgress>? progress, CancellationToken ct)
    {
        var clock = Stopwatch.StartNew();
        long lastReport = -1000;
        string actual = payload.Sha256(new InlineProgress<long>(done =>
        {
            if (clock.ElapsedMilliseconds - lastReport < 40)
                return;
            lastReport = clock.ElapsedMilliseconds;
            double f = (double)done / payload.Length * DigestShare;
            progress?.Report(new InstallProgress(f, InstallPhase.Extracting, (long)(expanded * f / ExtractShare), expanded));
        }), ct);
        if (!actual.Equals(digest, StringComparison.Ordinal))
        {
            Log.Error($"package SHA-256 {actual} differs from the stamped {digest}");
            throw new SetupException(S.BadPayload("checksum"), ExitCodes.NoPayload);
        }
        Log.Info($"package SHA-256 verified ({Paths.FormatSize(payload.Length)} in {clock.Elapsed.TotalSeconds:0.0} s)");
    }

    /// <summary>A synchronous <see cref="IProgress{T}"/> (Progress&lt;T&gt; would post to the UI thread).</summary>
    sealed class InlineProgress<T>(Action<T> report) : IProgress<T>
    {
        public void Report(T value) => report(value);
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
                        progress?.Report(new InstallProgress(DigestShare + (double)(done + written) / total * (ExtractShare - DigestShare), InstallPhase.Extracting, done + written, total));
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
        RunScript(Path.Combine(runtime, "Install-ARK.ps1"), new[] { "-InstallRoot", dir }, runtime, total, progress, creep: true);
        progress?.Report(new InstallProgress(PrepareEnd, InstallPhase.Preparing, total, total));
    }

    /// <summary>
    /// Setup's own step, with the package's own updates\Ark.Update.psm1 under update.lock: verifies this version's
    /// runtime against its receipt (Assert-ArkInstalledRuntime) and, when that fails, moves it to -Aside and copies it
    /// again from the package (Copy-ArkLocalRuntime); a missing folder of the version the launcher selects is copied
    /// again. With -Select it also makes this version the launcher's current one, keeping the version the launcher would
    /// have started (pending, else current) as "previous". REPAIRED is printed only after the new copy passed
    /// verification: only then does Setup delete the damaged copy (<see cref="SettleAside"/>).
    /// </summary>
    const string RepairScript = """
        param([Parameter(Mandatory=$true)][string]$Runtime, [Parameter(Mandatory=$true)][string]$Root,
              [Parameter(Mandatory=$true)][string]$Aside, [switch]$Select)
        $ErrorActionPreference = 'Stop'
        Set-StrictMode -Version Latest
        Import-Module (Join-Path $Runtime 'updates\Ark.Update.psm1') -Force
        $version = Get-ArkRuntimeVersion $Runtime
        $target = Assert-ArkChildPath $Root (Join-Path $Root "versions\$version")
        $aside = Assert-ArkChildPath $Root $Aside
        if ((Split-Path $aside -Leaf) -cnotmatch '^stage-[0-9a-f]{32}$' -or (Test-Path -LiteralPath $aside)) { throw 'Invalid repair folder.' }
        $lock = Enter-ArkUpdateLock $Root
        try {
            $state = $null
            if (Test-Path -LiteralPath (Join-Path $Root 'current.json')) { $state = Get-ArkState $Root }
            if ($Select -and $state -and $state.activating) { throw 'An ARK update is still being activated.' }
            $selected = if (-not $state) { '' } elseif ($state.pending) { [string]$state.pending } else { [string]$state.current }
            if (Test-Path -LiteralPath $target) {
                $damage = $null
                try { Assert-ArkInstalledRuntime $Root $version | Out-Null } catch { $damage = $_.Exception.Message }
                if ($damage) {
                    Write-Output "runtime $version failed verification: $damage"
                    [IO.Directory]::Move($target, $aside)
                    Write-Output "moved the damaged copy to $aside"
                    try { Copy-ArkLocalRuntime $Runtime $Root | Out-Null; Assert-ArkInstalledRuntime $Root $version | Out-Null }
                    catch { if (-not (Test-Path -LiteralPath $target)) { [IO.Directory]::Move($aside, $target); Write-Output 'moved the damaged copy back' }; throw }
                    Write-Output 'REPAIRED'
                } else { Write-Output "verified runtime $version" }
            } elseif ($Select -or $selected -ceq $version) {
                Write-Output "runtime $version is missing"
                Copy-ArkLocalRuntime $Runtime $Root | Out-Null
                Write-Output "copied runtime $version"
            }
            if ($Select -and $state -and ($state.current -cne $version -or $state.pending)) {
                $replaced = if ($state.pending -and $state.pending -cne $version) { [string]$state.pending } else { [string]$state.current }
                if ($replaced -cne $version) { $state.previous = $replaced }
                $state.current = $version; $state.pending = ''; $state.activating = ''
                Write-ArkAtomicJson (Join-Path $Root 'current.json') $state
                Write-Output "selected runtime $version (previous $($state.previous))"
            }
        } finally { $lock.Dispose() }
        """;

    static void RunRepairScript(string temp, string runtime, string dir, bool select, long total, IProgress<InstallProgress>? progress)
    {
        string script = Path.Combine(temp, "ark-setup-repair.ps1");
        File.WriteAllText(script, RepairScript, new UTF8Encoding(true));
        // Setup names the aside folder, so it finds the damaged copy even when the script is killed part-way
        string aside = Path.Combine(dir, "stage-" + Guid.NewGuid().ToString("N"));
        var args = new List<string> { "-Runtime", runtime, "-Root", dir, "-Aside", aside };
        if (select)
            args.Add("-Select");
        var output = new List<string>();
        try { RunScript(script, args, runtime, total, progress, creep: false, output); }
        finally
        {
            bool repaired;
            lock (output) repaired = output.Contains("REPAIRED");
            SettleAside(aside, repaired, Path.Combine(dir, "versions", ArkInfo.Version), dir);
        }
    }

    /// <summary>
    /// The damaged copy the repair moved aside: deleted (links removed, never followed) only when the script reported the
    /// new copy verified. Otherwise (the script failed or was killed part-way) it is the only copy of that runtime: it
    /// goes back to versions\&lt;version&gt; when that is still missing, else it stays where it is (uninstall removes it).
    /// </summary>
    static void SettleAside(string aside, bool repaired, string target, string dir)
    {
        if (!Directory.Exists(aside))
            return;
        if (repaired)
        {
            Log.Info(Uninstaller.RemoveTree(aside, dir) ? $"removed the damaged copy {aside}" : $"could not remove all of {aside}");
            return;
        }
        if (!Directory.Exists(target))
        {
            try
            {
                using (UpdateLock.Enter(dir))
                    if (!Directory.Exists(target))
                    {
                        Directory.Move(aside, target);
                        Log.Warn($"the repair did not finish: moved {aside} back to {target}");
                        return;
                    }
            }
            catch (Exception e) when (e is IOException or UnauthorizedAccessException or SetupException)
            {
                Log.Warn($"could not move {aside} back to {target}: {e.Message}");
            }
        }
        Log.Warn($"the repair did not finish: kept the previous copy of this runtime in {aside}");
    }

    /// <summary>Runs a script with Windows PowerShell, collecting its output lines; throws with its exit code and first error line.</summary>
    static void RunScript(string scriptPath, IEnumerable<string> scriptArgs, string workingDir, long total, IProgress<InstallProgress>? progress, bool creep, List<string>? output = null)
    {
        string label = Path.GetFileName(scriptPath);
        var psi = new ProcessStartInfo(ArkInfo.PowerShellPath)
        {
            WorkingDirectory = workingDir,
            UseShellExecute = false,
            CreateNoWindow = true,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            StandardOutputEncoding = ConsoleEncoding(),
            StandardErrorEncoding = ConsoleEncoding(),
        };
        foreach (string a in new[] { "-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass", "-File", scriptPath }.Concat(scriptArgs))
            psi.ArgumentList.Add(a);
        output ??= new List<string>();
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
                if (creep)
                    progress?.Report(new InstallProgress(ExtractShare + (PrepareEnd - ExtractShare) * (1 - Math.Exp(-t / 12.0)), InstallPhase.Preparing, total, total));
                if (clock.Elapsed > PrepareTimeout)
                {
                    // waits a little, so the script's update.lock and file handles are closed before Setup cleans up
                    try { process.Kill(entireProcessTree: true); process.WaitForExit(10000); } catch (Exception) { }
                    throw new SetupException(S.PrepareTimedOut, ExitCodes.PrepareFailed);
                }
            }
            process.WaitForExit(); // drain the redirected streams
            foreach (string line in output)
                Log.Info($"{label}: {line}");
            foreach (string line in errors)
                Log.Warn($"{label} stderr: {line}");
            Log.Info($"{label} exited with {process.ExitCode} after {clock.Elapsed.TotalSeconds:0.0} s");
            if (process.ExitCode != 0)
            {
                string detail = errors.Concat(output).Select(l => l.Trim()).FirstOrDefault(l => l.Length > 0) ?? "";
                if (detail.Length > 300)
                    detail = detail[..300] + "…";
                string message = S.PrepareFailed(process.ExitCode) + (detail.Length > 0 ? "\n" + detail : "");
                throw new SetupException(message, ExitCodes.PrepareFailed);
            }
        }
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
