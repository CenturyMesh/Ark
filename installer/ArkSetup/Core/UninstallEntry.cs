using System.IO;
using System.Text.Json.Nodes;
using System.Text.RegularExpressions;
using Microsoft.Win32;

namespace ArkSetup.Core;

/// <summary>An ARK install found on this machine.</summary>
public sealed record ExistingInstall(string Dir, string? Version, bool FromInno);

/// <summary>
/// The "Installed apps" entry: HKCU\Software\Microsoft\Windows\CurrentVersion\Uninstall\MeshSphere.ARK (per user,
/// no administrator rights). It replaces the Inno Setup entry MeshSphere.ARK_is1 for the same folder, so Windows
/// never lists two ARKs.
/// </summary>
public static class UninstallEntry
{
    const string Root = @"Software\Microsoft\Windows\CurrentVersion\Uninstall";

    public static string KeyPath(string keyName) => $@"{Root}\{keyName}";

    public static void Write(string dir, string version, long sizeBytes)
    {
        string keyPath = KeyPath(ArkInfo.UninstallKeyName);
        string uninstaller = Path.Combine(dir, ArkInfo.UninstallerName);
        string isolation = ArkInfo.Isolated ? $" --test-isolated {ArkInfo.IsolationId}" : "";
        using var key = Registry.CurrentUser.CreateSubKey(keyPath, writable: true)
                        ?? throw new IOException($"could not create HKCU\\{keyPath}");
        key.SetValue("DisplayName", ArkInfo.Product);
        key.SetValue("DisplayVersion", version);
        key.SetValue("Publisher", ArkInfo.Publisher);
        key.SetValue("InstallLocation", dir);
        key.SetValue("DisplayIcon", $"{Path.Combine(dir, "versions", version, ArkInfo.StudioExe)},0");
        key.SetValue("UninstallString", $"\"{uninstaller}\" --uninstall{isolation}");
        key.SetValue("QuietUninstallString", $"\"{uninstaller}\" --uninstall --quiet{isolation}");
        key.SetValue("InstallDate", DateTime.Now.ToString("yyyyMMdd"));
        key.SetValue("EstimatedSize", (int)Math.Min(int.MaxValue, sizeBytes / 1024), RegistryValueKind.DWord);
        key.SetValue("NoModify", 1, RegistryValueKind.DWord);
        key.SetValue("NoRepair", 1, RegistryValueKind.DWord);
        key.SetValue("Comments", ArkInfo.Title);
        Log.Info($"registered HKCU\\{keyPath} -> {dir}");
    }

    static string? Location(string keyName)
    {
        using var key = Registry.CurrentUser.OpenSubKey(KeyPath(keyName));
        string? location = key?.GetValue("InstallLocation") as string ?? key?.GetValue("Inno Setup: App Path") as string;
        return string.IsNullOrWhiteSpace(location) ? null : location;
    }

    /// <summary>
    /// The folder to offer: this installer's own entry, else the Inno Setup entry it replaces (Inno's
    /// UsePreviousAppDir), else nothing.
    /// </summary>
    public static ExistingInstall? Find()
    {
        try
        {
            foreach (var (name, inno) in new[] { (ArkInfo.UninstallKeyName, false), (ArkInfo.LegacyKeyName, true) })
            {
                string? location = Location(name);
                if (location == null || Paths.Problem(location) != null)
                    continue;
                string dir = Paths.Normalize(location);
                if (!inno && !File.Exists(Path.Combine(dir, "current.json")))
                    continue;
                return new ExistingInstall(dir, InstalledVersion(dir), inno);
            }
        }
        catch (Exception e)
        {
            Log.Warn($"could not read the existing install: {e.Message}");
        }
        return null;
    }

    /// <summary>The version the launcher will start: current.json "pending" if set, else "current".</summary>
    public static string? InstalledVersion(string dir)
    {
        try
        {
            string path = Path.Combine(dir, "current.json");
            if (!File.Exists(path) || new FileInfo(path).Length > 4096)
                return null;
            var node = JsonNode.Parse(File.ReadAllText(path)) as JsonObject;
            string? pending = node?["pending"]?.GetValue<string>();
            string? current = node?["current"]?.GetValue<string>();
            string? v = string.IsNullOrEmpty(pending) ? current : pending;
            return v != null && Regex.IsMatch(v, @"^[0-9]+\.[0-9]+\.[0-9]+$") ? v : null;
        }
        catch (Exception e)
        {
            Log.Warn($"could not read current.json in {dir}: {e.Message}");
            return null;
        }
    }

    /// <summary>
    /// After installing into the folder of an Inno Setup install: removes Inno's uninstaller (unins000.exe/.dat/.msg)
    /// and its entry, because this installer's entry now owns that folder.
    /// </summary>
    public static void ReplaceInno(string dir)
    {
        string keyPath = KeyPath(ArkInfo.LegacyKeyName);
        string? location = Location(ArkInfo.LegacyKeyName);
        if (location == null)
            return;
        if (!Paths.Same(location, dir))
        {
            Log.Info($"kept HKCU\\{keyPath}: it belongs to {location}");
            return;
        }
        string? uninstallString;
        using (var key = Registry.CurrentUser.OpenSubKey(keyPath))
            uninstallString = key?.GetValue("UninstallString") as string;
        var m = Regex.Match(uninstallString ?? "", "^\"?([^\"]+?\\\\unins[0-9]{3}\\.exe)\"?");
        if (m.Success && Paths.IsInside(m.Groups[1].Value, dir))
        {
            string exe = Paths.Normalize(m.Groups[1].Value);
            foreach (string ext in new[] { ".exe", ".dat", ".msg" })
            {
                string file = Path.ChangeExtension(exe, ext);
                try
                {
                    if (File.Exists(file))
                    {
                        File.SetAttributes(file, FileAttributes.Normal);
                        File.Delete(file);
                        Log.Info($"removed Inno Setup file {file}");
                    }
                }
                catch (Exception e) { Log.Warn($"could not remove {file}: {e.Message}"); }
            }
        }
        Registry.CurrentUser.DeleteSubKeyTree(keyPath, throwOnMissingSubKey: false);
        Log.Info($"replaced the Inno Setup entry HKCU\\{keyPath}");
    }

    /// <summary>Removes the entry <paramref name="keyName"/>, but only if it belongs to the install in <paramref name="dir"/>.</summary>
    public static void RemoveFor(string dir, string keyName)
    {
        string keyPath = KeyPath(keyName);
        using (var probe = Registry.CurrentUser.OpenSubKey(keyPath))
        {
            if (probe == null)
                return;
        }
        string? location = Location(keyName);
        if (location != null && !Paths.Same(location, dir))
        {
            Log.Info($"kept HKCU\\{keyPath}: it belongs to {location}");
            return;
        }
        Registry.CurrentUser.DeleteSubKeyTree(keyPath, throwOnMissingSubKey: false);
        Log.Info($"removed HKCU\\{keyPath}");
    }
}
