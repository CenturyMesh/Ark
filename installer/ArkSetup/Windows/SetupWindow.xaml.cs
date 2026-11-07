using System.Diagnostics;
using System.IO;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using System.Windows.Media.Animation;
using System.Windows.Threading;
using Microsoft.Win32;
using ArkSetup.Controls;
using ArkSetup.Core;

namespace ArkSetup.Windows;

/// <summary>The installer: Welcome → Options → Installing → Done (or Error).</summary>
public partial class SetupWindow : ChromeWindow
{
    readonly Payload? _payload;
    readonly CancellationTokenSource _cancel = new();
    readonly SmoothProgress _bar = new() { MinSeconds = 4.0 };
    InstallProgress? _latest;
    Panel _page;
    long _required = -1;
    string _installedDir = "";

    /// <param name="payload">The package inside this exe; null only when previewing pages.</param>
    public SetupWindow(Payload? payload)
    {
        _payload = payload;
        InitializeComponent();
        _page = WelcomePage;
        FillText();
        PickDefaultFolder();
        _bar.Changed += OnProgress;
        // the end of the path ("…\ARK\app") is the part that matters: keep it in view
        DirBox.IsVisibleChanged += (_, e) =>
        {
            if ((bool)e.NewValue)
                Dispatcher.BeginInvoke(ShowPathEnd, DispatcherPriority.ContextIdle);
        };
        Loaded += async (_, _) =>
        {
            if (!Preview)
                Anim.Reveal(WelcomePage, 160);
            if (_payload == null)
                return;
            // the size of ARK, from the zip directory (fast, but off the UI thread)
            _required = await Task.Run(() =>
            {
                try { return _payload.UncompressedSize(); }
                catch (Exception e)
                {
                    Log.Error("could not read the payload", e);
                    return -1L;
                }
            });
            UpdateSpace();
        };
    }

    void FillText()
    {
        string kicker = Spaced(ArkInfo.Title);
        TitleIcon.Source = Images.Icon(32);
        SetupLabel.Text = S.SetupLabel;
        MinButton.ToolTip = S.Minimize;
        CloseButton.ToolTip = S.Close;

        PublisherKicker.Text = Spaced(ArkInfo.Publisher);
        WelcomeLogo.Source = Images.Wordmark();
        TaglineText.Text = S.Tagline;
        BlurbText.Text = S.Blurb;
        VersionText.Text = $"{ArkInfo.Title}  ·  {ArkInfo.Version}";
        InstallButtonText.Text = S.Install;
        OptionsLinkText.Text = S.Options;

        OptionsKicker.Text = kicker;
        OptionsTitle.Text = S.OptionsTitle;
        OptionsSub.Text = S.OptionsSub;
        FolderLabel.Text = Spaced(S.Folder);
        BrowseText.Text = S.Browse;
        DesktopCheck.Content = S.DesktopShortcut;
        StartMenuCheck.Content = S.StartMenuShortcut;
        SpaceRequiredLabel.Text = S.SpaceRequired;
        SpaceAvailableLabel.Text = S.SpaceAvailable;
        DataNote.Text = S.DataNote;
        BackText.Text = S.Back;
        OptionsInstallText.Text = S.Install;
        if (ArkInfo.Isolated)
        {
            DesktopCheck.IsChecked = StartMenuCheck.IsChecked = false;
            DesktopCheck.IsEnabled = StartMenuCheck.IsEnabled = false;
        }

        InstallingKicker.Text = kicker;
        InstallingTitle.Text = S.Installing;
        StatusText.Text = S.Extracting;
        QuietText.Text = S.QuietLine;

        DoneTitle.Text = S.ReadyTitle;
        DoneBody.Text = S.InstalledLine(ArkInfo.Title);
        OpenArkText.Text = S.OpenArk;
        DoneCloseText.Text = S.Close;

        ErrorTitle.Text = S.FailedTitle;
        OpenLogText.Text = S.OpenLog;
        TryAgainText.Text = S.TryAgain;
        ErrorCloseText.Text = S.Close;
    }

    /// <summary>Default folder: the existing install (ours or Inno's) if there is one, else %LOCALAPPDATA%\ARK\app.</summary>
    void PickDefaultFolder()
    {
        var existing = UninstallEntry.Find();
        string dir = existing?.Dir ?? ArkInfo.DefaultInstallDir;
        string? installed = existing?.Version;
        DirBox.Text = dir;

        string action = S.Install;
        if (installed != null && Version.TryParse(installed, out var have) && Version.TryParse(ArkInfo.Version, out var ours))
            action = ours > have ? S.UpdateButton : ours == have ? S.Reinstall : S.Install;
        InstallButtonText.Text = action;
        OptionsInstallText.Text = action;
        InstallPathText.Text = installed != null ? $"{S.InstalledVersion(installed)}  ·  {dir}" : $"{S.InstallTo}  {dir}";
        InstallPathText.ToolTip = dir;
    }

