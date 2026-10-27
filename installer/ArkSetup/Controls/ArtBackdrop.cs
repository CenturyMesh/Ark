using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;

namespace ArkSetup.Controls;

/// <summary>
/// Full-bleed MeshSphere key art in two layers: a still plate (the studio and its stars) and the sphere, which
/// wanders slowly over it (<see cref="SphereMotion"/>), more lively while ARK installs. The art is sized to
/// <see cref="ArtHeight"/> times the window height and placed so the sphere's rest centre lands at
/// (<see cref="FocusX"/>, <see cref="FocusY"/>) of the window; the plate's left and top edges fade into the
/// background. The sphere never enters the left half (the text panel). With Windows animations off it stays still,
/// exactly where it is in the original frame.
/// </summary>
public sealed class ArtBackdrop : Grid
{
    // Source frame and where the sprite was cut from it (installer/art/make_layers.py, layers.json).
    const double SourceWidth = 1920, SourceHeight = 1080;
    const double SpriteX = 638, SpriteY = 112, SpriteSize = 640;
    const double SphereX = 958.5, SphereY = 432, SphereRadius = 257.5;
    /// <summary>Calm wander range as a fraction of the window size.</summary>
    const double Range = 0.04;

    readonly Grid _plate;
    readonly Image _sphere;
    readonly ScaleTransform _scale = new(1, 1);
    readonly RotateTransform _rotate = new(0);
    readonly TranslateTransform _move = new();
    readonly SphereMotion _motion = new();
    bool _animating;
    Point _center;
    double _radius;

    public double FocusX { get; set; } = 0.74;
    public double FocusY { get; set; } = 0.5;
    /// <summary>Art height as a multiple of the window height (the sphere is 48 % of the art height).</summary>
    public double ArtHeight { get; set; } = 1.16;
    /// <summary>Multiplier of the wander range (the uninstall window is calmer).</summary>
    public double MotionScale { get; set; } = 1.0;

    /// <summary>Livelier wandering (while installing); eases over about a second.</summary>
    public bool Lively
    {
        get => _motion.Lively;
        set => _motion.Lively = value;
    }

    public static bool MotionAllowed => SystemParameters.ClientAreaAnimation;

    public ArtBackdrop()
    {
        ClipToBounds = true;
        IsHitTestVisible = false;
        Background = (Brush)Application.Current.FindResource("Deep");
        var plate = new Image
        {
            Source = Images.Plate(),
            Stretch = Stretch.Fill,
            OpacityMask = (Brush)Application.Current.FindResource("ArtFadeLeft"),
        };
        RenderOptions.SetBitmapScalingMode(plate, BitmapScalingMode.HighQuality);
        _plate = new Grid
        {
            HorizontalAlignment = HorizontalAlignment.Left,
            VerticalAlignment = VerticalAlignment.Top,
            OpacityMask = (Brush)Application.Current.FindResource("ArtFadeTop"),
            Children = { plate },
            CacheMode = new BitmapCache(), // still: composed once, then only blitted under the moving sphere
        };
        _sphere = new Image
        {
            Source = Images.Sphere(),
            Stretch = Stretch.Fill,
            HorizontalAlignment = HorizontalAlignment.Left,
            VerticalAlignment = VerticalAlignment.Top,
            RenderTransformOrigin = new Point((SphereX - SpriteX) / SpriteSize, (SphereY - SpriteY) / SpriteSize),
            RenderTransform = new TransformGroup { Children = { _scale, _rotate, _move } },
            CacheMode = new BitmapCache(), // the GPU moves the cached bitmap; nothing is re-rasterised per frame
        };
        RenderOptions.SetBitmapScalingMode(_sphere, BitmapScalingMode.HighQuality);
        Children.Add(_plate);
        Children.Add(_sphere);
        SizeChanged += (_, _) => Place();
        Loaded += (_, _) =>
        {
            CacheAtDeviceScale(VisualTreeHelper.GetDpi(this).DpiScaleX);
            Start();
        };
        Unloaded += (_, _) => Stop();
    }

    protected override void OnDpiChanged(DpiScale oldDpi, DpiScale newDpi)
    {
        base.OnDpiChanged(oldDpi, newDpi);
        CacheAtDeviceScale(newDpi.DpiScaleX);
    }

    /// <summary>Caches at device resolution, so the cached layers stay sharp on scaled displays.</summary>
    void CacheAtDeviceScale(double scale)
    {
        scale = Math.Clamp(scale, 1, 4);
        _plate.CacheMode = new BitmapCache(scale);
        _sphere.CacheMode = new BitmapCache(scale * 1.05); // a little headroom for the breathing scale
    }

