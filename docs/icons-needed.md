# Icons to source

Every icon in Fast is currently drawn from primitives in `src/ui/theme.cpp`
(`theme::drawIcon`). They are placeholders: legible, but made from lines and
rectangles, not designed.
This list says what a real set needs to cover, so the whole set can be
commissioned or chosen at once rather than piecemeal.

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

## In use now, as placeholders

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
| App icon (window and file association) | OS | needed before any release |
| `.lsprite` document icon | OS file association | needed before any release |