    // --- navigation --------------------------------------------------------------------------------------

    void Go(Panel page)
    {
        if (page == _page)
            return;
        Anim.Swap(_page, page);
        _page = page;
        Art.Lively = page == InstallingPage; // the sphere livens up while ARK installs and calms down after
    }

    void Options_Click(object sender, RoutedEventArgs e) => Go(OptionsPage);

    void ShowPathEnd()
    {
        DirBox.CaretIndex = DirBox.Text.Length;
        DirBox.ScrollToHorizontalOffset(double.MaxValue);
    }

    void Back_Click(object sender, RoutedEventArgs e)
    {
        InstallPathText.Text = $"{S.InstallTo}  {DirBox.Text.Trim()}";
        InstallPathText.ToolTip = DirBox.Text.Trim();
        Go(WelcomePage);
    }

    void TryAgain_Click(object sender, RoutedEventArgs e) => Go(OptionsPage);

    // --- options -------------------------------------------------------------------------------------------

    void Browse_Click(object sender, RoutedEventArgs e)
    {
        var dialog = new OpenFolderDialog { Title = S.BrowseTitle, Multiselect = false };
        try
        {
            string current = DirBox.Text.Trim();
            string? start = Directory.Exists(current) ? current : Path.GetDirectoryName(current);
            if (start != null && Directory.Exists(start))
                dialog.InitialDirectory = start;
        }
        catch (Exception) { }
        if (dialog.ShowDialog(this) == true)
        {
            DirBox.Text = Paths.WithAppFolder(dialog.FolderName);
            Dispatcher.BeginInvoke(ShowPathEnd, DispatcherPriority.ContextIdle);
        }
    }

    void DirBox_TextChanged(object sender, TextChangedEventArgs e) => UpdateSpace();

    /// <summary>Validates the folder and fills the disk space rows; returns the folder when it is usable.</summary>
    string? UpdateSpace()
    {
        if (SpaceRequiredValue == null)
            return null;
        SpaceRequiredValue.Text = _required > 0 ? Paths.FormatSize(_required) : "…";
        string text = DirBox.Text.Trim();
        string? problem = null;
        string? dir = null;
        long free = -1;
        try
        {
            dir = Paths.Normalize(text);
            if (text.Length == 0 || Paths.Problem(dir) != null)
                problem = S.InvalidFolder;
            else
            {
                var drive = new DriveInfo(Path.GetPathRoot(dir)!);
                free = drive.IsReady ? drive.AvailableFreeSpace : -1;
                SpaceAvailableLabel.Text = $"{S.SpaceAvailable} ({drive.Name.TrimEnd('\\')})";
            }
        }
        catch (Exception)
        {
            problem = S.InvalidFolder;
        }
        SpaceAvailableValue.Text = free >= 0 ? Paths.FormatSize(free) : "—";
        if (problem == null && free >= 0 && _required > 0 && free < _required + 64L * 1024 * 1024)
            problem = S.NotEnoughSpace;
        DirError.Text = problem ?? "";
        DirError.Visibility = problem == null ? Visibility.Collapsed : Visibility.Visible;
        OptionsInstallButton.IsEnabled = problem == null;
        return problem == null ? dir : null;
    }

    // --- install -------------------------------------------------------------------------------------------

    async void Install_Click(object sender, RoutedEventArgs e)
    {
        string? dir = UpdateSpace();
        if (dir == null || _payload == null)
        {
            Go(OptionsPage);
            return;
        }
        var options = new InstallOptions
        {
            Dir = dir,
            DesktopShortcut = DesktopCheck.IsChecked == true,
            StartMenuShortcut = StartMenuCheck.IsChecked == true,
        };
        Go(InstallingPage);
        CanClose = false;
        CloseButton.IsEnabled = false;

        var progress = new Progress<InstallProgress>(p =>
        {
            _latest = p;
            _bar.Real = p.Fraction;
        });
        _latest = null;
        _bar.Reset();
        OnProgress(0);
        _bar.Start();
        try
        {
            var result = await Task.Run(() => InstallEngine.Install(_payload, options, progress, _cancel.Token));
            await _bar.WaitFullAsync();
            await Task.Delay(400); // let the bar rest on 100%
            ShowDone(result);
            Program.ExitCode = ExitCodes.Ok;
        }
        catch (Exception ex)
        {
            Log.Error("install failed", ex);
            Program.ExitCode = ex is SetupException se ? se.ExitCode : ExitCodes.Failed;
            ErrorText.Text = ex.Message;
            Go(ErrorPage);
        }
        finally
        {
            _bar.Stop();
            CanClose = true;
            CloseButton.IsEnabled = true;
        }
    }

