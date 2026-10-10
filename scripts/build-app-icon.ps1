param()
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
# Package the generated transparent mark into Windows' standard icon sizes.
# Preserve the original artwork and alpha; optically increase its height for the shell.
Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @'
using System;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.IO;
public static class ArkIconPackager {
    private static byte[] FrameBytes(Bitmap frame) {
        using (var stream=new MemoryStream()) {
            if (frame.Width==256) { frame.Save(stream,ImageFormat.Png); return stream.ToArray(); }
            // Native DIB frames avoid an extra PNG decode/scaling path in shell consumers.
            int size=frame.Width, maskStride=((size+31)/32)*4;
            using (var writer=new BinaryWriter(stream)) {
                writer.Write(40); writer.Write(size); writer.Write(size*2);
                writer.Write((ushort)1); writer.Write((ushort)32); writer.Write(0);
                writer.Write(size*size*4+maskStride*size);
                writer.Write(0); writer.Write(0); writer.Write(0); writer.Write(0);
                for (int y=size-1;y>=0;--y) for (int x=0;x<size;++x) {
                    var c=frame.GetPixel(x,y);
                    writer.Write(c.B); writer.Write(c.G); writer.Write(c.R); writer.Write(c.A);
                }
                for (int y=size-1;y>=0;--y) {
                    var mask=new byte[maskStride];
                    for (int x=0;x<size;++x) if(frame.GetPixel(x,y).A==0) mask[x/8]|=(byte)(0x80>>(x%8));
                    writer.Write(mask);
                }
                return stream.ToArray();
            }
        }
    }
    public static void Write(string source, string destination) {
        // Include exact Windows taskbar targets at 100/125/150/200/250/300/400% DPI.
        int[] sizes = {16,20,24,30,32,36,40,48,60,64,72,96,128,256};
        byte[][] frames = new byte[sizes.Length][];
        using (var mark = new Bitmap(source)) {
            int left=mark.Width, top=mark.Height, right=-1, bottom=-1;
            for (int y=0;y<mark.Height;y++) for (int x=0;x<mark.Width;x++)
                // Near-invisible generated fringe must not shrink the actual mark.
                if (mark.GetPixel(x,y).A>16) { left=Math.Min(left,x); top=Math.Min(top,y); right=Math.Max(right,x); bottom=Math.Max(bottom,y); }
            if (right<left) throw new InvalidDataException("Empty icon alpha");
            var crop=new Rectangle(left,top,right-left+1,bottom-top+1);
            for (int i=0;i<sizes.Length;i++) {
                int size=sizes[i];
                using (var frame=new Bitmap(size,size,PixelFormat.Format32bppArgb)) {
                    using (var g=Graphics.FromImage(frame)) {
                        g.Clear(Color.Transparent);
                        g.CompositingMode=CompositingMode.SourceCopy;
                        g.InterpolationMode=InterpolationMode.HighQualityBicubic;
                        g.PixelOffsetMode=PixelOffsetMode.HighQuality;
                        // User-approved optical fit: the wide original remains the source,
                        // but fills 86% of the icon height instead of appearing half-sized.
                        float width=size, height=size*.86f;
                        g.DrawImage(mark,new RectangleF((size-width)/2,(size-height)/2,width,height),crop,GraphicsUnit.Pixel);
                    }
                    frames[i]=FrameBytes(frame);
                }
            }
        }
        using (var writer=new BinaryWriter(File.Create(destination))) {
            writer.Write((ushort)0); writer.Write((ushort)1); writer.Write((ushort)sizes.Length);
            int offset=6+16*sizes.Length;
            for (int i=0;i<sizes.Length;i++) {
                writer.Write((byte)(sizes[i]==256?0:sizes[i])); writer.Write((byte)(sizes[i]==256?0:sizes[i]));
                writer.Write((byte)0); writer.Write((byte)0); writer.Write((ushort)1); writer.Write((ushort)32);
                writer.Write(frames[i].Length); writer.Write(offset); offset+=frames[i].Length;
            }
            foreach (var frame in frames) writer.Write(frame);
        }
    }
}
'@
$iconRoot = Split-Path $PSScriptRoot -Parent
[ArkIconPackager]::Write((Join-Path $iconRoot 'assets/ui/ark-app-mark.png'), (Join-Path $iconRoot 'assets/ui/ark-app.ico'))
