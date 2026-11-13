using System.Globalization;
using System.IO;

namespace ArkSetup.Core;

/// <summary>
/// Command line (also accepts /name for --name, and --name=value):
///   (none)                                             interactive install
///   --silent [--dir "X"] [--no-desktop] [--no-startmenu] [--allow-downgrade]
///                                                      install without UI
///   --uninstall [--dir "X"] [--quiet]                  uninstall (the default when run as uninstall.exe)
///   --allow-downgrade                                  install and select this version although the folder holds a newer one
///   --test-isolated ID                                 private test: own uninstall key, no shortcuts
///   --lang en|pt                                       force the UI language
///   --log=FILE                                         also write the log to FILE
/// The Inno Setup switches that scripts used with ARK's old installer keep working: /SILENT and /VERYSILENT (both
/// install without UI; uninstall.exe uninstalls quietly with them), /SUPPRESSMSGBOXES, /DIR=X, /NOICONS (no Start menu
/// shortcut), /LANG=english|portuguese, /LOG[=FILE] and /CURRENTUSER, plus /NORESTART, /SP-, /NOCANCEL,
/// /CLOSEAPPLICATIONS, /NOCLOSEAPPLICATIONS, /RESTARTAPPLICATIONS and /NORESTARTAPPLICATIONS, which need nothing:
/// Setup never restarts Windows and never closes ARK (it refuses to install while ARK runs). /ALLUSERS is refused: ARK
/// installs per user. With any silent or message box switch Setup shows no dialog: errors go to the log and the exit code.
/// -SelfTest builds only:
///   --screenshot DIR [--screenshot-scale N] [--screenshot-times T1,T2,...] [--layout-only]
///                                                      render every page to PNG and check its layout; the times
///                                                      also capture Welcome and Installing with the sphere posed there
/// There is no update mode: ARK updates itself in the app.
/// </summary>
public sealed class SetupArgs
{
    public bool Silent { get; private set; }
    public bool Uninstall { get; private set; }
    public bool Quiet { get; private set; }
    public bool SuppressMessages { get; private set; }
    public bool NoDesktop { get; private set; }
    public bool NoStartMenu { get; private set; }
    public bool AllowDowngrade { get; private set; }
    public string? Dir { get; private set; }
    public string? Lang { get; private set; }
    public string? LogFile { get; private set; }
    public string? TestIsolated { get; private set; }
#if ARK_SETUP_SELFTEST
    public string? Screenshot { get; private set; }
    public double ScreenshotScale { get; private set; } = 1.0;
    /// <summary>Phases (seconds of calm wandering) at which Welcome and Installing are also captured.</summary>
    public List<double> ScreenshotTimes { get; } = new();
    public bool LayoutOnly { get; private set; }
#endif
    public List<string> Errors { get; } = new();

    /// <summary>No window at all: install or uninstall without UI.</summary>
    public bool Headless => Silent || Quiet;

    /// <summary>
    /// No modal dialog either: also with /SUPPRESSMSGBOXES, or when a silent switch was swallowed into a --dir value
    /// by a trailing backslash before its closing quote.
    /// </summary>
    public bool NoDialogs => Headless || SuppressMessages || _silentInValue;
    bool _silentInValue;

    static readonly string[] SilentSwitches = { "--silent", "-s", "/silent", "/verysilent", "--verysilent", "--quiet", "-q", "/suppressmsgboxes", "--suppressmsgboxes" };
    static readonly string[] NoOps = { "--norestart", "--sp-", "--nocancel", "--closeapplications", "--nocloseapplications", "--restartapplications", "--norestartapplications", "--currentuser" };

