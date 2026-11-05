using System.IO;
using System.Text;
using System.Text.Json;
using System.Text.Json.Nodes;

namespace ArkSetup.Core;

/// <summary>
/// &lt;dir&gt;\ark-setup.json: what this installer added beside ARK's own layout, so the uninstaller removes exactly
/// that: {"schema":1, "product":"ARK", "version", "code", "installed_at", "uninstall_key", "shortcuts":[...]}.
/// The runtime layout itself (versions, launcher, ARK.cmd, current.json, update.lock) belongs to Install-ARK.ps1.
/// </summary>
public sealed class SetupManifest
{
    public string Version { get; set; } = "";
    public string Code { get; set; } = "";
    public string InstalledAt { get; set; } = "";
    public string UninstallKey { get; set; } = ArkInfo.AppId;
    public List<string> Shortcuts { get; set; } = new();

    public static string PathIn(string dir) => Path.Combine(dir, ArkInfo.ManifestName);

    public static SetupManifest? Read(string dir)
    {
        string path = PathIn(dir);
        if (!File.Exists(path))
            return null;
        try
        {
            if (new FileInfo(path).Length > 64 * 1024)
                throw new InvalidDataException("too large");
            if (JsonNode.Parse(File.ReadAllText(path)) is not JsonObject node || node["schema"]?.GetValue<int>() != 1)
                throw new InvalidDataException("unsupported schema");
            var m = new SetupManifest
            {
                Version = node["version"]?.GetValue<string>() ?? "",
                Code = node["code"]?.GetValue<string>() ?? "",
                InstalledAt = node["installed_at"]?.GetValue<string>() ?? "",
            };
            string key = node["uninstall_key"]?.GetValue<string>() ?? "";
            if (ArkInfo.ValidKeyName(key))
                m.UninstallKey = key;
            else
                Log.Warn($"ignored uninstall key \"{key}\" in {path}");
            if (node["shortcuts"] is JsonArray links)
                foreach (var l in links)
                    if (l?.GetValue<string>() is string s && s.Length > 0)
                        m.Shortcuts.Add(s);
            return m;
        }
        catch (Exception e)
        {
            Log.Warn($"{path} is unreadable: {e.Message}");
            return new SetupManifest();
        }
    }

    public void Write(string dir)
    {
        var node = new JsonObject
        {
            ["schema"] = 1,
            ["product"] = ArkInfo.Product,
            ["version"] = Version,
            ["code"] = Code,
            ["installed_at"] = InstalledAt,
            ["uninstall_key"] = UninstallKey,
            ["shortcuts"] = new JsonArray(Shortcuts.Select(s => (JsonNode?)JsonValue.Create(s)).ToArray()),
        };
        string path = PathIn(dir);
        string temp = path + "." + Guid.NewGuid().ToString("N") + ".tmp";
        File.WriteAllText(temp, node.ToJsonString(new JsonSerializerOptions { WriteIndented = true }) + "\n", new UTF8Encoding(false));
        File.Move(temp, path, overwrite: true);
    }
}
