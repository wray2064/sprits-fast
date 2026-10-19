# gallery.ps1 -- every screen of the program, in every built-in theme, at 100%
# and 150% interface size, as images, and each screen's themes tiled into one
# contact sheet, so that a change of look is judged by eye in one pass.
#
# From the project folder, after .\build-gui.bat:
#
#     .\tools\gallery.ps1
#
# It runs tests\gallery\gallery.txt (a UI script: build-gui\sprits_fast.exe
# --script ...) from a folder of its own, which collects THEME_SIZE_SCREEN.png,
# then writes the sheets to <out>\sheets\sheet_SIZE_SCREEN.png and an
# index.html that shows them all on one page. The sheets need Windows
# PowerShell (System.Drawing); the script itself runs anywhere the program does.
#
#     -Exe     the program (default build-gui\sprits_fast.exe)
#     -Out     where the images go (default build-gui\gallery, emptied first)
#     -NoSheets   the images only

param(
    [string]$Exe = "build-gui\sprits_fast.exe",
    [string]$Out = "build-gui\gallery",
    [switch]$NoSheets
)

$ErrorActionPreference = "Stop"
$project = Split-Path -Parent $PSScriptRoot
Set-Location $project

$exePath = (Resolve-Path $Exe).Path
$scriptPath = (Resolve-Path "tests\gallery\gallery.txt").Path
if (Test-Path $Out) { Remove-Item -Recurse -Force $Out }
New-Item -ItemType Directory -Force $Out | Out-Null
$outPath = (Resolve-Path $Out).Path

# The program writes each image into the folder it runs in.
Push-Location $outPath
try {
    $log = & $exePath --script $scriptPath 2>&1 | Out-String
    $code = $LASTEXITCODE
} finally {
    Pop-Location
}
$summary = ($log -split "`n" | Where-Object { $_ -match "check\(s\)" } | Select-Object -Last 1)
$problems = @($log -split "`n" | Where-Object { $_ -match "^FAIL|could not" })
$images = @(Get-ChildItem $outPath -Filter "*.png")
Write-Host ("{0} images in {1}. {2}" -f $images.Count, $outPath, $summary.Trim())
if ($problems.Count -gt 0) {
    $problems | Group-Object | Sort-Object Count -Descending | ForEach-Object {
        Write-Host ("  {0,3} x {1}" -f $_.Count, $_.Name)
    }
}
if ($code -ne 0) { throw "the gallery script failed (exit $code)" }
if ($NoSheets) { return }

# ------------------------------------------------------------------ the sheets --
Add-Type -AssemblyName System.Drawing
$pattern = '^(?<theme>.+)_(?<scale>\d+)_(?<screen>.+)\.png$'
$parsed = foreach ($file in $images) {
    if ($file.Name -match $pattern) {
        [pscustomobject]@{ File = $file; Theme = $Matches.theme; Scale = $Matches.scale; Screen = $Matches.screen }
    }
}
# Themes in the order the script ran them: the order the chooser lists them.
$themes = @($parsed | Where-Object { $_.Scale -eq "100" -and $_.Screen -eq "drawing" } |
            Sort-Object { $_.File.LastWriteTime } | ForEach-Object { $_.Theme })
$scales = @($parsed | ForEach-Object { $_.Scale } | Sort-Object { [int]$_ } -Unique)
$screens = @($parsed | ForEach-Object { $_.Screen } | Sort-Object -Unique)

$sheets = Join-Path $outPath "sheets"
New-Item -ItemType Directory -Force $sheets | Out-Null
$columns = 3
$tileW = 640
$tileH = 400
$gap = 8
$rows = [int][Math]::Ceiling($themes.Count / $columns)
$font = New-Object System.Drawing.Font("Segoe UI", 13, [System.Drawing.FontStyle]::Bold)
$made = @()
foreach ($scale in $scales) {
    foreach ($screen in $screens) {
        $sheet = New-Object System.Drawing.Bitmap(($columns * $tileW + ($columns + 1) * $gap), ($rows * $tileH + ($rows + 1) * $gap))
        $g = [System.Drawing.Graphics]::FromImage($sheet)
        $g.Clear([System.Drawing.Color]::FromArgb(128, 128, 128))
        $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
        for ($i = 0; $i -lt $themes.Count; $i++) {
            $name = "{0}_{1}_{2}.png" -f $themes[$i], $scale, $screen
            $path = Join-Path $outPath $name
            if (-not (Test-Path $path)) { continue }
            $x = $gap + ($i % $columns) * ($tileW + $gap)
            $y = $gap + [Math]::Floor($i / $columns) * ($tileH + $gap)
            $image = [System.Drawing.Image]::FromFile($path)
            $g.DrawImage($image, $x, $y, $tileW, $tileH)
            $image.Dispose()
            # The name goes over the status bar, at the bottom, where it hides nothing
            # that is being judged (the menu bar is at the top).
            $label = $themes[$i]
            $size = $g.MeasureString($label, $font)
            $top = $y + $tileH - [int]$size.Height - 2
            $g.FillRectangle([System.Drawing.Brushes]::Black, $x, $top, [int]$size.Width + 8, [int]$size.Height + 2)
            $g.DrawString($label, $font, [System.Drawing.Brushes]::White, $x + 4, $top + 1)
        }
        $g.Dispose()
        $target = Join-Path $sheets ("sheet_{0}_{1}.png" -f $scale, $screen)
        $sheet.Save($target, [System.Drawing.Imaging.ImageFormat]::Png)
        $sheet.Dispose()
        $made += [pscustomobject]@{ Scale = $scale; Screen = $screen; Name = (Split-Path -Leaf $target) }
    }
}

$html = New-Object System.Text.StringBuilder
[void]$html.AppendLine("<!doctype html><meta charset=utf-8><title>Gallery</title>")
[void]$html.AppendLine("<style>body{font:14px Segoe UI,sans-serif;background:#222;color:#ddd;margin:16px}img{max-width:100%;display:block;margin:4px 0 24px}h2{margin:24px 0 0}</style>")
foreach ($sheet in $made) {
    [void]$html.AppendLine(("<h2>{0} at {1}%</h2><img src='sheets/{2}'>" -f $sheet.Screen, $sheet.Scale, $sheet.Name))
}
Set-Content -Path (Join-Path $outPath "index.html") -Value $html.ToString() -Encoding UTF8
Write-Host ("{0} sheets in {1}; open {2}" -f $made.Count, $sheets, (Join-Path $outPath "index.html"))
