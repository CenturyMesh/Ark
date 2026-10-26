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
