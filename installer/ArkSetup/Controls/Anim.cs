using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using System.Windows.Media.Animation;

namespace ArkSetup.Controls;

/// <summary>
/// Small animation helpers for page transitions and reveals. With Windows animations turned off (Settings ›
/// Accessibility › Visual effects) every helper lands on its end state at once.
/// </summary>
public static class Anim
{
    /// <summary>Frame rate of the endless ambient animation (art drift).</summary>
    public const int AmbientFps = 30;

    /// <summary>False when Windows animations are turned off.</summary>
    public static bool Allowed => SystemParameters.ClientAreaAnimation;

    static readonly IEasingFunction Out = new CubicEase { EasingMode = EasingMode.EaseOut };
    static readonly IEasingFunction In = new CubicEase { EasingMode = EasingMode.EaseIn };

    static TranslateTransform Shift(UIElement e)
    {
        if (e.RenderTransform is TranslateTransform t && !t.IsFrozen)
            return t;
        t = new TranslateTransform();
        e.RenderTransform = t;
        return t;
    }

    /// <summary>Fades and slides an element in from <paramref name="dx"/>, <paramref name="dy"/>.</summary>
    public static void Enter(UIElement e, double dx, double dy, int ms, int delayMs = 0)
    {
        var t = Shift(e);
        if (!Allowed)
        {
            e.BeginAnimation(UIElement.OpacityProperty, null);
            t.BeginAnimation(TranslateTransform.XProperty, null);
            t.BeginAnimation(TranslateTransform.YProperty, null);
            e.Opacity = 1;
            t.X = t.Y = 0;
            return;
        }
        var begin = TimeSpan.FromMilliseconds(delayMs);
        var d = new Duration(TimeSpan.FromMilliseconds(ms));
        e.Opacity = 0;
        e.BeginAnimation(UIElement.OpacityProperty, new DoubleAnimation(0, 1, d) { BeginTime = begin, EasingFunction = Out });
        t.BeginAnimation(TranslateTransform.XProperty, new DoubleAnimation(dx, 0, d) { BeginTime = begin, EasingFunction = Out });
        t.BeginAnimation(TranslateTransform.YProperty, new DoubleAnimation(dy, 0, d) { BeginTime = begin, EasingFunction = Out });
    }

    /// <summary>Fades and slides an element out, then runs <paramref name="done"/>.</summary>
    public static void Leave(UIElement e, double dx, int ms, Action? done = null)
    {
        var t = Shift(e);
        if (!Allowed)
        {
            e.BeginAnimation(UIElement.OpacityProperty, null);
            e.Opacity = 0;
            done?.Invoke();
            return;
        }
        var d = new Duration(TimeSpan.FromMilliseconds(ms));
        var fade = new DoubleAnimation(e.Opacity, 0, d) { EasingFunction = In };
        if (done != null)
            fade.Completed += (_, _) => done();
        e.BeginAnimation(UIElement.OpacityProperty, fade);
        t.BeginAnimation(TranslateTransform.XProperty, new DoubleAnimation(0, dx, d) { EasingFunction = In });
    }

    /// <summary>Reveals a page: its stacked children rise gently into place one after the other.</summary>
    public static void Reveal(Panel page, int delayMs = 0)
    {
        Show(page);
        int i = 0;
        foreach (UIElement block in page.Children)
        {
            if (block is StackPanel stack)
                foreach (UIElement child in stack.Children)
                    Enter(child, 0, 10, 420, delayMs + 40 * i++);
            else
                Enter(block, 0, 10, 420, delayMs + 40 * i++);
        }
    }

    /// <summary>Shows a page at once, with no animation left on it or its children.</summary>
    public static void Show(Panel page)
    {
        page.Visibility = Visibility.Visible;
        page.BeginAnimation(UIElement.OpacityProperty, null);
        page.Opacity = 1;
        if (page.RenderTransform is TranslateTransform pt)
        {
            pt.BeginAnimation(TranslateTransform.XProperty, null);
            pt.X = 0;
        }
    }

    /// <summary>Swaps pages: the old one slides away to the left, the new one rises in.</summary>
    public static void Swap(Panel from, Panel to)
    {
        if (from == to)
            return;
        from.IsHitTestVisible = false;
        Leave(from, -20, 180, () =>
        {
            from.Visibility = Visibility.Collapsed;
            from.IsHitTestVisible = true;
            Reveal(to);
        });
    }

    /// <summary>Animates a width (progress bars).</summary>
    public static void Width(FrameworkElement e, double to, int ms)
    {
        if (!Allowed)
        {
            e.BeginAnimation(FrameworkElement.WidthProperty, null);
            e.Width = to;
            return;
        }
        e.BeginAnimation(FrameworkElement.WidthProperty,
                         new DoubleAnimation(to, new Duration(TimeSpan.FromMilliseconds(ms))) { EasingFunction = Out });
    }
}