    public static SetupArgs Parse(string[] args)
    {
        var a = new SetupArgs();
        for (int i = 0; i < args.Length; i++)
        {
            string key = args[i].Trim();
            if (key.StartsWith('/'))
                key = "--" + key[1..];
            string? inline = null;
            int eq = key.IndexOf('=');
            if (key.StartsWith("--", StringComparison.Ordinal) && eq > 2)
            {
                inline = key[(eq + 1)..];
                key = key[..eq];
            }
            string? Value() => inline ?? (i + 1 < args.Length ? args[++i] : null);
            string lower = key.ToLowerInvariant();
            if (inline != null && lower is not ("--dir" or "--lang" or "--log" or "--test-isolated"
#if ARK_SETUP_SELFTEST
                or "--screenshot" or "--screenshot-scale" or "--screenshot-times"
#endif
                ))
            {
                a.Errors.Add($"{key} takes no value ({args[i]})");
                continue;
            }
            if (NoOps.Contains(lower))
                continue; // see the summary: nothing to do
            switch (lower)
            {
                case "--silent": case "-s": case "--verysilent": a.Silent = true; break;
                case "--uninstall": a.Uninstall = true; break;
                case "--quiet": case "-q": a.Quiet = true; break;
                case "--suppressmsgboxes": a.SuppressMessages = true; break;
                case "--no-desktop": a.NoDesktop = true; break;
                case "--no-startmenu": case "--noicons": a.NoStartMenu = true; break;
                case "--allow-downgrade": a.AllowDowngrade = true; break;
                case "--allusers": a.Errors.Add("ARK installs for the current user only (/ALLUSERS is not supported)"); break;
                case "--dir": a.SetDir(Value()); break;
                case "--lang":
                    a.Lang = (Value() ?? "").Trim().ToLowerInvariant() switch
                    {
                        "en" or "english" => "en",
                        "pt" or "pt-pt" or "pt-br" or "portuguese" or "brazilianportuguese" => "pt",
                        _ => null,
                    };
                    if (a.Lang == null) a.Errors.Add("--lang needs en or pt");
                    break;
                case "--log":
                    if (inline == null)
                        break; // /LOG: the log is always written to %TEMP%\ARK-Setup.log
                    string log = inline.Trim().Trim('"');
                    if (log.Length > 0 && !Path.IsPathRooted(log) && log.IndexOfAny(Path.GetInvalidPathChars()) < 0)
                        log = Path.Combine(Environment.CurrentDirectory, log); // relative to the current folder, as with Inno
                    if (PathError(log) is string bad) a.Errors.Add($"--log={log}: {bad}");
                    else a.LogFile = log;
                    break;
                case "--test-isolated":
                    a.TestIsolated = Value();
                    if (a.TestIsolated == null || !ArkInfo.ValidIsolationId(a.TestIsolated)) a.Errors.Add("--test-isolated needs an id matching [A-Za-z0-9-]{1,48}");
                    break;
#if ARK_SETUP_SELFTEST
                case "--screenshot":
                    a.Screenshot = Value();
                    if (string.IsNullOrWhiteSpace(a.Screenshot)) a.Errors.Add("--screenshot needs a folder");
                    break;
                case "--screenshot-scale":
                    if (!double.TryParse(Value(), NumberStyles.Float, CultureInfo.InvariantCulture, out double scale) || scale < 0.5 || scale > 4)
                        a.Errors.Add("--screenshot-scale needs a number between 0.5 and 4");
                    else a.ScreenshotScale = scale;
                    break;
                case "--screenshot-times":
                    foreach (string part in (Value() ?? "").Split(',', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries))
                    {
                        if (double.TryParse(part, NumberStyles.Float, CultureInfo.InvariantCulture, out double t) && t >= 0 && t < 1e6)
                            a.ScreenshotTimes.Add(t);
                        else
                            a.Errors.Add("--screenshot-times needs seconds, e.g. 0,12.5,40");
                    }
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

    void SetDir(string? raw)
    {
        string v = (raw ?? "").Trim().Trim('"').Trim();
        if (v.Length == 0)
        {
            Errors.Add("--dir needs a folder");
            return;
        }
        if (v.Contains('"'))
        {
            // "C:\Apps\" makes the backslash escape the quote, and Windows joins the next arguments into this one
            foreach (string token in v.Split(' ', StringSplitOptions.RemoveEmptyEntries))
                if (SilentSwitches.Contains(token.Trim('"').ToLowerInvariant()))
                    _silentInValue = true;
            Errors.Add($"--dir {v}: a backslash right before a closing quote (\\\") escapes the quote and joins the following arguments into the folder; leave out that trailing backslash");
            return;
        }
        if (PathError(v) is string bad)
            Errors.Add($"--dir {v}: {bad}");
        else
            Dir = v;
    }

    /// <summary>Why <paramref name="path"/> is not a usable absolute path, or null.</summary>
    static string? PathError(string path)
    {
        if (path.IndexOfAny(Path.GetInvalidPathChars()) >= 0 || path.IndexOfAny(new[] { '*', '?', '<', '>', '|', '"' }) >= 0
            || (path.Length > 2 && path.IndexOf(':', 2) >= 0) || path.StartsWith(@"\\?\", StringComparison.Ordinal))
            return "it has characters a folder name cannot contain";
        if (!Path.IsPathFullyQualified(path))
            return @"it needs a full path such as C:\Apps\ARK";
        try { Path.GetFullPath(path); }
        catch (Exception e) when (e is ArgumentException or NotSupportedException or PathTooLongException) { return "it is not a valid path"; }
        return null;
    }
}
