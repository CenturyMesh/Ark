using System.IO;
using System.IO.Compression;
using System.Security.Cryptography;
using System.Text;
using Microsoft.Win32.SafeHandles;

namespace ArkSetup.Core;

/// <summary>
/// ARK travels inside the setup exe: scripts/build-installer.ps1 appends the portable package
/// (ark-windows-x64-&lt;version&gt;.zip, unchanged) after the published .NET single-file exe ("stub"), then a 40-byte footer:
///
///     "ARKPAYL2" (8 ASCII bytes) | payload offset (int64) | payload length (int64)
///     | stub PE CheckSum (uint32) | stub certificate table offset (uint32) | stub certificate table size (uint32) | 0 (uint32)
///
/// all little endian. The .NET single-file host finds its own bundle through an offset stamped into the exe, so bytes
/// appended at the end do not disturb it. A signed setup is signed after the append (Authenticode then also covers the
/// package): its certificate table follows the footer, after up to 7 zero bytes of alignment, and the PE security
/// directory points at it. The bytes before the payload offset are the stub; uninstall.exe is that stub with the
/// CheckSum and security directory the stub had when it was published (and, in a signed build, signed on its own),
/// so it is byte for byte the separately built (and signed) uninstaller. See docs/WINDOWS_INSTALLER.md.
///
/// Everything is read through one handle, opened once and held for the life of the process with no write sharing:
/// the footer, the digest check, the extraction and uninstall.exe come from the same file even if the exe is renamed
/// and another file put at its path meanwhile.
/// </summary>
public sealed class Payload
{
    public const string Magic = "ARKPAYL2";
    public const int FooterSize = 40;

    public string ExePath { get; }
    public long Offset { get; }
    public long Length { get; }
    readonly SafeFileHandle _file;
    readonly PeLayout _pe;
    readonly uint _stubChecksum, _stubCertOffset, _stubCertSize;

    /// <summary>True when the setup itself carries an Authenticode certificate table.</summary>
    public bool Signed => _pe.CertSize != 0;

    /// <summary>True when the stub (uninstall.exe) was signed on its own before the package was appended.</summary>
    public bool StubSigned => _stubCertSize != 0;

    Payload(string exePath, SafeFileHandle file, long offset, long length, PeLayout pe, uint stubChecksum, uint stubCertOffset, uint stubCertSize)
    {
        ExePath = exePath;
        _file = file;
        Offset = offset;
        Length = length;
        _pe = pe;
        _stubChecksum = stubChecksum;
        _stubCertOffset = stubCertOffset;
        _stubCertSize = stubCertSize;
    }

    /// <summary>
    /// Finds the footer of <paramref name="exePath"/>; null when the file carries no payload. The returned payload keeps
    /// the file open (shared for reading and deletion only) until the process exits.
    /// </summary>
    public static Payload? TryOpen(string? exePath)
    {
        if (string.IsNullOrEmpty(exePath) || !File.Exists(exePath))
            return null;
        SafeFileHandle? file = null;
        try
        {
            file = File.OpenHandle(exePath, FileMode.Open, FileAccess.Read, FileShare.Read | FileShare.Delete, FileOptions.RandomAccess);
            var payload = Find(file, exePath);
            if (payload == null)
                file.Dispose();
            return payload;
        }
        catch (Exception e)
        {
            file?.Dispose();
            Log.Error("could not read the payload footer", e);
            return null;
        }
    }

    static Payload? Find(SafeFileHandle file, string exePath)
    {
        long size = RandomAccess.GetLength(file);
        if (PeLayout.Read(new SubStream(file, 0, size)) is not PeLayout pe)
            return null;
        // Authenticode puts its certificate table at the end of the file: the footer sits right before it.
        long end = size;
        if (pe.CertSize != 0)
        {
            if (pe.CertOffset % 8 != 0 || pe.CertOffset + (long)pe.CertSize != size)
            {
                Log.Warn($"the certificate table ({pe.CertOffset}, {pe.CertSize}) is not at the end of the file ({size}); no payload");
                return null;
            }
            end = pe.CertOffset;
        }
        int window = FooterSize + 7;
        if (end < window + pe.HeaderEnd)
            return null;
        var tail = new byte[window];
        ReadAt(file, end - window, tail);
        // signing pads the signed bytes to a multiple of 8 with zeros before the table
        for (int pad = 0; pad <= 7; pad++)
        {
            if (pad > 0 && tail[window - pad] != 0)
                break;
            if (pad > 0 && pe.CertSize == 0)
                break; // only a signature adds padding
            int at = window - pad - FooterSize;
            if (Encoding.ASCII.GetString(tail, at, 8) != Magic)
                continue;
            long offset = BitConverter.ToInt64(tail, at + 8);
            long length = BitConverter.ToInt64(tail, at + 16);
            uint checksum = BitConverter.ToUInt32(tail, at + 24);
            uint certOffset = BitConverter.ToUInt32(tail, at + 28);
            uint certSize = BitConverter.ToUInt32(tail, at + 32);
            uint reserved = BitConverter.ToUInt32(tail, at + 36);
            long footerStart = end - pad - FooterSize;
            bool stubOk = certSize == 0 ? certOffset == 0 : certOffset % 8 == 0 && certOffset >= pe.HeaderEnd && certOffset + (long)certSize == offset;
            if (offset < pe.HeaderEnd || length <= 0 || offset + length != footerStart || reserved != 0 || !stubOk)
            {
                Log.Warn($"payload footer found but inconsistent (offset {offset}, length {length}, stub certificate {certOffset}+{certSize}, footer at {footerStart})");
                return null;
            }
            return new Payload(exePath, file, offset, length, pe, checksum, certOffset, certSize);
        }
        return null;
    }

