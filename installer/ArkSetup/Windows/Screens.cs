#if ARK_SETUP_SELFTEST
using System.Globalization;
using System.IO;
using System.Text;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Threading;
using ArkSetup.Core;

namespace ArkSetup.Windows;

/// <summary>
/// Self-test builds only (build-installer.ps1 -SelfTest): renders every page off-screen to PNG with
/// RenderTargetBitmap, without installing anything, and writes layout.txt with simple checks per page:
/// controls outside their page, top and bottom blocks overlapping, and clipped (untrimmed) text.
/// </summary>
public static class Screens
{
    static readonly string[] SetupPages = { "welcome", "options", "installing", "done", "error" };

    public static async void Run(App app, Payload? payload, string folder, double scale, bool layoutOnly)
    {
        int code = ExitCodes.Ok;
        try
        {
            Directory.CreateDirectory(folder);
            var report = new StringBuilder();
            int n = 1, problems = 0;

            var setup = new SetupWindow(payload) { Preview = true };
            OffScreen(setup);
            setup.Show();
            await Settle(setup);
            foreach (string name in SetupPages)
            {
                setup.PreviewPage(name);
                await Settle(setup);
                var page = setup.Pages.Children.OfType<Panel>().First(p => p.Visibility == Visibility.Visible);
                problems += Check(report, name, setup.Surface, page);
                if (!layoutOnly)
                    Save(setup.Surface, Path.Combine(folder, $"{n:00}-{name}.png"), scale);
                n++;
            }
            setup.Close();

            var uninstall = new UninstallWindow(ArkInfo.DefaultInstallDir, preview: true);
            OffScreen(uninstall);
            uninstall.Show();
            await Settle(uninstall);
            problems += Check(report, "uninstall", uninstall.Surface, uninstall.Body);
            if (!layoutOnly)
                Save(uninstall.Surface, Path.Combine(folder, $"{n:00}-uninstall.png"), scale);
            uninstall.Close();

            File.WriteAllText(Path.Combine(folder, "layout.txt"), report.ToString(), new UTF8Encoding(false));
            Log.Info($"screenshots in {folder}, {problems} layout problems");
            code = problems == 0 ? ExitCodes.Ok : ExitCodes.Failed;
        }
        catch (Exception e)
        {
            Log.Error("screenshots failed", e);
            code = ExitCodes.Failed;
        }
        finally
        {
            Program.ExitCode = code;
            app.Shutdown(code);
        }
    }

    static void OffScreen(Window w)
    {
        w.WindowStartupLocation = WindowStartupLocation.Manual;
        w.Left = -32000;
        w.Top = -32000;
        w.ShowActivated = false;
        w.ShowInTaskbar = false;
    }

    static async Task Settle(Window w)
    {
        w.UpdateLayout();
        await w.Dispatcher.InvokeAsync(() => { }, DispatcherPriority.ContextIdle);
        await Task.Delay(350);
        await w.Dispatcher.InvokeAsync(() => { }, DispatcherPriority.ContextIdle);
    }

    static void Save(FrameworkElement e, string path, double scale)
    {
        double w = e.ActualWidth, h = e.ActualHeight;
        var visual = new DrawingVisual();
        using (var dc = visual.RenderOpen())
        {
            var brush = new VisualBrush(e)
            {
                Stretch = Stretch.Fill,
                ViewboxUnits = BrushMappingMode.Absolute,
                Viewbox = new Rect(0, 0, w, h),
            };
            dc.DrawRectangle(brush, null, new Rect(0, 0, w, h));
        }
        var rtb = new RenderTargetBitmap((int)Math.Ceiling(w * scale), (int)Math.Ceiling(h * scale), 96 * scale, 96 * scale, PixelFormats.Pbgra32);
        rtb.Render(visual);
        var png = new PngBitmapEncoder();
        png.Frames.Add(BitmapFrame.Create(rtb));
        using var fs = File.Create(path);
        png.Save(fs);
    }

