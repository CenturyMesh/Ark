param([Parameter(Mandatory=$true)][string]$OutputDirectory)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$repository = Split-Path $PSScriptRoot -Parent
[IO.Directory]::CreateDirectory($OutputDirectory) | Out-Null
# Flatten the existing transparent application artwork onto the welcome-page
# colour. The source mark and its proportions remain unchanged.
$source = New-Object Drawing.Bitmap (Join-Path $repository 'assets/ui/ark-app-mark.png')
$bitmap = New-Object Drawing.Bitmap 702,468
$graphics = [Drawing.Graphics]::FromImage($bitmap)
$small = New-Object Drawing.Bitmap 165,165
$smallGraphics = [Drawing.Graphics]::FromImage($small)
try {
    $graphics.Clear([Drawing.Color]::FromArgb(13,23,36))
    $graphics.InterpolationMode = [Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $graphics.DrawImage($source, (New-Object Drawing.Rectangle 0,0,702,468))
    $bitmap.Save((Join-Path $OutputDirectory 'ark-setup-mark.bmp'), [Drawing.Imaging.ImageFormat]::Bmp)
    $smallGraphics.Clear([Drawing.Color]::White)
    $smallGraphics.InterpolationMode = [Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $smallGraphics.DrawImage($source, (New-Object Drawing.Rectangle 0,27,165,110))
    $small.Save((Join-Path $OutputDirectory 'ark-setup-small.bmp'), [Drawing.Imaging.ImageFormat]::Bmp)
} finally {
    $smallGraphics.Dispose()
    $small.Dispose()
    $graphics.Dispose()
    $bitmap.Dispose()
    $source.Dispose()
}
