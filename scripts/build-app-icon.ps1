param()
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
# Package the generated transparent mark into Windows' standard icon sizes.
# Preserve its alpha and aspect ratio; do not matte it onto a background.
Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @'
using System;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.IO;
public static class ArkIconPackager {
    public static void Write(string source, string destination) {
        int[] sizes = {16,20,24,32,40,48,64,128,256};
        byte[][] frames = new byte[sizes.Length][];
        using (var mark = new Bitmap(source)) {
            int left=mark.Width, top=mark.Height, right=-1, bottom=-1;
            for (int y=0;y<mark.Height;y++) for (int x=0;x<mark.Width;x++)
                if (mark.GetPixel(x,y).A>0) { left=Math.Min(left,x); top=Math.Min(top,y); right=Math.Max(right,x); bottom=Math.Max(bottom,y); }
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
                        float scale=(float)size/Math.Max(crop.Width,crop.Height);
                        float width=crop.Width*scale, height=crop.Height*scale;
                        g.DrawImage(mark,new RectangleF((size-width)/2,(size-height)/2,width,height),crop,GraphicsUnit.Pixel);
                    }
                    using (var stream=new MemoryStream()) { frame.Save(stream,ImageFormat.Png); frames[i]=stream.ToArray(); }
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
