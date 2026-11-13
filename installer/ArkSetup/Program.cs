using System.IO;
using System.Windows;
using ArkSetup.Core;
using ArkSetup.Windows;

namespace ArkSetup;

/// <summary>
/// Entry point. Picks the mode from the command line (see <see cref="SetupArgs"/>): interactive install, silent
/// install or uninstall. The uninstaller is the same exe without the payload (uninstall.exe), so running
/// uninstall.exe with no arguments uninstalls the folder it sits in. There is no update mode: ARK updates itself.
/// </summary>
public static class Program
{
    /// <summary>Set by the windows before they close; returned as the process exit code.</summary>
    public static int ExitCode { get; set; } = ExitCodes.Ok;

    [STAThread]
    public static int Main(string[] argv)
    {
        var args = SetupArgs.Parse(argv);
        S.Force(args.Lang);
        ArkInfo.Isolate(args.TestIsolated);
        Log.MirrorPath = args.LogFile;
        Log.Start(argv);
        int code;
        try
        {
            code = Run(args);
        }
        catch (Exception e)
        {
            Log.Error("unhandled", e);
            code = ExitCodes.Failed;
        }
        Log.Info($"exit code {code}");
        return code;
    }

    static int Run(SetupArgs args)
    {
        // a silent run never waits on a dialog: the log and the exit code report everything
        bool dialogs = !args.NoDialogs;
        if (args.Errors.Count > 0)
        {
            Log.Error(string.Join("; ", args.Errors));
            if (dialogs)
                MessageBox.Show(string.Join("\n", args.Errors), S.WindowTitle, MessageBoxButton.OK, MessageBoxImage.Warning);
            return ExitCodes.BadArguments;
        }

        string exe = Environment.ProcessPath ?? throw new InvalidOperationException("no process path");
        string exeDir = Path.GetDirectoryName(exe)!;
        var payload = Payload.TryOpen(exe);
        Log.Info(payload == null ? "no payload (uninstaller mode)"
                 : $"payload at {payload.Offset}, {Paths.FormatSize(payload.Length)}{(payload.Signed ? ", setup signed" : "")}{(payload.StubSigned ? ", uninstaller signed" : "")}");
        Log.Info($"native libraries extracted to {SetupTemp.ExtractionDir() ?? "(none found)"}");

#if ARK_SETUP_SELFTEST
        if (args.Screenshot != null)
            return RunApp(app => Screens.Run(app, payload, args.Screenshot, args.ScreenshotScale, args.LayoutOnly, args.ScreenshotTimes));
#endif

        using var single = new Mutex(true, ArkInfo.Isolated ? $@"Local\MeshSphere.ArkSetup.Test.{ArkInfo.IsolationId}" : @"Local\MeshSphere.ArkSetup", out bool first);
        if (!first)
        {
            Log.Warn("another ARK Setup is already running");
            return ExitCodes.AlreadyRunning;
        }
        SetupTemp.CleanStale();

        // uninstall.exe (no payload, beside ark-setup.json) uninstalls; with /SILENT or /VERYSILENT quietly, as Inno's did
        bool uninstall = args.Uninstall
            || (payload == null && File.Exists(Path.Combine(exeDir, ArkInfo.ManifestName)));
        if (uninstall)
        {
            string dir = args.Dir ?? exeDir;
            if (args.Headless)
                return QuietUninstall(dir);
            return RunWindow(() => new UninstallWindow(dir));
        }

        if (payload == null)
        {
            Log.Error("this exe carries no ARK package");
            if (dialogs)
                MessageBox.Show(S.NoPayload, S.WindowTitle, MessageBoxButton.OK, MessageBoxImage.Error);
            return ExitCodes.NoPayload;
        }
        if (args.Silent)
            return SilentInstall(payload, args);
        return RunWindow(() => new SetupWindow(payload, args));
    }

    static App NewApp()
    {
        var app = new App { ShutdownMode = ShutdownMode.OnMainWindowClose };
        app.InitializeComponent();
        app.DispatcherUnhandledException += (_, e) =>
        {
            Log.Error("UI error", e.Exception);
            ExitCode = ExitCodes.Failed;
        };
        return app;
    }

    static int RunWindow(Func<Window> make)
    {
        var app = NewApp();
        app.Run(make());
        return ExitCode;
    }

    static int RunApp(Action<App> start)
    {
        var app = NewApp();
        app.ShutdownMode = ShutdownMode.OnExplicitShutdown;
        app.Startup += (_, _) => start(app);
        app.Run();
        return ExitCode;
    }

    static int SilentInstall(Payload payload, SetupArgs args)
    {
        string dir = args.Dir ?? UninstallEntry.Find()?.Dir ?? ArkInfo.DefaultInstallDir;
        try
        {
            InstallEngine.Install(payload, new InstallOptions
            {
                Dir = dir,
                DesktopShortcut = !args.NoDesktop,
                StartMenuShortcut = !args.NoStartMenu,
                AllowDowngrade = args.AllowDowngrade,
            }, null, CancellationToken.None);
            return ExitCodes.Ok;
        }
        catch (SetupException e)
        {
            Log.Error(e.Message, e);
            return e.ExitCode;
        }
        catch (Exception e)
        {
            Log.Error("install failed", e);
            return ExitCodes.Failed;
        }
    }

    static int QuietUninstall(string dir)
    {
        try
        {
            Uninstaller.Run(dir, null);
            return ExitCodes.Ok;
        }
        catch (SetupException e)
        {
            Log.Error(e.Message);
            return e.ExitCode;
        }
        catch (Exception e)
        {
            Log.Error("uninstall failed", e);
            return ExitCodes.Failed;
        }
    }
}
