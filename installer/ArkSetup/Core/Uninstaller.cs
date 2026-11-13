using System.Diagnostics;
using System.IO;
using System.Text.RegularExpressions;

namespace ArkSetup.Core;

/// <summary>
/// Removes an install the way the Inno uninstaller did: versions, launcher, ARK.cmd, current.json and update.lock,
/// plus what Setup added (uninstall.exe, uninstall-notices.txt, ark-setup.json, shortcuts, the registry entry) and
/// transient files of ARK's own updater. The folder itself goes only when it is then empty. Nothing outside the
/// install folder is touched: user data in %LOCALAPPDATA%\ARK and documents stay.
/// </summary>
public static class Uninstaller
{
    static readonly string[] RuntimeFolders = { "versions", "launcher" };
    static readonly string[] RuntimeFiles = { "ARK.cmd", "current.json", "update.lock" };
    // Leftovers of an interrupted update or launch (Ark.Update.psm1 / ark-launch.ps1).
    static readonly Regex UpdaterLeftover = new(@"^(stage-[0-9a-f]{32}|baseline-[0-9a-f]{32}|download-[0-9a-f]{32}\.zip|ack-[0-9a-f]{32}\.tmp|current\.json\.[0-9a-f]{32}\.tmp)$",
                                                RegexOptions.IgnoreCase);

    /// <summary>True when the folder holds an ARK install this uninstaller may remove.</summary>
    public static bool LooksInstalled(string dir) =>
        File.Exists(SetupManifest.PathIn(dir))
        || (File.Exists(Path.Combine(dir, "current.json")) && Directory.Exists(Path.Combine(dir, "launcher")));

    /// <summary>Returns true when the folder is gone (false: other files were left behind).</summary>
    public static bool Run(string dirArg, IProgress<double>? progress)
    {
        string dir = Paths.Normalize(dirArg);
        Log.Info($"uninstalling from {dir}");
        if (!LooksInstalled(dir))
            throw new SetupException(S.NotInstalledHere, ExitCodes.NotInstalled);
        if (Paths.Problem(dir) is string why)
            throw new SetupException($"Refusing to uninstall from {dir}: {why}.", ExitCodes.BadArguments);

        var manifest = SetupManifest.Read(dir);
        string keyName = manifest?.UninstallKey ?? ArkInfo.UninstallKeyName;
        bool isolated = ArkInfo.Isolated || keyName.Contains(".Test.", StringComparison.Ordinal);
        string self = Environment.ProcessPath ?? "";
        bool selfInside = self.Length > 0 && Paths.IsInside(self, dir);

        // 1. the runtime layout (all or nothing: on failure the entry stays so the uninstall can be retried), under
        // update.lock, taken before anything is checked or removed: a launch or update in progress fails the uninstall
        // at once, and none can start or stage a version while it goes
        using (UpdateLock.Enter(dir))
        {
            if (ArkProcesses.FindRunningFrom(dir) is { } running)
                throw new SetupException(S.ArkRunningProcess(running), ExitCodes.ArkRunning);
            progress?.Report(0.04);
            var files = new List<string>();
            var folders = new List<string>();
            foreach (string name in RuntimeFolders)
                Collect(Path.Combine(dir, name), files, folders);
            foreach (string entry in Directory.EnumerateFileSystemEntries(dir))
                if (UpdaterLeftover.IsMatch(Path.GetFileName(entry)))
                {
                    if (Directory.Exists(entry))
                        Collect(entry, files, folders);
                    else
                        files.Add(entry);
                }
            foreach (string name in RuntimeFiles)
                files.Add(Path.Combine(dir, name));
            int failed = 0;
            for (int i = 0; i < files.Count; i++)
            {
                if (!DeleteFile(files[i], dir))
                    failed++;
                if (i % 8 == 0)
                    progress?.Report(0.04 + 0.86 * (i + 1) / files.Count);
            }
            foreach (string folder in folders.OrderByDescending(f => f.Length))
                RemoveDir(folder, dir);
            if (failed > 0 || RuntimeFolders.Any(f => Directory.Exists(Path.Combine(dir, f))))
            {
                Log.Error($"{failed} files could not be removed");
                throw new SetupException(S.FilesInUse, ExitCodes.Failed);
            }
        }

        // 2. what Setup added
        Shortcuts.RemoveFor(dir, manifest?.Shortcuts ?? new List<string>(), includeStandard: !isolated);
        UninstallEntry.RemoveFor(dir, keyName);
        DeleteFile(Path.Combine(dir, ArkInfo.NoticesName), dir);
        DeleteFile(SetupManifest.PathIn(dir), dir);
        string stub = Path.Combine(dir, ArkInfo.UninstallerName);
        if (!(selfInside && Paths.Same(stub, self)))
            DeleteFile(stub, dir);
        progress?.Report(1.0);

        if (selfInside)
        {
            ScheduleSelfDelete(self, dir);
            bool onlySelfLeft = Directory.EnumerateFileSystemEntries(dir).All(e => Paths.Same(e, self));
            Log.Info(onlySelfLeft ? "uninstall finished (the folder goes when this process exits)" : "uninstall finished, other files left");
            return onlySelfLeft;
        }
        bool gone = RemoveDir(dir, dir);
        Log.Info(gone ? "uninstall finished, folder removed" : "uninstall finished, other files left in the folder");
        return gone;
    }