    static IEnumerable<DependencyObject> Descendants(DependencyObject root)
    {
        int count = VisualTreeHelper.GetChildrenCount(root);
        for (int i = 0; i < count; i++)
        {
            var child = VisualTreeHelper.GetChild(root, i);
            yield return child;
            foreach (var d in Descendants(child))
                yield return d;
        }
    }

    static Rect Bounds(FrameworkElement e, Visual ancestor) =>
        e.TransformToAncestor(ancestor).TransformBounds(new Rect(new Point(0, 0), e.RenderSize));

    static double NaturalWidth(TextBlock tb)
    {
        var ft = new FormattedText(tb.Text, CultureInfo.CurrentUICulture, tb.FlowDirection,
                                   new Typeface(tb.FontFamily, tb.FontStyle, tb.FontWeight, tb.FontStretch), tb.FontSize,
                                   Brushes.Black, VisualTreeHelper.GetDpi(tb).PixelsPerDip);
        return ft.WidthIncludingTrailingWhitespace + tb.Padding.Left + tb.Padding.Right;
    }

    /// <summary>Appends one line for the page; returns the number of problems (trimmed paths are allowed).</summary>
    static int Check(StringBuilder report, string name, FrameworkElement surface, FrameworkElement page)
    {
        int elements = 0, outside = 0, overlap = 0, clipped = 0, trimmed = 0;
        var notes = new List<string>();
        var region = Bounds(page, surface);
        var window = new Rect(0, 0, surface.ActualWidth, surface.ActualHeight);
        if (!window.Contains(region))
        {
            outside++;
            notes.Add("page outside the window");
        }
        region.Inflate(4, 1); // optical alignment (large numerals sit a few pixels left)
        foreach (var fe in Descendants(page).OfType<FrameworkElement>())
        {
            if (!fe.IsVisible || fe is not (TextBlock or Button or TextBox or CheckBox or Image or System.Windows.Shapes.Shape))
                continue;
            if (fe is TextBlock { Text.Length: 0 })
                continue;
            elements++;
            var r = Bounds(fe, surface);
            if (!region.Contains(r))
            {
                outside++;
                notes.Add($"{Describe(fe)} outside");
            }
            if (fe is TextBlock tb)
            {
                if (tb.TextWrapping == TextWrapping.NoWrap && NaturalWidth(tb) > tb.ActualWidth + 1)
                {
                    if (tb.TextTrimming != TextTrimming.None) trimmed++;
                    else { clipped++; notes.Add($"{Describe(tb)} clipped"); }
                }
                // text in a button must fit the button
                if (FindAncestor<Button>(tb) is Button b && b.IsVisible)
                {
                    var br = Bounds(b, surface);
                    br.Inflate(0.5, 0.5);
                    if (!br.Contains(r)) { clipped++; notes.Add($"{Describe(tb)} overflows its button"); }
                }
            }
        }
        if (page is Panel panel)
        {
            var blocks = panel.Children.OfType<FrameworkElement>().Where(c => c.IsVisible).ToList();
            foreach (var top in blocks.Where(b => b.VerticalAlignment == VerticalAlignment.Top))
                foreach (var bottom in blocks.Where(b => b.VerticalAlignment == VerticalAlignment.Bottom))
                    if (Bounds(top, surface).Bottom > Bounds(bottom, surface).Top - 12)
                    {
                        overlap++;
                        notes.Add("top and bottom blocks touch");
                    }
        }
        report.Append($"{name}: elements={elements}; outside={outside}; overlap={overlap}; clipped={clipped}; trimmed={trimmed}");
        if (notes.Count > 0)
            report.Append("; notes=" + string.Join(" | ", notes));
        report.AppendLine();
        return outside + overlap + clipped;
    }

    static T? FindAncestor<T>(DependencyObject d) where T : DependencyObject
    {
        for (var p = VisualTreeHelper.GetParent(d); p != null; p = VisualTreeHelper.GetParent(p))
            if (p is T t)
                return t;
        return null;
    }

    static string Describe(FrameworkElement e) =>
        e is TextBlock tb ? $"\"{(tb.Text.Length > 30 ? tb.Text[..30] + "…" : tb.Text)}\"" : (e.Name is { Length: > 0 } n ? n : e.GetType().Name);
}
#endif
