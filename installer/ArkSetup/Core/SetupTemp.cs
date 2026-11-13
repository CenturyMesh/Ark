using System.IO;
using System.Text.RegularExpressions;

namespace ArkSetup.Core;

/// <summary>
/// Setup's own temporary folder %TEMP%\ark-setup-&lt;32 hex&gt;: it holds the extracted package while Install-ARK.ps1 runs,
/// and setup.lock stays open in it for the whole run. Folders of runs that were killed are removed by the next run:
/// a folder is renamed before it is deleted, and Windows refuses to rename a folder while any file in it is open, so
/// the folder of a live run (or one still in use by its PowerShell) is never touched.
/// </summary>
public sealed class SetupTemp : IDisposable
{
    const string Prefix = "ark-setup-";
    static readonly Regex Owned = new(@"^ark-setup-[0-9a-f]{32}(\.stale)?$", RegexOptions.CultureInvariant);
    // The .NET single-file host extracts WPF's native libraries to %TEMP%\.net\<exe name>\<bundle hash>.
    static readonly Regex SetupCache = new(@"^ARK-[0-9]{3}[A-G]-windows-x64-setup(-test-[A-Za-z0-9-]{1,48})?$", RegexOptions.CultureInvariant);

    public string Dir { get; }
    readonly FileStream _lock;

    SetupTemp(string dir, FileStream lockFile)
    {
        Dir = dir;
        _lock = lockFile;
    }

    public static SetupTemp Create()
    {
        string dir = Path.Combine(Path.GetTempPath(), Prefix + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(dir);
        var lockFile = new FileStream(Path.Combine(dir, "setup.lock"), FileMode.CreateNew, FileAccess.ReadWrite, FileShare.None);
        return new SetupTemp(dir, lockFile);
    }

    public void Dispose()
    {
        _lock.Dispose();
        try
        {
            if (Directory.Exists(Dir) && Paths.IsInside(Dir, Path.GetTempPath()) && Owned.IsMatch(Path.GetFileName(Dir)))
                Directory.Delete(Dir, recursive: true); // .NET removes links inside without following them
        }
        catch (Exception e)
        {
            Log.Warn($"could not remove the temporary folder {Dir}: {e.Message}");
        }
    }

    /// <summary>
    /// The folder the single-file host extracted this process's native libraries to (it lists it among the native
    /// library search folders); null when there is none or it is not under %TEMP%\.net.
    /// </summary>
    public static string? ExtractionDir()
    {
        string root = Path.Combine(Path.GetTempPath(), ".net");
        if (AppContext.GetData("NATIVE_DLL_SEARCH_DIRECTORIES") is string dirs)
            foreach (string d in dirs.Split(Path.PathSeparator, StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries))
                if (Path.IsPathFullyQualified(d) && Paths.IsInside(d, root) && !Paths.Same(d, root) && Directory.Exists(d))
                    return Paths.Normalize(d);
        return null;
    }

    /// <summary>
    /// Removes what killed Setup runs left in %TEMP% (their ark-setup-&lt;hex&gt; folders) and the native library caches of
    /// other ARK setup builds under %TEMP%\.net. Called while holding the single-instance mutex; best effort.
    /// A run never removes its own cache: renaming it can succeed while the process still exits (the libraries are
    /// loaded lazily, and WPF's C++/CLI module calls into vcruntime140_cor3.dll while it unloads), so the cache of the
    /// last Setup run stays until the next Setup or uninstall run.
    /// </summary>
    public static void CleanStale()
    {
        string temp = Path.GetTempPath();
        try
        {
            foreach (string d in Directory.EnumerateDirectories(temp, Prefix + "*"))
                if (Owned.IsMatch(Path.GetFileName(d)))
                    RemoveUnused(d);
            string caches = Path.Combine(temp, ".net");
            if (!Directory.Exists(caches))
                return;
            string? own = ExtractionDir();
            foreach (string app in Directory.EnumerateDirectories(caches))
            {
                if (!SetupCache.IsMatch(Path.GetFileName(app)) || (File.GetAttributes(app) & FileAttributes.ReparsePoint) != 0)
                    continue;
                if (OtherProcessNamed(Path.GetFileName(app)))
                {
                    Log.Info($"kept {app}: a setup of that name is running");
                    continue;
                }
                foreach (string build in Directory.EnumerateDirectories(app))
                    if (own == null || !Paths.Same(build, own))
                        RemoveUnused(build);
                if (!Directory.EnumerateFileSystemEntries(app).Any())
                    try { Directory.Delete(app); } catch (IOException) { }
            }
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
            Log.Warn($"could not clean old temporary folders: {e.Message}");
        }
    }

    /// <summary>
    /// True while another process runs an exe of that name: it may not have loaded its libraries yet, so its cache
    /// could be renamed under it.
    /// </summary>
    static bool OtherProcessNamed(string name)
    {
        var processes = System.Diagnostics.Process.GetProcessesByName(name);
        try { return processes.Any(p => p.Id != Environment.ProcessId); }
        finally { foreach (var p in processes) p.Dispose(); }
    }

    /// <summary>Deletes a folder only after renaming it, which fails while anything in it is open.</summary>
    static void RemoveUnused(string dir)
    {
        string stale = dir.EndsWith(".stale", StringComparison.Ordinal) ? dir : dir + ".stale";
        try
        {
            if (stale != dir)
                Directory.Move(dir, stale);
            if ((File.GetAttributes(stale) & FileAttributes.ReparsePoint) != 0)
                Directory.Delete(stale, recursive: false);
            else
                Directory.Delete(stale, recursive: true);
            Log.Info($"removed the leftover {dir}");
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
            Log.Info($"kept {dir}: {e.Message}");
        }
    }
}

/// <summary>
/// &lt;dir&gt;\update.lock with the semantics of Enter-ArkUpdateLock (updates\Ark.Update.psm1): whoever holds it open
/// exclusively owns the install folder, and ARK's launcher and updater fail at once while someone else holds it.
/// </summary>
public static class UpdateLock
{
    public static string PathIn(string dir) => Path.Combine(dir, "update.lock");

    /// <summary>
    /// Takes the lock for an uninstall. The handle shares nothing but deletion, so the uninstall can delete update.lock
    /// itself while launcher and updater keep failing until the handle is closed.
    /// </summary>
    public static FileStream Enter(string dir)
    {
        try
        {
            var lockFile = new FileStream(PathIn(dir), FileMode.OpenOrCreate, FileAccess.ReadWrite, FileShare.Delete);
            Log.Info($"holding {PathIn(dir)}");
            return lockFile;
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
            Log.Warn($"{PathIn(dir)} is held: {e.Message}");
            throw new SetupException(S.UpdateInProgress, ExitCodes.ArkRunning, e);
        }
    }

    /// <summary>Install: fails early while a launch or update holds the lock (Install-ARK.ps1 then takes it itself).</summary>
    public static void Probe(string dir)
    {
        string path = PathIn(dir);
        if (!File.Exists(path))
            return;
        try
        {
            using var probe = new FileStream(path, FileMode.Open, FileAccess.ReadWrite, FileShare.None);
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
            Log.Warn($"{path} is held: {e.Message}");
            throw new SetupException(S.UpdateInProgress, ExitCodes.ArkRunning, e);
        }
    }
}
