using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using System.Windows.Media.Animation;

namespace ArkSetup.Controls;

/// <summary>
/// Full-bleed MeshSphere key art with a very slow, subtle drift. The art (a sphere centred in a 16:9 frame) is
/// sized to <see cref="ArtHeight"/> times the window height and placed so that the sphere's centre lands at
/// (<see cref="FocusX"/>, <see cref="FocusY"/>) of the window: the sphere sits in the right half and the text
/// panel on the left stays calm. The art's left and top edges fade into the background, so they never show a seam.
/// </summary>
public sealed class ArtBackdrop : Grid
{
    /// <summary>Where the sphere's centre lies in the source image (fractions of its width and height).</summary>
    const double SourceFocusX = 0.5, SourceFocusY = 0.405;

    readonly Grid _frame;
    readonly ScaleTransform _zoom = new(1, 1);
    readonly TranslateTransform _pan = new();
    bool _animating;

    public double FocusX { get; set; } = 0.74;
    public double FocusY { get; set; } = 0.5;
    /// <summary>Art height as a multiple of the window height (the sphere is about 47 % of the art height).</summary>
    public double ArtHeight { get; set; } = 1.16;

    public ArtBackdrop()
    {
        ClipToBounds = true;
        IsHitTestVisible = false;
        Background = (Brush)Application.Current.FindResource("Deep");
        var image = new Image
        {
            Source = Images.KeyArt(),
            Stretch = Stretch.Fill,
            OpacityMask = (Brush)Application.Current.FindResource("ArtFadeLeft"),
        };
        RenderOptions.SetBitmapScalingMode(image, BitmapScalingMode.HighQuality);
        _frame = new Grid
        {
            HorizontalAlignment = HorizontalAlignment.Left,
            VerticalAlignment = VerticalAlignment.Top,
            OpacityMask = (Brush)Application.Current.FindResource("ArtFadeTop"),
            RenderTransform = new TransformGroup { Children = { _zoom, _pan } },
            RenderTransformOrigin = new Point(SourceFocusX, SourceFocusY),
            Children = { image },
        };
        Children.Add(_frame);
        SizeChanged += (_, _) => Place();
        Loaded += (_, _) => Animate();
    }

    void Place()
    {
        double w = ActualWidth, h = ActualHeight;
        if (w <= 0 || h <= 0 || Images.KeyArt() is not ImageSource src)
            return;
        double aspect = src.Width / src.Height;
        // the requested size, but always enough to cover the bottom and right edges (the drift moves up to ~10 px)
        double hBottom = ((1 - FocusY) * h + 12) / (1 - SourceFocusY);
        double hRight = ((1 - FocusX) * w + 12) / ((1 - SourceFocusX) * aspect);
        double artH = Math.Max(ArtHeight * h, Math.Max(hBottom, hRight));
        double artW = artH * aspect;
        _frame.Width = artW;
        _frame.Height = artH;
        _frame.Margin = new Thickness(FocusX * w - SourceFocusX * artW, FocusY * h - SourceFocusY * artH, 0, 0);
    }

    void Animate()
    {
        if (_animating)
            return;
        _animating = true;
        var ease = new SineEase { EasingMode = EasingMode.EaseInOut };
        void Loop(Animatable target, DependencyProperty prop, double from, double to, double seconds)
        {
            var a = new DoubleAnimation(from, to, TimeSpan.FromSeconds(seconds))
            {
                AutoReverse = true,
                RepeatBehavior = RepeatBehavior.Forever,
                EasingFunction = ease,
            };
            Timeline.SetDesiredFrameRate(a, Anim.AmbientFps);
            target.BeginAnimation(prop, a);
        }
        // zoom and pan on different long periods: a slow breath that never visibly loops
        Loop(_zoom, ScaleTransform.ScaleXProperty, 1.0, 1.03, 47);
        Loop(_zoom, ScaleTransform.ScaleYProperty, 1.0, 1.03, 47);
        Loop(_pan, TranslateTransform.XProperty, 0, -8, 61);
        Loop(_pan, TranslateTransform.YProperty, 0, 5, 43);
    }
}
