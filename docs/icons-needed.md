# Icons to source

**Status (2026-10-18, UI overhaul stage 5): the set is chosen.** Every tool icon
and the eye pair are **Lucide** 1.59.0 (ISC; the few it took from Feather are
MIT), 24 SVGs in `assets/icons/lucide`, whose README gives the source, hashes
and licence. They are parsed into strokes (`app/vector_icon`) and drawn through
the one `theme::drawIcon` call, so the hand-drawn placeholders are gone. What
is left below is the icons that have no widget yet: take each from the same
release, add its file and a row to that README, and add it to the `Icon` enum
in `ui/theme.h`, `kIconSources` in `ui/theme.cpp` and `FAST_ICONS` in
`CMakeLists.txt`. The constraints below are what the choice was made against.

**Constraints on whatever is sourced**

- Licence: something that imposes nothing on a fork of an Apache-2.0 project --
  CC0, MIT, Apache-2.0, or commissioned outright. Not CC-BY-SA, not GPL, not
  "free for non-commercial use".
- Format: SVG masters, rendered at build time or shipped as a small atlas at
  1x and 2x. Monochrome, drawn as an alpha mask, so the theme can tint them
  (the selected tool takes the accent colour; everything else is a dim
  neutral).
- Size: designed on a 16 px grid, shown at 16-20 px in the toolbar and 12-14 px
  inline (eyes, locks, small buttons).
- Style: one stroke weight across the set. Pixel-art tools look best with
  crisp 1.5 px strokes at 16 px, not filled glyphs.

## In use now (Lucide)

The tool icons and the eye pair below, and the toolbar entries of the next
table (marquees, lassos, wand, move, spray, gradient, text, hand, zoom, curve,
contour, polygon, slice), are the Lucide files. Names in the repository: pencil, eraser, paint-bucket, pipette, square, circle, slash, eye,
eye-off, square-dashed, circle-dashed, lasso, wand, move, spray-can, spline,
hand, zoom-in, blend, type, lasso-select, pentagon, pen-tool, slice.

| Icon | Where | Notes |
|---|---|---|
| Pencil | Toolbar | `B` |
| Eraser | Toolbar | `E` |
| Bucket | Toolbar | `G` |
| Eyedropper | Toolbar | `I` |
| Rectangle | Toolbar | `R` |
| Ellipse | Toolbar | `U` |
| Line | Toolbar | `L` |
| Eye open / shut | Layer rows, reference rows | visibility |

## Needed as parity work lands

Added here as each tool is built; the placeholder is drawn in the meantime.

| Icon | Where | Status |
|---|---|---|
| Rectangle marquee | Toolbar | planned |
| Ellipse marquee | Toolbar | planned |
| Lasso | Toolbar | planned |
| Polygon lasso | Toolbar | planned |
| Magic wand | Toolbar | planned |
| Move (four-way arrow) | Toolbar | planned |
| Gradient | Toolbar | planned |
| Spray / airbrush | Toolbar | planned |
| Text (a "T") | Toolbar | planned |
| Hand | Toolbar | planned |
| Zoom (magnifier) | Toolbar | planned |
| Curve (an S with a handle drawn out) | Toolbar | placeholder drawn |
| Contour | Toolbar | placeholder drawn |
| Polygon (a filled polygon, corners marked) | Toolbar | placeholder drawn |
| Slice (a frame with a name tab) | Toolbar | placeholder drawn |
| Shape handles: square anchor, round control point | Canvas | drawn; fine as they are |
| Shading ink | Tool options | planned |
| Symmetry: horizontal, vertical, both | Tool options / view | planned |
| Swap colours (two arrows) | Colour section | planned; now a click on the second swatch |
| Lock / unlock | Layer rows | planned; now a checkbox |
| Link (linked cels) | Timeline | planned |
| Play, pause, step back, step forward, first, last | Timeline | planned; now text buttons |
| Onion skin | Timeline | planned; now a checkbox |
| Add frame, duplicate frame, delete | Timeline | planned; now text buttons |
| New layer, new group, delete layer | Layer panel | planned; now text buttons |
| App icon (window and executable) | OS | **done**: from the logo (`assets/icon/logo.png`, `tools/make_icons.py`); the window's at 256 and 32, the executable's `.ico` 16-256 on Windows. Still to do: a macOS `.icns` and a Linux `.desktop` entry, with packaging |
| `.lsprite` document icon | OS file association | needed before any release |
