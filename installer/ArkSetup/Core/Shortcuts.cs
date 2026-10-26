using System.IO;
using System.Runtime.InteropServices;
using System.Runtime.InteropServices.ComTypes;
using System.Text;

namespace ArkSetup.Core;

/// <summary>
/// ARK's shortcuts, through the shell's IShellLink (no WScript dependency). They launch exactly like the Inno
/// installer's: powershell.exe running &lt;dir&gt;\launcher\ark-launch.ps1 -InstallRoot &lt;dir&gt;, working folder
/// Documents, icon from versions\&lt;version&gt;\ark-studio.exe.
/// </summary>
public static class Shortcuts
{
    public static string DesktopPath =>
        Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.DesktopDirectory), ArkInfo.ShortcutName);

    /// <summary>Per-user Start menu, the same place as Inno's {group}\ARK: Programs\ARK\ARK.lnk.</summary>
    public static string StartMenuPath =>
        Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.Programs), ArkInfo.StartMenuGroup, ArkInfo.ShortcutName);

    public static string LauncherScript(string dir) => Path.Combine(dir, "launcher", "ark-launch.ps1");

    /// <summary>The launcher arguments, identical in the shortcuts and in "Open ARK".</summary>
    public static string LauncherArguments(string dir) =>
        $"-NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File \"{LauncherScript(dir)}\" -InstallRoot \"{dir}\"";

    public static string WorkingDirectory => Environment.GetFolderPath(Environment.SpecialFolder.MyDocuments);

    public static void CreateLauncher(string lnkPath, string dir, string version)
    {
        string icon = Path.Combine(dir, "versions", version, ArkInfo.StudioExe);
        var link = (IShellLinkW)new CShellLink();
        try
        {
            link.SetPath(ArkInfo.PowerShellPath);
            link.SetArguments(LauncherArguments(dir));
            link.SetWorkingDirectory(WorkingDirectory);
            link.SetDescription($"{ArkInfo.Product} · {ArkInfo.Publisher}");
            link.SetIconLocation(icon, 0);
            link.SetShowCmd(7); // SW_SHOWMINNOACTIVE: no console flash before Studio opens
            Directory.CreateDirectory(Path.GetDirectoryName(lnkPath)!);
            ((IPersistFile)link).Save(lnkPath, true);
        }
        finally
        {
            Marshal.FinalReleaseComObject(link);
        }
    }

    /// <summary>True when the .lnk launches the ARK installed in <paramref name="dir"/>.</summary>
    public static bool PointsTo(string lnkPath, string dir)
    {
        if (!File.Exists(lnkPath))
            return false;
        var link = (IShellLinkW)new CShellLink();
        try
        {
            ((IPersistFile)link).Load(lnkPath, 0);
            var sb = new StringBuilder(4096);
            link.GetArguments(sb, sb.Capacity);
            string args = Environment.ExpandEnvironmentVariables(sb.ToString());
            string script = LauncherScript(Paths.Normalize(dir));
            return args.Contains($"\"{script}\"", StringComparison.OrdinalIgnoreCase);
        }
        catch (Exception e)
        {
            Log.Warn($"could not read shortcut {lnkPath}: {e.Message}");
            return false;
        }
        finally
        {
            Marshal.FinalReleaseComObject(link);
        }
    }

    /// <summary>
    /// Deletes ARK shortcuts that launch the install in <paramref name="dir"/>: the ones recorded in ark-setup.json
    /// and, unless isolated, the standard desktop and Start menu places. Shortcuts to another install are kept.
    /// </summary>
    public static void RemoveFor(string dir, IEnumerable<string> recorded, bool includeStandard)
    {
        var candidates = new List<string>(recorded.Where(p => p.EndsWith(".lnk", StringComparison.OrdinalIgnoreCase)));
        if (includeStandard)
            candidates.AddRange(new[] { DesktopPath, StartMenuPath });
        foreach (string lnk in candidates.Distinct(StringComparer.OrdinalIgnoreCase))
        {
            if (!File.Exists(lnk))
                continue;
            if (!PointsTo(lnk, dir))
            {
                Log.Info($"kept shortcut {lnk}: it does not launch {dir}");
                continue;
            }
            try
            {
                File.Delete(lnk);
                Log.Info($"removed shortcut {lnk}");
                string? folder = Path.GetDirectoryName(lnk);
                if (folder != null && Path.GetFileName(folder).Equals(ArkInfo.StartMenuGroup, StringComparison.OrdinalIgnoreCase)
                    && !Directory.EnumerateFileSystemEntries(folder).Any())
                    Directory.Delete(folder);
            }
            catch (Exception e)
            {
                Log.Warn($"could not remove shortcut {lnk}: {e.Message}");
            }
        }
    }

    [ComImport, Guid("00021401-0000-0000-C000-000000000046")]
    class CShellLink { }

    [ComImport, InterfaceType(ComInterfaceType.InterfaceIsIUnknown), Guid("000214F9-0000-0000-C000-000000000046")]
    interface IShellLinkW
    {
        void GetPath([Out, MarshalAs(UnmanagedType.LPWStr)] StringBuilder pszFile, int cch, IntPtr pfd, uint fFlags);
        void GetIDList(out IntPtr ppidl);
        void SetIDList(IntPtr pidl);
        void GetDescription([Out, MarshalAs(UnmanagedType.LPWStr)] StringBuilder pszName, int cch);
        void SetDescription([MarshalAs(UnmanagedType.LPWStr)] string pszName);
        void GetWorkingDirectory([Out, MarshalAs(UnmanagedType.LPWStr)] StringBuilder pszDir, int cch);
        void SetWorkingDirectory([MarshalAs(UnmanagedType.LPWStr)] string pszDir);
        void GetArguments([Out, MarshalAs(UnmanagedType.LPWStr)] StringBuilder pszArgs, int cch);
        void SetArguments([MarshalAs(UnmanagedType.LPWStr)] string pszArgs);
        void GetHotkey(out short pwHotkey);
        void SetHotkey(short wHotkey);
        void GetShowCmd(out int piShowCmd);
        void SetShowCmd(int iShowCmd);
        void GetIconLocation([Out, MarshalAs(UnmanagedType.LPWStr)] StringBuilder pszIconPath, int cch, out int piIcon);
        void SetIconLocation([MarshalAs(UnmanagedType.LPWStr)] string pszIconPath, int iIcon);
        void SetRelativePath([MarshalAs(UnmanagedType.LPWStr)] string pszPathRel, uint dwReserved);
        void Resolve(IntPtr hwnd, uint fFlags);
        void SetPath([MarshalAs(UnmanagedType.LPWStr)] string pszFile);
    }
}
