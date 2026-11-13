using System.IO;
using System.Reflection;
using System.Text.RegularExpressions;

namespace ArkSetup.Core;

/// <summary>Fixed facts about ARK and this build of the installer.</summary>
public static class ArkInfo
{
    public const string Product = "ARK";
    public const string Publisher = "MeshSphere Software";
    /// <summary>Key under HKCU\Software\Microsoft\Windows\CurrentVersion\Uninstall (Inno used AppId + "_is1").</summary>
    public const string AppId = "MeshSphere.ARK";
    public const string UninstallerName = "uninstall.exe";
    /// <summary>What this installer wrote: version, uninstall key and shortcuts (read by the uninstaller).</summary>
    public const string ManifestName = "ark-setup.json";
    /// <summary>License notices of the .NET runtime bundled in uninstall.exe.</summary>
    public const string NoticesName = "uninstall-notices.txt";
    public const string ShortcutName = "ARK.lnk";
    public const string StartMenuGroup = "ARK";
    public const string StudioExe = "ark-studio.exe";
    /// <summary>The executables the packaged runtime runs from versions\&lt;version&gt;.</summary>
    public static readonly string[] RuntimeProcesses = { "ark-studio", "ark-mcp", "ark-cli", "ark-occt-worker" };

    /// <summary>Technical version ("0.9.65"), from release.json at build time.</summary>
    public static string Version { get; } = Metadata("ArkVersion") ?? "0.0.0";

    /// <summary>Iteration code ("064A"), from release.json at build time.</summary>
    public static string Code { get; } = Metadata("ArkCode") ?? "000A";

    /// <summary>"ARK 064A".</summary>
    public static string Title => $"{Product} {Code}";

    /// <summary>SHA-256 of the appended package (lower-case hex), stamped by build-installer.ps1; null in a bare build.</summary>
    public static string? PayloadSha256 { get; } =
        Metadata("ArkPayloadSha256") is string h && Regex.IsMatch(h, "^[0-9a-f]{64}$") ? h : null;

    /// <summary>
    /// Private test isolation: its own uninstall key (MeshSphere.ARK.Test.&lt;id&gt;) and no shortcuts, so a test never
    /// touches the user's real ARK registration. Stamped by build-installer.ps1 -TestIsolationId, or --test-isolated.
    /// </summary>
    public static string? IsolationId { get; private set; } = Metadata("ArkTestIsolationId");

    public static bool Isolated => IsolationId != null;

    public static void Isolate(string? id)
    {
        if (id != null)
            IsolationId = id;
    }

    public static string UninstallKeyName => Isolated ? $"{AppId}.Test.{IsolationId}" : AppId;

    /// <summary>The Inno Setup entry this installer replaces.</summary>
    public static string LegacyKeyName => UninstallKeyName + "_is1";

    public static bool ValidIsolationId(string id) => Regex.IsMatch(id, "^[A-Za-z0-9-]{1,48}$");

    public static bool ValidKeyName(string key) => Regex.IsMatch(key, @"^MeshSphere\.ARK(\.Test\.[A-Za-z0-9-]{1,48})?$");

    static string LocalAppData => Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData);

    /// <summary>
    /// Default per-user install root, the same as the Inno installer and Get-ArkInstallRoot. An isolated test never
    /// defaults to the real install: it uses a folder of its own under %TEMP%.
    /// </summary>
    public static string DefaultInstallDir => Isolated
        ? Path.Combine(Path.GetTempPath(), $"ark-setup-test-{IsolationId}", "app")
        : Path.Combine(LocalAppData, "ARK", "app");

    /// <summary>ARK's user data (memory, recovery, cache, studio.json). Never an install root and never touched.</summary>
    public static string UserDataDir => Path.Combine(LocalAppData, "ARK");

    /// <summary>The launcher's document workspace. Never an install root and never touched.</summary>
    public static string DocumentsDir => Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.MyDocuments), "ARK");

    public static string PowerShellPath =>
        Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.System), "WindowsPowerShell", "v1.0", "powershell.exe");

    static string? Metadata(string key)
    {
        foreach (var a in typeof(ArkInfo).Assembly.GetCustomAttributes<AssemblyMetadataAttribute>())
            if (a.Key == key && !string.IsNullOrWhiteSpace(a.Value))
                return a.Value;
        return null;
    }
}
