using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Interop;
using System.Windows.Media;
using System.Windows.Media.Animation;
using System.Windows.Media.Imaging;
using System.Windows.Shell;
using ArkSetup.Core;

namespace ArkSetup.Windows;

/// <summary>
/// Borderless window with rounded corners and a drop shadow, shared by the installer and the uninstaller.
///
/// The XAML of each window has a root Grid "Root" with a 24 px margin, a "Shadow" border behind the content,
/// the content Grid "Surface" and a "Hairline" border on top. Two ways to draw it:
///  - Windows 11: a normal DWM window (custom chrome through WindowChrome). DWM rounds the corners, draws the
///    shadow and the open/minimise animations. The margin, Shadow and Hairline are dropped.
///  - Windows 10: a per-pixel transparent window that draws its own 14 px corners and shadow, and fades in and out.
/// </summary>
public class ChromeWindow : Window
{
    const double ShadowMargin = 24;
    static readonly bool UseDwm = Environment.OSVersion.Version.Build >= 22000
                                  && Environment.GetEnvironmentVariable("ARK_SETUP_LAYERED") != "1";
    bool _closing;
    bool _closeNow;

    /// <summary>False while work that must not be interrupted is running.</summary>
    protected bool CanClose { get; set; } = true;

    /// <summary>Rendered off-screen for screenshots: no fitting, no entrance animation.</summary>
    public bool Preview { get; init; }

    public ChromeWindow()
    {
        WindowStyle = UseDwm ? WindowStyle.SingleBorderWindow : WindowStyle.None;
        AllowsTransparency = !UseDwm;
        Background = UseDwm ? (Brush)Application.Current.FindResource("Deep") : Brushes.Transparent;
        ResizeMode = ResizeMode.CanMinimize;
        WindowStartupLocation = WindowStartupLocation.CenterScreen;
        UseLayoutRounding = true;
        SnapsToDevicePixels = true;
        Title = S.WindowTitle;
        TextOptions.SetTextFormattingMode(this, TextFormattingMode.Ideal);
        TextOptions.SetTextRenderingMode(this, TextRenderingMode.ClearType);
        try { Icon = BitmapFrame.Create(new Uri("pack://application:,,,/Art/ark-app.ico")); }
        catch (Exception) { }
        if (UseDwm)
        {
            // keep the caption styles (DWM shadow, rounding, animations) but no visible non-client frame
            WindowChrome.SetWindowChrome(this, new WindowChrome
            {
                CaptionHeight = 0,
                ResizeBorderThickness = new Thickness(0),
                GlassFrameThickness = new Thickness(0, 0, 0, 1),
                CornerRadius = new CornerRadius(0),
                UseAeroCaptionButtons = false,
            });
        }
        else
        {
            Opacity = 0;
        }
        Loaded += (_, _) => Appear();
    }

    FrameworkElement? Part(string name) => FindName(name) as FrameworkElement;

    /// <summary>After the XAML loaded: drop the self-drawn frame when DWM draws it.</summary>
    protected override void OnInitialized(EventArgs e)
    {
        base.OnInitialized(e);
        if (!UseDwm)
            return;
        if (Part("Root") is FrameworkElement root)
            root.Margin = new Thickness(0);
        foreach (string name in new[] { "Shadow", "Hairline" })
            if (Part(name) is FrameworkElement el)
                el.Visibility = Visibility.Collapsed;
        Width -= 2 * ShadowMargin;
        Height -= 2 * ShadowMargin;
    }

    protected override void OnSourceInitialized(EventArgs e)
    {
        base.OnSourceInitialized(e);
        if (UseDwm)
        {
            var hwnd = new WindowInteropHelper(this).Handle;
            int round = 2;                                  // DWMWCP_ROUND
            DwmSetWindowAttribute(hwnd, 33, ref round, 4);  // DWMWA_WINDOW_CORNER_PREFERENCE
            int dark = 1;
            DwmSetWindowAttribute(hwnd, 20, ref dark, 4);   // DWMWA_USE_IMMERSIVE_DARK_MODE
            var c = (Color)Application.Current.FindResource("WindowEdgeColor");
            int border = c.R | (c.G << 8) | (c.B << 16);    // COLORREF is BGR
            DwmSetWindowAttribute(hwnd, 34, ref border, 4); // DWMWA_BORDER_COLOR
        }
        if (!Preview)
            FitToScreen();
    }

