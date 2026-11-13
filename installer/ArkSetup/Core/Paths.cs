using System.IO;

namespace ArkSetup.Core;

/// <summary>Path checks that keep Setup from ever writing or deleting outside the install folder.</summary>
public static class Paths
{
    /// <summary>Full path without a trailing separator.</summary>
    public static string Normalize(string path) =>
        Path.TrimEndingDirectorySeparator(Path.GetFullPath(path.Trim().Trim('"')));

    public static bool Same(string a, string b) => Normalize(a).Equals(Normalize(b), StringComparison.OrdinalIgnoreCase);

    /// <summary>True when <paramref name="path"/> is <paramref name="dir"/> itself or lies below it.</summary>
    public static bool IsInside(string path, string dir)
    {
        string p = Normalize(path);
        string d = Normalize(dir);
        return p.Equals(d, StringComparison.OrdinalIgnoreCase)
            || p.StartsWith(d + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase);
    }

    /// <summary>
    /// Folders Setup refuses to install into or uninstall from: drive roots, well-known shell folders, and the
    /// folders that hold ARK's user data and documents.
    /// </summary>
    public static string? Problem(string dir)
    {
        string d;
        try { d = Normalize(dir); }
        catch (Exception) { return "invalid path"; }
        if (!Path.IsPathFullyQualified(d))
            return "not an absolute path";
        if (d.StartsWith(@"\\", StringComparison.Ordinal))
            return "a network path";
        string? root = Path.GetPathRoot(d);
        if (root != null && Normalize(root).Equals(d, StringComparison.OrdinalIgnoreCase))
            return "a drive root";
        var special = new[]
        {
            Environment.SpecialFolder.UserProfile, Environment.SpecialFolder.Desktop,
            Environment.SpecialFolder.DesktopDirectory, Environment.SpecialFolder.MyDocuments,
            Environment.SpecialFolder.ApplicationData, Environment.SpecialFolder.LocalApplicationData,
            Environment.SpecialFolder.ProgramFiles, Environment.SpecialFolder.ProgramFilesX86,
            Environment.SpecialFolder.Windows, Environment.SpecialFolder.System, Environment.SpecialFolder.SystemX86,
            Environment.SpecialFolder.Programs, Environment.SpecialFolder.StartMenu, Environment.SpecialFolder.MyMusic,
            Environment.SpecialFolder.MyPictures, Environment.SpecialFolder.MyVideos, Environment.SpecialFolder.CommonApplicationData,
        };
        foreach (var f in special)
        {
            string s = Environment.GetFolderPath(f);
            if (s.Length > 0 && Same(s, d))
                return $"the {f} folder";
        }
        string downloads = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.UserProfile), "Downloads");
        string programs = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "Programs");
        foreach (string s in new[] { downloads, programs, Path.GetTempPath() })
            if (Same(s, d))
                return "a shared system folder";
        if (Same(ArkInfo.UserDataDir, d) || Same(ArkInfo.DocumentsDir, d))
            return "the folder of ARK's user data";
        if (IsInside(d, ArkInfo.DocumentsDir))
            return "inside ARK's documents folder";
        return null;
    }

    /// <summary>
    /// What the folder picker returns becomes an "ARK" subfolder unless it already is an ARK install root,
    /// so picking "D:\Apps" installs to "D:\Apps\ARK".
    /// </summary>
    public static string WithAppFolder(string picked)
    {
        string d = Normalize(picked);
        string name = Path.GetFileName(d);
        if (name.Equals(ArkInfo.Product, StringComparison.OrdinalIgnoreCase))
            return d;
        if (File.Exists(Path.Combine(d, "current.json")) || File.Exists(Path.Combine(d, ArkInfo.ManifestName)))
            return d;
        if (name.Equals("app", StringComparison.OrdinalIgnoreCase)
            && Path.GetFileName(Path.GetDirectoryName(d) ?? "").Equals(ArkInfo.Product, StringComparison.OrdinalIgnoreCase))
            return d;
        return Path.Combine(d, ArkInfo.Product);
    }

    public static string FormatSize(long bytes)
    {
        string[] units = { "B", "KB", "MB", "GB", "TB" };
        double v = bytes;
        int u = 0;
        while (v >= 1024 && u < units.Length - 1)
        {
            v /= 1024;
            u++;
        }
        return u == 0 ? $"{bytes} B" : $"{v:0.#} {units[u]}";
    }

    /// <summary>Free bytes on the drive holding <paramref name="path"/>; -1 when unknown.</summary>
    public static long FreeSpace(string path, out string drive)
    {
        drive = "";
        try
        {
            var info = new DriveInfo(Path.GetPathRoot(Normalize(path))!);
            drive = info.Name.TrimEnd('\\');
            return info.IsReady ? info.AvailableFreeSpace : -1;
        }
        catch (Exception e) when (e is IOException or ArgumentException or UnauthorizedAccessException)
        {
            return -1;
        }
    }

    /// <summary>Total size of the files below <paramref name="dir"/> (links are not followed).</summary>
    public static long SizeOf(string dir)
    {
        long total = 0;
        if (!Directory.Exists(dir))
            return 0;
        var stack = new Stack<string>();
        stack.Push(dir);
        while (stack.Count > 0)
        {
            string d = stack.Pop();
            try
            {
                foreach (string f in Directory.EnumerateFiles(d))
                    try { total += new FileInfo(f).Length; } catch (IOException) { }
                foreach (string sub in Directory.EnumerateDirectories(d))
                    if ((File.GetAttributes(sub) & FileAttributes.ReparsePoint) == 0)
                        stack.Push(sub);
            }
            catch (Exception e) when (e is IOException or UnauthorizedAccessException) { }
        }
        return total;
    }
}