    /// <summary>Reads exactly <paramref name="buffer"/>.Length bytes at <paramref name="offset"/>.</summary>
    static void ReadAt(SafeFileHandle file, long offset, Span<byte> buffer)
    {
        while (buffer.Length > 0)
        {
            int n = RandomAccess.Read(file, buffer, offset);
            if (n <= 0)
                throw new EndOfStreamException("setup exe ended early");
            buffer = buffer[n..];
            offset += n;
        }
    }

    /// <summary>Opens the zip inside the exe (over the payload's own handle, which the archive does not close).</summary>
    public ZipArchive OpenArchive() =>
        new ZipArchive(new SubStream(_file, Offset, Length), ZipArchiveMode.Read, leaveOpen: false);

    /// <summary>Total uncompressed size of the package, for the disk space check.</summary>
    public long UncompressedSize()
    {
        using var zip = OpenArchive();
        long total = 0;
        foreach (var e in zip.Entries)
            total += e.Length;
        return total;
    }

    /// <summary>
    /// SHA-256 of the package bytes as stored in this exe (lower-case hex). They are read through the same handle as
    /// <see cref="OpenArchive"/>, and no other handle may write to the file while it is open (the running exe is also
    /// mapped as an image): the bytes hashed here are the bytes extracted afterwards.
    /// </summary>
    public string Sha256(IProgress<long>? progress, CancellationToken ct)
    {
        using var sha = SHA256.Create();
        var buffer = new byte[1 << 20];
        long done = 0;
        while (done < Length)
        {
            ct.ThrowIfCancellationRequested();
            int n = RandomAccess.Read(_file, buffer.AsSpan(0, (int)Math.Min(buffer.Length, Length - done)), Offset + done);
            if (n <= 0)
                throw new EndOfStreamException("setup exe ended inside the payload");
            sha.TransformBlock(buffer, 0, n, null, 0);
            done += n;
            progress?.Report(done);
        }
        sha.TransformFinalBlock(Array.Empty<byte>(), 0, 0);
        return Convert.ToHexString(sha.Hash!).ToLowerInvariant();
    }

    /// <summary>
    /// Writes the installer without its payload (used as uninstall.exe): the stub bytes, with the PE CheckSum and
    /// security directory restored to the stub's own, so a separately signed stub keeps its own valid signature and an
    /// unsigned one never points at the setup's certificate table.
    /// </summary>
    public void WriteStub(string destination)
    {
        using var dst = new FileStream(destination, FileMode.Create, FileAccess.Write, FileShare.None);
        var buffer = new byte[1 << 20];
        long done = 0;
        while (done < Offset)
        {
            int n = RandomAccess.Read(_file, buffer.AsSpan(0, (int)Math.Min(buffer.Length, Offset - done)), done);
            if (n <= 0)
                throw new EndOfStreamException("setup exe ended before the payload offset");
            if (done == 0)
            {
                if (n < _pe.HeaderEnd)
                    throw new InvalidDataException("short read of the PE header");
                BitConverter.TryWriteBytes(buffer.AsSpan((int)_pe.ChecksumPos, 4), _stubChecksum);
                BitConverter.TryWriteBytes(buffer.AsSpan((int)_pe.SecurityDirPos, 4), _stubCertOffset);
                BitConverter.TryWriteBytes(buffer.AsSpan((int)_pe.SecurityDirPos + 4, 4), _stubCertSize);
            }
            dst.Write(buffer, 0, n);
            done += n;
        }
    }
}

/// <summary>
/// The PE header fields Authenticode signing changes: the optional header CheckSum and the security (certificate table)
/// data directory, whose "virtual address" is a file offset.
/// </summary>
public readonly record struct PeLayout(long ChecksumPos, long SecurityDirPos, uint Checksum, uint CertOffset, uint CertSize)
{
    /// <summary>End of the header fields Setup reads or patches.</summary>
    public long HeaderEnd => SecurityDirPos + 8;

    public static PeLayout? Read(Stream s)
    {
        var head = new byte[4096];
        s.Seek(0, SeekOrigin.Begin);
        int n = s.ReadAtLeast(head, head.Length, throwOnEndOfStream: false);
        if (n < 0x40 || head[0] != (byte)'M' || head[1] != (byte)'Z')
            return null;
        int pe = BitConverter.ToInt32(head, 0x3C);
        if (pe < 0x40 || pe > n - 24 - 2 || head[pe] != (byte)'P' || head[pe + 1] != (byte)'E' || head[pe + 2] != 0 || head[pe + 3] != 0)
            return null;
        int opt = pe + 24;
        int optSize = BitConverter.ToUInt16(head, pe + 20);
        int dirs = BitConverter.ToUInt16(head, opt) switch { 0x10B => opt + 96, 0x20B => opt + 112, _ => -1 };
        if (dirs < 0 || dirs + 5 * 8 > opt + optSize || dirs + 5 * 8 > n || BitConverter.ToUInt32(head, dirs - 4) < 5)
            return null;
        int checksum = opt + 64;
        int security = dirs + 4 * 8;
        return new PeLayout(checksum, security, BitConverter.ToUInt32(head, checksum), BitConverter.ToUInt32(head, security), BitConverter.ToUInt32(head, security + 4));
    }
}

/// <summary>
/// A read-only, seekable window [offset, offset + length) over a file handle. Reads are positional, so several windows
/// can share the handle (also from different threads); disposing a window leaves the handle open.
/// </summary>
public sealed class SubStream : Stream
{
    readonly SafeFileHandle _file;
    readonly long _start;
    readonly long _length;
    long _pos;

    public SubStream(SafeFileHandle file, long start, long length)
    {
        _file = file;
        _start = start;
        _length = length;
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
        int n = RandomAccess.Read(_file, buffer, _start + _pos);
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
}
