using System.IO;
using System.IO.Compression;
using System.Text;

namespace ArkSetup.Core;

/// <summary>
/// ARK travels inside the setup exe: scripts/build-installer.ps1 appends the portable package
/// (ark-windows-x64-&lt;version&gt;.zip, unchanged) after the published .NET single-file exe, then a 24-byte footer:
///
///     "ARKPAYLD" (8 ASCII bytes) | payload offset (int64, little endian) | payload length (int64, little endian)
///
/// The .NET single-file host finds its own bundle through an offset stamped into the exe, so bytes appended
/// at the end do not disturb it. The bytes before the payload offset are the bare installer ("stub"), which is
/// what gets copied to the install folder as uninstall.exe.
/// </summary>
public sealed class Payload
{
    public const string Magic = "ARKPAYLD";
    public const int FooterSize = 24;

    public string ExePath { get; }
    public long Offset { get; }
    public long Length { get; }

    Payload(string exePath, long offset, long length)
    {
        ExePath = exePath;
        Offset = offset;
        Length = length;
    }

    /// <summary>Reads the footer of <paramref name="exePath"/>; null when the file carries no payload.</summary>
    public static Payload? TryOpen(string? exePath)
    {
        if (string.IsNullOrEmpty(exePath) || !File.Exists(exePath))
            return null;
        try
        {
            using var fs = new FileStream(exePath, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete);
            if (fs.Length < FooterSize + 22)
                return null;
            fs.Seek(-FooterSize, SeekOrigin.End);
            var footer = new byte[FooterSize];
            fs.ReadExactly(footer);
            if (Encoding.ASCII.GetString(footer, 0, 8) != Magic)
                return null;
            long offset = BitConverter.ToInt64(footer, 8);
            long length = BitConverter.ToInt64(footer, 16);
            if (offset <= 0 || length <= 0 || offset + length != fs.Length - FooterSize)
            {
                Log.Warn($"payload footer found but inconsistent (offset {offset}, length {length}, file {fs.Length})");
                return null;
            }
            return new Payload(exePath, offset, length);
        }
        catch (Exception e)
        {
            Log.Error("could not read the payload footer", e);
            return null;
        }
    }

    /// <summary>Opens the zip inside the exe. Dispose the archive when done (it owns the file handle).</summary>
    public ZipArchive OpenArchive()
    {
        var fs = new FileStream(ExePath, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete,
                                1 << 16, FileOptions.RandomAccess);
        return new ZipArchive(new SubStream(fs, Offset, Length), ZipArchiveMode.Read, leaveOpen: false);
    }

    /// <summary>Total uncompressed size of the package, for the disk space check.</summary>
    public long UncompressedSize()
    {
        using var zip = OpenArchive();
        long total = 0;
        foreach (var e in zip.Entries)
            total += e.Length;
        return total;
    }

    /// <summary>Writes the installer without its payload (used as uninstall.exe).</summary>
    public void WriteStub(string destination)
    {
        using var src = new FileStream(ExePath, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete);
        using var dst = new FileStream(destination, FileMode.Create, FileAccess.Write, FileShare.None);
        var buffer = new byte[1 << 20];
        long left = Offset;
        while (left > 0)
        {
            int n = src.Read(buffer, 0, (int)Math.Min(buffer.Length, left));
            if (n <= 0)
                throw new EndOfStreamException("setup exe ended before the payload offset");
            dst.Write(buffer, 0, n);
            left -= n;
        }
    }
}

/// <summary>A read-only, seekable window [offset, offset + length) over another stream.</summary>
public sealed class SubStream : Stream
{
    readonly Stream _inner;
    readonly long _start;
    readonly long _length;
    long _pos;

    public SubStream(Stream inner, long start, long length)
    {
        _inner = inner;
        _start = start;
        _length = length;
        _inner.Seek(start, SeekOrigin.Begin);
    }

    public override bool CanRead => true;
    public override bool CanSeek => true;
    public override bool CanWrite => false;
    public override long Length => _length;

    public override long Position
    {
        get => _pos;
        set => Seek(value, SeekOrigin.Begin);
    }

    public override int Read(byte[] buffer, int offset, int count) => Read(buffer.AsSpan(offset, count));

    public override int Read(Span<byte> buffer)
    {
        long left = _length - _pos;
        if (left <= 0)
            return 0;
        if (buffer.Length > left)
            buffer = buffer[..(int)left];
        if (_inner.Position != _start + _pos)
            _inner.Seek(_start + _pos, SeekOrigin.Begin);
        int n = _inner.Read(buffer);
        _pos += n;
        return n;
    }

    public override long Seek(long offset, SeekOrigin origin)
    {
        long target = origin switch
        {
            SeekOrigin.Begin => offset,
            SeekOrigin.Current => _pos + offset,
            _ => _length + offset,
        };
        if (target < 0)
            throw new IOException("seek before the start of the payload");
        _pos = target;
        return _pos;
    }

    public override void Flush() { }
    public override void SetLength(long value) => throw new NotSupportedException();
    public override void Write(byte[] buffer, int offset, int count) => throw new NotSupportedException();

    protected override void Dispose(bool disposing)
    {
        if (disposing)
            _inner.Dispose();
        base.Dispose(disposing);
    }
}