    /// <summary>Draws the smoothed progress; the status follows the bar, not the (possibly earlier) real work.</summary>
    void OnProgress(double fraction)
    {
        PercentText.Text = $"{(int)Math.Floor(fraction * 100)}%";
        BarFill.Width = BarTrack.ActualWidth * Math.Clamp(fraction, 0, 1);
        var p = _latest;
        if (p == null)
            return;
        StatusText.Text = fraction > InstallEngine.PrepareEnd ? S.Finishing
                        : fraction < InstallEngine.ExtractShare ? S.Extracting : S.Preparing;
        long shown = (long)(p.BytesTotal * Math.Min(1, fraction / InstallEngine.ExtractShare));
        BytesText.Text = S.OfSize(Paths.FormatSize(shown), Paths.FormatSize(p.BytesTotal));
    }

    void ShowDone(InstallResult result)
    {
        _installedDir = result.Dir;
        DonePath.Text = result.Dir;
        DonePath.ToolTip = result.Dir;
        DoneShortcuts.Text = (result.Desktop, result.StartMenu) switch
        {
            (true, true) => S.ShortcutsMade,
            (true, false) => S.ShortcutDesktopOnly,
            (false, true) => S.ShortcutStartOnly,
            _ => S.NoShortcuts,
        };
        Go(DonePage);
        // the badge settles in after the page
        var scale = new ScaleTransform(0.7, 0.7);
        DoneBadge.RenderTransform = scale;
        var pop = new DoubleAnimation(0.7, 1, TimeSpan.FromMilliseconds(520))
        {
            BeginTime = TimeSpan.FromMilliseconds(260),
            EasingFunction = new BackEase { EasingMode = EasingMode.EaseOut, Amplitude = 0.3 },
        };
        scale.BeginAnimation(ScaleTransform.ScaleXProperty, pop);
        scale.BeginAnimation(ScaleTransform.ScaleYProperty, pop);
    }

    void OpenArk_Click(object sender, RoutedEventArgs e)
    {
        try { InstallEngine.OpenArk(_installedDir); }
        catch (Exception ex)
        {
            Log.Error("could not open ARK", ex);
            ErrorTitle.Text = S.LaunchFailed;
            ErrorText.Text = ex.Message;
            Go(ErrorPage);
            return;
        }
        Close();
    }

    void OpenLog_Click(object sender, RoutedEventArgs e)
    {
        try { Process.Start(new ProcessStartInfo(Log.FilePath) { UseShellExecute = true })?.Dispose(); }
        catch (Exception ex) { Log.Warn($"could not open the log: {ex.Message}"); }
    }

    protected override void OnLeaving()
    {
        _bar.Stop();
        _cancel.Cancel();
        if (Program.ExitCode == ExitCodes.Ok && _page != DonePage)
            Program.ExitCode = ExitCodes.Cancelled;
    }

#if ARK_SETUP_SELFTEST
    /// <summary>Self-test builds: shows a page at once, filled with representative content, for a screenshot.</summary>
    internal FrameworkElement PreviewPage(string name)
    {
        Panel page = name switch
        {
            "welcome" => WelcomePage,
            "options" => OptionsPage,
            "installing" => InstallingPage,
            "done" => DonePage,
            _ => ErrorPage,
        };
        foreach (Panel p in new Panel[] { WelcomePage, OptionsPage, InstallingPage, DonePage, ErrorPage })
            p.Visibility = p == page ? Visibility.Visible : Visibility.Collapsed;
        Anim.Show(page);
        _page = page;
        Art.Pose(null); // at rest: exactly the original frame
        switch (name)
        {
            case "options":
                if (_required <= 0)
                    _required = 380L * 1024 * 1024;
                UpdateSpace();
                break;
            case "installing":
                _latest = new InstallProgress(0.81, InstallPhase.Preparing, _required > 0 ? _required : 380L * 1024 * 1024, _required > 0 ? _required : 380L * 1024 * 1024);
                UpdateLayout();
                OnProgress(0.81);
                break;
            case "done":
                ShowDone(new InstallResult(Paths.Normalize(DirBox.Text), ArkInfo.Version, true, true));
                Anim.Show(DonePage);
                DoneBadge.RenderTransform = null;
                break;
            case "error":
                ErrorTitle.Text = S.FailedTitle;
                ErrorText.Text = S.ArkRunningProcess(new ArkRunningProcess("ark-mcp.exe", 12345, "synthetic", true));
                break;
        }
        return Surface;
    }
#endif
}