    /// <summary>
    /// On small or heavily scaled screens (1080p at 150 %) the window would not fit the work area: scale the whole
    /// layout down instead of letting it run off the screen.
    /// </summary>
    void FitToScreen()
    {
        var area = SystemParameters.WorkArea;
        double k = Math.Min(1, Math.Min(area.Width / Width, area.Height / Height));
        if (k >= 1 || Part("Root") is not FrameworkElement root)
            return;
        k *= 0.98;
        root.LayoutTransform = new ScaleTransform(k, k);
        Width *= k;
        Height *= k;
        Left = area.Left + (area.Width - Width) / 2;
        Top = area.Top + (area.Height - Height) / 2;
        Log.Info($"window scaled to {k:0.00} to fit the screen");
    }

    void Appear()
    {
        if (Part("Surface") is Grid clip && !UseDwm)
        {
            var radius = (CornerRadius)Application.Current.FindResource("WindowRadius");
            void Round() => clip.Clip = new RectangleGeometry(new Rect(0, 0, clip.ActualWidth, clip.ActualHeight), radius.TopLeft, radius.TopLeft);
            clip.SizeChanged += (_, _) => Round();
            Round();
        }
        if (UseDwm)
            return; // DWM animates the window in
        if (Preview)
        {
            Opacity = 1;
            return;
        }
        var ease = new CubicEase { EasingMode = EasingMode.EaseOut };
        BeginAnimation(OpacityProperty, new DoubleAnimation(0, 1, TimeSpan.FromMilliseconds(320)) { EasingFunction = ease });
        if (Part("Root") is FrameworkElement root)
        {
            var scale = new ScaleTransform(0.975, 0.975);
            root.RenderTransformOrigin = new Point(0.5, 0.5);
            root.RenderTransform = scale;
            var grow = new DoubleAnimation(0.975, 1, TimeSpan.FromMilliseconds(420)) { EasingFunction = ease };
            scale.BeginAnimation(ScaleTransform.ScaleXProperty, grow);
            scale.BeginAnimation(ScaleTransform.ScaleYProperty, grow);
        }
    }

    /// <summary>Drag the window by its title bar / art.</summary>
    protected void DragArea_MouseDown(object sender, MouseButtonEventArgs e)
    {
        if (e.ChangedButton == MouseButton.Left && e.ButtonState == MouseButtonState.Pressed)
        {
            try { DragMove(); }
            catch (InvalidOperationException) { }
        }
    }

    protected void Minimize_Click(object sender, RoutedEventArgs e) => WindowState = WindowState.Minimized;

    protected void Close_Click(object sender, RoutedEventArgs e) => Close();

    /// <summary>Closing hides (or fades) the window at once; refused while work must not be interrupted.</summary>
    protected override void OnClosing(CancelEventArgs e)
    {
        if (_closeNow)
        {
            base.OnClosing(e);
            return;
        }
        e.Cancel = true;
        if (!CanClose || _closing)
            return;
        _closing = true;
        OnLeaving();
        IsHitTestVisible = false;
        void Finish()
        {
            if (_closeNow)
                return;
            _closeNow = true;
            Dispatcher.BeginInvoke(Close);
        }
        if (UseDwm || Preview)
        {
            Dispatcher.BeginInvoke(Hide); // not inside the Closing call itself
            Finish();
            return;
        }
        var fade = new DoubleAnimation(Opacity, 0, TimeSpan.FromMilliseconds(260))
        {
            EasingFunction = new CubicEase { EasingMode = EasingMode.EaseIn },
        };
        fade.Completed += (_, _) => Finish();
        BeginAnimation(OpacityProperty, fade);
    }

    /// <summary>Called once when the window starts closing.</summary>
    protected virtual void OnLeaving() { }

    /// <summary>Spaced capitals for small headings ("M E S H S P H E R E").</summary>
    public static string Spaced(string s) =>
        string.Join(" ", s.ToUpperInvariant().Select(c => c == ' ' ? " " : c.ToString()));

    [DllImport("dwmapi.dll")]
    static extern int DwmSetWindowAttribute(IntPtr hwnd, int attribute, ref int value, int size);
}