    void Place()
    {
        double w = ActualWidth, h = ActualHeight;
        if (w <= 0 || h <= 0)
            return;
        double fx = SphereX / SourceWidth, fy = SphereY / SourceHeight, aspect = SourceWidth / SourceHeight;
        // the requested size, but always enough to cover the bottom and right edges
        double hBottom = ((1 - FocusY) * h + 12) / (1 - fy);
        double hRight = ((1 - FocusX) * w + 12) / ((1 - fx) * aspect);
        double artH = Math.Max(ArtHeight * h, Math.Max(hBottom, hRight));
        double artW = artH * aspect, k = artW / SourceWidth;
        double left = FocusX * w - fx * artW, top = FocusY * h - fy * artH;
        _plate.Width = artW;
        _plate.Height = artH;
        _plate.Margin = new Thickness(left, top, 0, 0);
        _sphere.Width = _sphere.Height = SpriteSize * k;
        _sphere.Margin = new Thickness(left + SpriteX * k, top + SpriteY * k, 0, 0);
        _center = new Point(left + SphereX * k, top + SphereY * k);
        _radius = SphereRadius * k;

        // the wander range, kept clear of the left half even at its liveliest and largest
        double lively = SphereMotion.LivelyAmplitude;
        double grow = 1 + _motion.AmplitudeScale * lively;
        double room = _center.X - _radius * grow - w / 2;
        _motion.AmplitudeX = Math.Max(0, Math.Min(Range * w * MotionScale, room / lively));
        _motion.AmplitudeY = Range * h * MotionScale;
        _motion.AmplitudeScale = 0.03 * Math.Min(1, MotionScale);
        _motion.AmplitudeAngle = 4 * Math.Min(1, MotionScale);
    }

    void Start()
    {
        if (_animating || !MotionAllowed)
            return;
        _animating = true;
        _move.BeginAnimation(TranslateTransform.XProperty, WanderAnimation.Create(_motion, SphereMotion.X));
        _move.BeginAnimation(TranslateTransform.YProperty, WanderAnimation.Create(_motion, SphereMotion.Y));
        _scale.BeginAnimation(ScaleTransform.ScaleXProperty, WanderAnimation.Create(_motion, SphereMotion.Scale));
        _scale.BeginAnimation(ScaleTransform.ScaleYProperty, WanderAnimation.Create(_motion, SphereMotion.Scale));
        _rotate.BeginAnimation(RotateTransform.AngleProperty, WanderAnimation.Create(_motion, SphereMotion.Angle));
    }

    void Stop()
    {
        _animating = false;
        _move.BeginAnimation(TranslateTransform.XProperty, null);
        _move.BeginAnimation(TranslateTransform.YProperty, null);
        _scale.BeginAnimation(ScaleTransform.ScaleXProperty, null);
        _scale.BeginAnimation(ScaleTransform.ScaleYProperty, null);
        _rotate.BeginAnimation(RotateTransform.AngleProperty, null);
    }

    /// <summary>
    /// Holds the sphere still: at rest (<paramref name="tau"/> null, exactly as in the original frame) or at the pose
    /// of phase <paramref name="tau"/> seconds, calm or lively. Used for screenshots.
    /// </summary>
    public void Pose(double? tau, bool lively = false)
    {
        Stop();
        Place();
        double amp = lively ? SphereMotion.LivelyAmplitude : 1;
        _move.X = tau is double t ? _motion.ValueAt(SphereMotion.X, t, amp) : 0;
        _move.Y = tau is double t2 ? _motion.ValueAt(SphereMotion.Y, t2, amp) : 0;
        _scale.ScaleX = _scale.ScaleY = tau is double t3 ? _motion.ValueAt(SphereMotion.Scale, t3, amp) : 1;
        _rotate.Angle = tau is double t4 ? _motion.ValueAt(SphereMotion.Angle, t4, amp) : 0;
    }

    /// <summary>The live (animated) offset of the sphere and the motion's current relative speed (for self-tests).</summary>
    public (Point Offset, double Speed) Live => (new Point(_move.X, _move.Y), _motion.Speed);

    /// <summary>The sphere's extent over a long stretch of wandering at the lively amplitude (for layout checks).</summary>
    public Rect WanderBounds(double seconds = 900, double step = 0.25)
    {
        Place();
        double minX = double.MaxValue, minY = double.MaxValue, maxX = double.MinValue, maxY = double.MinValue;
        double amp = SphereMotion.LivelyAmplitude;
        for (double t = 0; t <= seconds; t += step)
        {
            double r = _radius * _motion.ValueAt(SphereMotion.Scale, t, amp);
            double x = _center.X + _motion.ValueAt(SphereMotion.X, t, amp), y = _center.Y + _motion.ValueAt(SphereMotion.Y, t, amp);
            minX = Math.Min(minX, x - r); maxX = Math.Max(maxX, x + r);
            minY = Math.Min(minY, y - r); maxY = Math.Max(maxY, y + r);
        }
        return new Rect(minX, minY, maxX - minX, maxY - minY);
    }
}
