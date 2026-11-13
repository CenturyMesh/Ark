using System.Windows;
using System.Windows.Automation;
using ArkSetup.Controls;
using ArkSetup.Core;

namespace ArkSetup.Windows;

/// <summary>Small confirm window for uninstall.exe.</summary>
public partial class UninstallWindow : ChromeWindow
{
    readonly string _dir;
    bool _done;

    public UninstallWindow(string dir, bool preview = false)
    {
        Preview = preview;
        _dir = Paths.Normalize(dir);
        InitializeComponent();
        string? version = SetupManifest.Read(_dir)?.Version is { Length: > 0 } v ? v : UninstallEntry.InstalledVersion(_dir);
        Kicker.Text = Spaced(version != null ? $"{ArkInfo.Product} · {version}" : ArkInfo.Product);
        TitleText.Text = S.UninstallTitle;
        BodyText.Text = S.UninstallBody(_dir);
        KeepsText.Text = S.UninstallKeeps;
        CancelText.Text = S.Cancel;
        UninstallText.Text = S.UninstallButton;
        CloseButton.ToolTip = S.Close;
        AutomationProperties.SetName(CloseButton, S.Close);
        AutomationProperties.SetName(RemoveProgressValue, S.Removing);
        Program.ExitCode = ExitCodes.Cancelled;
        if (!preview && !Uninstaller.LooksInstalled(_dir))
        {
            // nothing of ours here: say so and offer only Close
            BodyText.Text = S.NotInstalledHere;
            KeepsText.Text = _dir;
            UninstallButton.Visibility = Visibility.Collapsed;
            CancelText.Text = S.Close;
            Program.ExitCode = ExitCodes.NotInstalled;
        }
        Loaded += (_, _) =>
        {
            if (!Preview)
                Anim.Reveal(Body, 120);
        };
    }

    async void Uninstall_Click(object sender, RoutedEventArgs e)
    {
        if (_done)
            return;
        CanClose = false;
        CloseButton.IsEnabled = false;
        UninstallButton.IsEnabled = false;
        CancelButton.IsEnabled = false;
        TitleText.Text = S.Removing;
        BarTrack.Visibility = Visibility.Visible;
        Anim.Enter(BarTrack, 0, 0, 260);
        var progress = new Progress<double>(f =>
        {
            Anim.Width(BarFill, BarTrack.ActualWidth * f, 200);
            RemoveProgressValue.Value = Math.Floor(Math.Clamp(f, 0, 1) * 100);
        });
        try
        {
            bool gone = await Task.Run(() => Uninstaller.Run(_dir, progress));
            await Task.Delay(350);
            TitleText.Text = S.Removed;
            BodyText.Text = S.UninstallKeeps;
            KeepsText.Text = gone ? "" : S.RemovedKept;
            Program.ExitCode = ExitCodes.Ok;
        }
        catch (Exception ex)
        {
            Log.Error("uninstall failed", ex);
            Program.ExitCode = ex is SetupException se ? se.ExitCode : ExitCodes.Failed;
            TitleText.Text = S.UninstallFailedTitle;
            BodyText.Text = ex.Message;
            KeepsText.Text = Log.FilePath;
        }
        BarTrack.Visibility = Visibility.Hidden;
        _done = true;
        CanClose = true;
        CloseButton.IsEnabled = true;
        UninstallButton.Visibility = Visibility.Collapsed;
        CancelText.Text = S.Close;
        CancelButton.IsEnabled = true;
        Anim.Enter(Buttons, 0, 6, 320);
    }
}