    /// <summary>
    /// Deletes the folder <paramref name="root"/> inside <paramref name="dir"/> with everything below it, removing
    /// links without following them. Returns true when it is gone.
    /// </summary>
    public static bool RemoveTree(string root, string dir)
    {
        if (!Paths.IsInside(root, dir) || Paths.Same(root, dir))
            return false;
        var files = new List<string>();
        var folders = new List<string>();
        Collect(root, files, folders);
        foreach (string f in files)
            DeleteFile(f, dir);
        foreach (string folder in folders.OrderByDescending(f => f.Length))
            RemoveDir(folder, dir);
        return !Directory.Exists(root);
    }

    /// <summary>Lists files and folders below <paramref name="root"/>; links are listed, never followed.</summary>
    static void Collect(string root, List<string> files, List<string> folders)
    {
        if (!Directory.Exists(root))
            return;
        folders.Add(root);
        if ((File.GetAttributes(root) & FileAttributes.ReparsePoint) != 0)
            return;
        var stack = new Stack<string>();
        stack.Push(root);
        while (stack.Count > 0)
        {
            string d = stack.Pop();
            files.AddRange(Directory.EnumerateFiles(d));
            foreach (string sub in Directory.EnumerateDirectories(d))
            {
                folders.Add(sub);
                if ((File.GetAttributes(sub) & FileAttributes.ReparsePoint) == 0)
                    stack.Push(sub);
            }
        }
    }

    static bool DeleteFile(string path, string dir)
    {
        if (!Paths.IsInside(path, dir) || Paths.Same(path, dir))
        {
            Log.Warn($"skipped {path}: outside the install folder");
            return true;
        }
        if (!File.Exists(path))
            return true;
        for (int attempt = 1; ; attempt++)
        {
            try
            {
                File.SetAttributes(path, FileAttributes.Normal);
                File.Delete(path);
                return true;
            }
            catch (Exception e) when (e is IOException or UnauthorizedAccessException)
            {
                if (attempt == 8)
                {
                    Log.Warn($"could not delete {path}: {e.Message}");
                    return false;
                }
                Thread.Sleep(250);
            }
        }
    }

    /// <summary>Removes a folder only if it is empty, or a link without following it (never recursive).</summary>
    static bool RemoveDir(string path, string dir)
    {
        try
        {
            if (!Directory.Exists(path))
                return true;
            if (!Paths.IsInside(path, dir))
                return false;
            bool link = (File.GetAttributes(path) & FileAttributes.ReparsePoint) != 0;
            if (!link && Directory.EnumerateFileSystemEntries(path).Any())
                return false;
            Directory.Delete(path, recursive: false);
            return true;
        }
        catch (Exception e)
        {
            Log.Warn($"could not remove folder {path}: {e.Message}");
            return false;
        }
    }

    /// <summary>
    /// uninstall.exe cannot delete itself while it runs: a hidden cmd waits for this process to exit, deletes
    /// the exe and then removes the folder if (and only if) it is empty ("rd" without /s). It also removes the native
    /// library cache the .NET host extracted for this uninstall.exe, renamed first so a cache still in use stays; only
    /// after the exe is gone, which Windows allows only once this process has fully exited (it still loads
    /// vcruntime140_cor3.dll from the cache while it unloads).
    /// The paths travel in environment variables, never in the command line, so "%", "^", "&amp;" or "!" in a folder
    /// name are not interpreted by cmd (it expands a variable once and does not rescan the value; /v:off keeps
    /// delayed expansion off).
    /// </summary>
    static void ScheduleSelfDelete(string exe, string dir)
    {
        const string script = "/v:off /d /c \"for /l %i in (1,1,20) do (ping -n 2 127.0.0.1 >nul & del /f /q \"%ARK_UNINSTALL_EXE%\" >nul 2>&1 & if not exist \"%ARK_UNINSTALL_EXE%\" ("
                              + "rd \"%ARK_UNINSTALL_DIR%\" >nul 2>&1 & if defined ARK_UNINSTALL_CACHE (move \"%ARK_UNINSTALL_CACHE%\" \"%ARK_UNINSTALL_CACHE%.stale\" >nul 2>&1 && rd /s /q \"%ARK_UNINSTALL_CACHE%.stale\" >nul 2>&1) & exit))\"";
        try
        {
            var psi = new ProcessStartInfo("cmd.exe", script)
            {
                CreateNoWindow = true,
                UseShellExecute = false,
                WindowStyle = ProcessWindowStyle.Hidden,
                WorkingDirectory = Path.GetTempPath(),
            };
            psi.Environment["ARK_UNINSTALL_EXE"] = exe;
            psi.Environment["ARK_UNINSTALL_DIR"] = dir;
            psi.Environment.Remove("ARK_UNINSTALL_CACHE");
            // %TEMP%\.net\uninstall\<bundle hash>: named after this exe, so only this build's own folder
            if (SetupTemp.ExtractionDir() is string cache && Path.GetFileName(Path.GetDirectoryName(cache) ?? "")
                    .Equals(Path.GetFileNameWithoutExtension(exe), StringComparison.OrdinalIgnoreCase))
                psi.Environment["ARK_UNINSTALL_CACHE"] = cache;
            Process.Start(psi)?.Dispose();
            Log.Info($"scheduled the removal of {ArkInfo.UninstallerName}");
        }
        catch (Exception e)
        {
            Log.Warn($"could not schedule the removal of {exe}: {e.Message}");
        }
    }
}
