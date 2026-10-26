using System.IO;
using System.Text;

namespace ArkSetup.Core;

/// <summary>
/// The setup exe and uninstall.exe are self-contained: they carry the Microsoft .NET and Windows Desktop runtimes
/// (MIT). Their license and third-party notices are embedded at build time (ArkSetup.csproj) and written beside
/// uninstall.exe as uninstall-notices.txt.
/// </summary>
public static class Notices
{
    public static string Text()
    {
        var asm = typeof(Notices).Assembly;
        var names = asm.GetManifestResourceNames().Where(n => n.StartsWith("notices/", StringComparison.Ordinal)).Order().ToList();
        var sb = new StringBuilder();
        sb.AppendLine("ARK Setup and uninstall.exe");
        sb.AppendLine();
        sb.AppendLine("ARK's installer is ARK code under the Apache License 2.0 (see LICENSE and NOTICE in");
        sb.AppendLine("versions\\<version>\\licenses). It is a self-contained .NET application and includes the");
        sb.AppendLine("Microsoft .NET runtime and the Windows Desktop runtime (WPF), which are distributed under");
        sb.AppendLine("the following terms.");
        foreach (string name in names)
        {
            using var s = asm.GetManifestResourceStream(name);
            if (s == null)
                continue;
            using var r = new StreamReader(s);
            sb.AppendLine();
            sb.AppendLine(new string('=', 78));
            sb.AppendLine(name["notices/".Length..].Replace('/', ' '));
            sb.AppendLine(new string('=', 78));
            sb.AppendLine(r.ReadToEnd().TrimEnd());
        }
        return sb.ToString();
    }

    public static void WriteTo(string dir)
    {
        string path = Path.Combine(dir, ArkInfo.NoticesName);
        File.WriteAllText(path, Text(), new UTF8Encoding(false));
    }
}