/// <summary>
/// The disk space an install needs: the package expands into %TEMP% first, then its runtime is copied into the folder
/// and Setup adds uninstall.exe. The runtime is counted even when that version is already there: a Reinstall that finds
/// it damaged keeps the damaged copy aside until the new copy is verified. When %TEMP% is on the same drive as the
/// folder, that drive must hold both at once.
/// </summary>
public sealed record SpacePlan(string TargetDrive, long TargetNeed, long TargetFree, string TempDrive, long TempNeed, long TempFree)
{
    public const long Margin = 64L * 1024 * 1024;

    public bool SameDrive => TargetDrive.Equals(TempDrive, StringComparison.OrdinalIgnoreCase);

    /// <summary>What the folder's drive must hold, including the temporary files when they share it.</summary>
    public long TargetTotal => SameDrive ? TargetNeed + TempNeed : TargetNeed;

    public bool TargetShort => TargetFree >= 0 && TargetFree < TargetTotal + Margin;
    public bool TempShort => !SameDrive && TempFree >= 0 && TempFree < TempNeed + Margin;

    public static SpacePlan For(string dir, long expanded, long uninstaller)
    {
        long targetFree = Paths.FreeSpace(dir, out string target);
        long tempFree = Paths.FreeSpace(Path.GetTempPath(), out string temp);
        return new SpacePlan(target, expanded + uninstaller, targetFree, temp, expanded, tempFree);
    }

    /// <summary>The message for the first shortage, or null.</summary>
    public string? Problem() =>
        TempShort ? S.NotEnoughTempSpace(TempDrive, Paths.FormatSize(TempNeed + Margin))
        : TargetShort ? S.NotEnoughSpaceOn(TargetDrive, Paths.FormatSize(TargetTotal + Margin))
        : null;

    public override string ToString() =>
        $"space: {TargetDrive} needs {Paths.FormatSize(TargetTotal)} of {Paths.FormatSize(TargetFree)} free; temporary files on {TempDrive} need {Paths.FormatSize(TempNeed)}";
}
