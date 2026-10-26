using System.IO;
using System.Windows;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using ArkSetup.Core;

namespace ArkSetup.Controls;

/// <summary>The embedded art: key art, ARK wordmark and icon, decoded once.</summary>
public static class Images
{
    static BitmapSource? _art, _wordmark, _icon;

    static BitmapSource? Load(string name, int decodeWidth = 0)
    {
        try
        {
            var info = Application.GetResourceStream(new Uri($"pack://application:,,,/Art/{name}"));
            if (info == null)
                return null;
            using var stream = info.Stream;
            var bmp = new BitmapImage();
            bmp.BeginInit();
            bmp.CacheOption = BitmapCacheOption.OnLoad;
            if (decodeWidth > 0)
                bmp.DecodePixelWidth = decodeWidth;
            bmp.StreamSource = stream;
            bmp.EndInit();
            bmp.Freeze();
            return bmp;
        }
        catch (Exception e)
        {
            Log.Warn($"image {name} missing: {e.Message}");
            return null;
        }
    }

    public static BitmapSource? KeyArt() => _art ??= Load("keyart.jpg");

    /// <summary>The ARK wordmark with its transparent margins trimmed, so it aligns with the text column.</summary>
    public static BitmapSource? Wordmark()
    {
        if (_wordmark != null)
            return _wordmark;
        var src = Load("ark-wordmark.png");
        if (src == null)
            return null;
        try
        {
            var bgra = new FormatConvertedBitmap(src, PixelFormats.Bgra32, null, 0);
            int w = bgra.PixelWidth, h = bgra.PixelHeight, stride = w * 4;
            var px = new byte[stride * h];
            bgra.CopyPixels(px, stride, 0);
            int minX = w, minY = h, maxX = -1, maxY = -1;
            for (int y = 0; y < h; y++)
                for (int x = 0; x < w; x++)
                    if (px[y * stride + x * 4 + 3] > 24)
                    {
                        if (x < minX) minX = x;
                        if (x > maxX) maxX = x;
                        if (y < minY) minY = y;
                        if (y > maxY) maxY = y;
                    }
            if (maxX >= minX && maxY >= minY)
            {
                var cropped = new CroppedBitmap(src, new Int32Rect(minX, minY, maxX - minX + 1, maxY - minY + 1));
                cropped.Freeze();
                _wordmark = cropped;
                return _wordmark;
            }
        }
        catch (Exception e)
        {
            Log.Warn($"could not trim the wordmark: {e.Message}");
        }
        _wordmark = src;
        return _wordmark;
    }

    /// <summary>The application icon frame closest to <paramref name="pixels"/>.</summary>
    public static BitmapSource? Icon(int pixels = 32)
    {
        if (_icon != null)
            return _icon;
        try
        {
            var info = Application.GetResourceStream(new Uri("pack://application:,,,/Art/ark-app.ico"));
            if (info == null)
                return null;
            using var stream = info.Stream;
            var decoder = new IconBitmapDecoder(stream, BitmapCreateOptions.PreservePixelFormat, BitmapCacheOption.OnLoad);
            var frame = decoder.Frames.OrderBy(f => Math.Abs(f.PixelWidth - pixels)).ThenByDescending(f => f.PixelWidth).First();
            frame.Freeze();
            _icon = frame;
        }
        catch (Exception e)
        {
            Log.Warn($"icon missing: {e.Message}");
        }
        return _icon;
    }
}
