using System.Windows.Media;

namespace ArkSetup.Controls;

/// <summary>
/// Eases a progress value towards the real one, never faster than a full bar in <see cref="MinSeconds"/>.
/// Extraction can be very quick; this keeps the bar readable.
/// </summary>
public sealed class SmoothProgress
{
    TimeSpan _last = TimeSpan.Zero;
    bool _running;

    public double Real { get; set; }
    public double Shown { get; private set; }
    public double MinSeconds { get; init; } = 3.0;

    /// <summary>Called on every frame the shown value changes.</summary>
    public event Action<double>? Changed;

    public void Start()
    {
        if (_running)
            return;
        _running = true;
        _last = TimeSpan.Zero;
        CompositionTarget.Rendering += OnFrame;
    }

    public void Reset()
    {
        Real = 0;
        Shown = 0;
    }

    public void Stop()
    {
        _running = false;
        CompositionTarget.Rendering -= OnFrame;
    }

    /// <summary>Completes when the shown value has caught up with a finished job.</summary>
    public async Task WaitFullAsync()
    {
        Real = 1.0;
        while (_running && Shown < 0.9999)
            await Task.Delay(30);
    }

    void OnFrame(object? sender, EventArgs e)
    {
        var now = ((RenderingEventArgs)e).RenderingTime;
        double dt = _last == TimeSpan.Zero ? 0 : Math.Min(0.1, (now - _last).TotalSeconds);
        _last = now;
        if (Shown >= Real)
            return;
        Shown = Math.Min(Real, Shown + dt / MinSeconds);
        Changed?.Invoke(Shown);
    }
}
