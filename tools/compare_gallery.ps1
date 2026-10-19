# compare_gallery.ps1 -- are two galleries the same to the eye?
#
# A refactor must not change what the program draws. Run the gallery
# (tools\gallery.ps1 -NoSheets) before the change, keep a copy of its folder, run
# it again after, and compare:
#
#     .\tools\compare_gallery.ps1 -Before build-gui\gallery_baseline -After build-gui\gallery
#
# Images are compared pixel by pixel, with two allowances for what is never the
# same twice: the status bar along the bottom is left out (it prints how long the
# last frame took), and a pixel only counts when a colour channel differs by more
# than -Tolerance (a modal dialog dims what is behind it by a fade that depends on
# when the frame was drawn, so the dimming differs by a few levels from run to
# run). An image differs when more than -Allow pixels count. Prints each image
# that differs, how many pixels, where, and the largest difference; the exit code
# is the number of images that differ or are missing.
#
#     -Skip        pixels ignored along the bottom edge (default 40)
#     -Tolerance   the largest channel difference that is not a difference (default 24)
#     -Allow       how many pixels may differ before an image does (default 8)

param(
    [Parameter(Mandatory = $true)][string]$Before,
    [Parameter(Mandatory = $true)][string]$After,
    [int]$Skip = 40,
    [int]$Tolerance = 24,
    [int]$Allow = 8
)

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing
Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @"
using System;
using System.Drawing;
using System.Drawing.Imaging;

public static class GalleryCompare {
    public struct Result {
        public int Width, Height, Count, MaxDelta, MinX, MinY, MaxX, MaxY;
        public bool SizeChanged;
    }

    static byte[] Read(string path, out int width, out int height) {
        using (Bitmap bitmap = new Bitmap(path)) {
            width = bitmap.Width;
            height = bitmap.Height;
            BitmapData data = bitmap.LockBits(new Rectangle(0, 0, width, height),
                ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb);
            byte[] bytes = new byte[data.Stride * height];
            System.Runtime.InteropServices.Marshal.Copy(data.Scan0, bytes, 0, bytes.Length);
            bitmap.UnlockBits(data);
            return bytes;
        }
    }

    public static Result Compare(string before, string after, int skip, int tolerance) {
        int w1, h1, w2, h2;
        byte[] a = Read(before, out w1, out h1);
        byte[] b = Read(after, out w2, out h2);
        Result r = new Result();
        r.Width = w1; r.Height = h1;
        r.MinX = w1; r.MinY = h1; r.MaxX = -1; r.MaxY = -1;
        if (w1 != w2 || h1 != h2) { r.SizeChanged = true; return r; }
        int stride = w1 * 4;
        int limit = Math.Max(0, h1 - skip);
        for (int y = 0; y < limit; y++) {
            for (int x = 0; x < w1; x++) {
                int i = y * stride + x * 4;
                int d = Math.Max(Math.Abs(a[i] - b[i]),
                        Math.Max(Math.Abs(a[i + 1] - b[i + 1]), Math.Abs(a[i + 2] - b[i + 2])));
                if (d > r.MaxDelta) r.MaxDelta = d;
                if (d > tolerance) {
                    r.Count++;
                    if (x < r.MinX) r.MinX = x; if (x > r.MaxX) r.MaxX = x;
                    if (y < r.MinY) r.MinY = y; if (y > r.MaxY) r.MaxY = y;
                }
            }
        }
        return r;
    }
}
"@

$different = 0
$missing = 0
$checked = 0
foreach ($file in Get-ChildItem $Before -Filter "*.png") {
    $other = Join-Path $After $file.Name
    if (-not (Test-Path $other)) { Write-Host "missing after: $($file.Name)"; $missing++; continue }
    $checked++
    $r = [GalleryCompare]::Compare($file.FullName, $other, $Skip, $Tolerance)
    if ($r.SizeChanged) { Write-Host "$($file.Name): size changed"; $different++; continue }
    if ($r.Count -gt $Allow) {
        Write-Host ("{0}: {1} pixels differ (largest difference {2}), within x {3}-{4}, y {5}-{6}" -f
                    $file.Name, $r.Count, $r.MaxDelta, $r.MinX, $r.MaxX, $r.MinY, $r.MaxY)
        $different++
    }
}
Write-Host ("{0} images compared, {1} differ, {2} missing" -f $checked, $different, $missing)
exit ($different + $missing)
