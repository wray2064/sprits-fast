# Selecting and moving

## Making a selection

| Tool | Key | |
|---|---|---|
| Rectangle marquee | `M` | Drag a rectangle |
| Ellipse marquee | `Shift+M` | Drag an ellipse |
| Lasso | `Q` | Draw round what you want |
| Polygon lasso | `Shift+Q` | Click the corners |
| Magic wand | `W` | Click to select the connected area of one colour |

Hold `Shift` to add to the selection, `Alt` to subtract from it, and both to keep only what
overlaps. The selection is drawn with marching ants.

**Select** menu: **All** (`Ctrl+A`), **Deselect** (`Ctrl+D`), **Reselect**, **Invert**
(`Ctrl+Shift+I`), and **Modify**, which grows, shrinks or borders the selection by any number
of pixels, with square or round corners.

While there is a selection, the pencil, eraser and fill stay inside it.

## Moving and changing what is selected

- Drag the selected pixels with the Move tool (`V`), or nudge them with the arrow keys.
- **Flip** horizontally (`Shift+H`) or vertically (`Shift+V`).
- **Edit > Rotate freely** puts the selected pixels on a layer of their own and shows a
  ring to turn them by any angle, as often as you like; **Edit > Scale freely** shows eight
  handles to size them. Neither resamples the drawing: any size or angle goes back to the
  original exactly.
- **Cut**, **Copy** and **Paste** (`Ctrl+X`, `Ctrl+C`, `Ctrl+V`). **Paste as new layer** is
  `Ctrl+Shift+V`. **Delete** removes the selected part and keeps the hole as part of what it
  cut from.
- **Edit > Fill selection** paints it in the current colour; **Stroke selection** paints a band
  the brush's width just inside its edge. Both are ordinary paint, one undo step each.
- **Brush from selection** (`Ctrl+B`) makes the selected pixels your brush.

## The clipboard works with other programs

Copies also go on the system clipboard as an image, so they paste into a browser, a chat or
another editor. An image copied anywhere else pastes into Fast as a floating piece, one
element per colour; a colour the palette already has comes in as that slot, so it recolours
with the palette straight away. Paste can also make the image a [reference](topic:references).

## Snapping

**Snap to grid** (`Shift+S`) makes selections cover whole tiles, shapes put their corners on
tile lines, and a moved selection step from tile to tile. Set the tile size under
**View > Tile grid**. See [Tiles, grids and slices](topic:tiles).
