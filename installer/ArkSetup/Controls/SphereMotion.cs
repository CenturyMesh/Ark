using System.Windows;
using System.Windows.Media.Animation;

namespace ArkSetup.Controls;

/// <summary>
/// The sphere's wandering: per channel a sum of sines with incommensurate periods, so the path never visibly
/// repeats. Time runs through a phase τ that advances at an eased speed, and the amplitude eases too, so switching
/// between calm and lively never makes the sphere jump.
/// </summary>
public sealed class SphereMotion
{
    public const int X = 0, Y = 1, Scale = 2, Angle = 3;

    /// <summary>(period at calm speed in seconds, weight, phase). The weights of a channel add up to 1.</summary>
    static readonly (double Period, double Weight, double Phase)[][] Waves =
    {
        new[] { (31.7, 0.55, 0.3), (19.3, 0.30, 2.1), (11.9, 0.15, 4.4) },
        new[] { (27.1, 0.55, 1.7), (16.7, 0.30, 5.0), (9.7, 0.15, 0.9) },
        new[] { (23.3, 0.70, 3.1), (13.1, 0.30, 1.2) },
        new[] { (37.9, 0.70, 0.6), (21.7, 0.30, 4.2) },
    };

    /// <summary>Speed and amplitude while ARK installs, relative to the calm state.</summary>
    public const double LivelySpeed = 2.6, LivelyAmplitude = 1.3;
    /// <summary>Time constant of the ease between states (about 1 s to settle).</summary>
    const double EaseSeconds = 0.32;

    /// <summary>Calm amplitudes: translation in DIPs, scale as a fraction, rotation in degrees.</summary>
    public double AmplitudeX { get; set; }
    public double AmplitudeY { get; set; }
    public double AmplitudeScale { get; set; } = 0.03;
    public double AmplitudeAngle { get; set; } = 4;

    double _last = double.NaN, _tau, _speed = 1, _amp = 1, _targetSpeed = 1, _targetAmp = 1;

    public bool Lively
    {
        get => _targetSpeed > 1;
        set
        {
            _targetSpeed = value ? LivelySpeed : 1;
            _targetAmp = value ? LivelyAmplitude : 1;
        }
    }

    /// <summary>Moves the phase to global time <paramref name="seconds"/> (once per frame; repeated calls are free).</summary>
    public void Advance(double seconds)
    {
        if (seconds == _last)
            return;
        // a restarted clock starts again from 0: that frame simply does not advance
        double dt = double.IsNaN(_last) ? 0 : Math.Clamp(seconds - _last, 0, 0.25);
        _last = seconds;
        double k = 1 - Math.Exp(-dt / EaseSeconds);
        _speed += (_targetSpeed - _speed) * k;
        _amp += (_targetAmp - _amp) * k;
        _tau += _speed * dt;
    }

    public double Value(int channel) => ValueAt(channel, _tau, _amp);

    /// <summary>Current speed relative to calm (eases towards 1 or <see cref="LivelySpeed"/>).</summary>
    public double Speed => _speed;

    /// <summary>The pose at phase <paramref name="tau"/> with amplitude factor <paramref name="amp"/>.</summary>
    public double ValueAt(int channel, double tau, double amp)
    {
        double w = Wander(channel, tau) * amp;
        return channel switch
        {
            X => w * AmplitudeX,
            Y => w * AmplitudeY,
            Scale => 1 + w * AmplitudeScale,
            _ => w * AmplitudeAngle,
        };
    }

    /// <summary>The normalised wander of a channel, in [-1, 1].</summary>
    public static double Wander(int channel, double tau)
    {
        double sum = 0;
        foreach (var (period, weight, phase) in Waves[channel])
            sum += weight * Math.Sin(2 * Math.PI * tau / period + phase);
        return sum;
    }
}

/// <summary>
/// Drives one channel of a <see cref="SphereMotion"/> through WPF's animation clock, so frames are paced with the
/// display and capped by <see cref="Timeline.DesiredFrameRateProperty"/> (no per-frame event handler).
/// </summary>
public sealed class WanderAnimation : DoubleAnimationBase
{
    SphereMotion? _motion;
    int _channel;

    public static WanderAnimation Create(SphereMotion motion, int channel)
    {
        var a = new WanderAnimation { _motion = motion, _channel = channel, Duration = Duration.Forever };
        Timeline.SetDesiredFrameRate(a, Anim.AmbientFps);
        a.Freeze(); // frozen, so WPF uses this instance (and its motion) instead of a clone
        return a;
    }

    protected override Freezable CreateInstanceCore() => new WanderAnimation();

    protected override double GetCurrentValueCore(double defaultOriginValue, double defaultDestinationValue, AnimationClock animationClock)
    {
        // the channels begin together and never repeat, so their clock times are the same frame time
        if (_motion == null || animationClock.CurrentTime is not TimeSpan now)
            return defaultOriginValue;
        _motion.Advance(now.TotalSeconds);
        return _motion.Value(_channel);
    }
}
