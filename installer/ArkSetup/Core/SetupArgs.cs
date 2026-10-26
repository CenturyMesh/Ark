using System.Globalization;

namespace ArkSetup.Core;

/// <summary>
/// Command line (also accepts /name for --name):
///   (none)                                             interactive install
///   --silent [--dir "X"] [--no-desktop] [--no-startmenu]   install without UI
///   --uninstall [--dir "X"] [--quiet]                  uninstall (the default when run as uninstall.exe)
///   --test-isolated ID                                 private test: own uninstall key, no shortcuts
///   --lang en|pt                                       force the UI language
/// -SelfTest builds only:
///   --screenshot DIR [--screenshot-scale N] [--layout-only]   render every page to PNG and check its layout
/// There is no update mode: ARK updates itself in the app.
/// </summary>
public sealed class SetupArgs
{
    public bool Silent { get; private set; }
    public bool Uninstall { get; private set; }
    public bool Quiet { get; private set; }
    public bool NoDesktop { get; private set; }
    public bool NoStartMenu { get; private set; }
    public string? Dir { get; private set; }
    public string? Lang { get; private set; }
    public string? TestIsolated { get; private set; }
#if ARK_SETUP_SELFTEST
    public string? Screenshot { get; private set; }
    public double ScreenshotScale { get; private set; } = 1.0;
    public bool LayoutOnly { get; private set; }
#endif
    public List<string> Errors { get; } = new();

    public static SetupArgs Parse(string[] args)
    {
        var a = new SetupArgs();
        for (int i = 0; i < args.Length; i++)
        {
            string key = args[i].Trim();
            if (key.StartsWith('/'))
                key = "--" + key[1..];
            string? Next() => i + 1 < args.Length ? args[++i] : null;
            switch (key.ToLowerInvariant())
            {
                case "--silent": case "-s": a.Silent = true; break;
                case "--uninstall": a.Uninstall = true; break;
                case "--quiet": case "-q": a.Quiet = true; break;
                case "--no-desktop": a.NoDesktop = true; break;
                case "--no-startmenu": a.NoStartMenu = true; break;
                case "--dir":
                    a.Dir = Next()?.Trim().Trim('"');
                    if (string.IsNullOrWhiteSpace(a.Dir)) a.Errors.Add("--dir needs a folder");
                    break;
                case "--lang":
                    a.Lang = Next()?.ToLowerInvariant();
                    if (a.Lang is not ("en" or "pt")) a.Errors.Add("--lang needs en or pt");
                    break;
                case "--test-isolated":
                    a.TestIsolated = Next();
                    if (a.TestIsolated == null || !ArkInfo.ValidIsolationId(a.TestIsolated)) a.Errors.Add("--test-isolated needs an id matching [A-Za-z0-9-]{1,48}");
                    break;
#if ARK_SETUP_SELFTEST
                case "--screenshot":
                    a.Screenshot = Next();
                    if (string.IsNullOrWhiteSpace(a.Screenshot)) a.Errors.Add("--screenshot needs a folder");
                    break;
                case "--screenshot-scale":
                    if (!double.TryParse(Next(), NumberStyles.Float, CultureInfo.InvariantCulture, out double scale) || scale < 0.5 || scale > 4)
                        a.Errors.Add("--screenshot-scale needs a number between 0.5 and 4");
                    else a.ScreenshotScale = scale;
                    break;
                case "--layout-only": a.LayoutOnly = true; break;
#endif
                default:
                    a.Errors.Add($"unknown argument {args[i]}");
                    break;
            }
        }
        return a;
    }
}
