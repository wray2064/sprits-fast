# Drawing

## The tools

| Tool | Key | What it does |
|---|---|---|
| Pencil | `B` | Freehand strokes with the current brush |
| Spray | `Shift+B` | Scattered dots inside a radius |
| Eraser | `E` | Erases; a stroke is cut where it was erased, a shape keeps the hole |
| Fill | `G` | Fills the connected area you click |
| Gradient | `Shift+G` | Fills the area a fill would find (or the selection) with a dithered gradient from the first colour to the second, along your drag |
| Pick colour | `I` | Takes the colour under the pointer |
| Rectangle | `R` | Drag a rectangle; stays editable |
| Ellipse | `U` | Drag an ellipse; stays editable |
| Line | `L` | Drag a line; `Shift` snaps to 15 degrees |
| Contour | `D` | Fills inside a closed outline you draw |
| Polygon | `Shift+D` | Click corner by corner |
| Curve | `Shift+L` | Click to place points, drag as you place one to pull out handles |
| Text | `Shift+T` | Click, then type |
| Slice | `C` | Draws a named rectangle for exports |
| Move | `V` | Moves the layer or selection; shows the rotate and scale gizmo |
| Hand | `H` | Pans (or hold `Space`) |
| Zoom | `Z` | Click to zoom in, `Alt`+click to zoom out |

The selection tools are in [Selecting and moving](topic:selecting).

## The brush

The **Tool** panel on the left shows the options of the tool in hand. For the pencil, spray
and eraser:

- **Size**, and **Round** or **Square**. `Shift+]` and `Shift+[` make the brush larger or smaller.
- **Pixel-perfect** keeps a diagonal one pixel wide, with no doubled corners.
- **Stabiliser** smooths a shaky hand by following the pointer a little behind.
- A **custom brush**: select pixels and press `Ctrl+B`.
- A **pen tablet** works as it is: pressure drives the size, and the eraser end erases.

Hold `Shift` and click to draw a straight line from the last stroke. Right-click draws with
the second colour; `X` swaps the two.

## Inks

The pencil and spray have four **inks**: what a stroke does to what is under it.

- **Simple** paints every pixel the brush covers.
- **Lock alpha** paints only where something is already drawn.
- **Replace colour** paints only over the second colour.
- **Shading** steps each pixel to the neighbouring slot of the palette, so a shaded area
  still follows the palette.

Each ink is part of the stroke, so it turns and scales with the drawing.

## Symmetry

**View > Symmetry across** and **Symmetry down** mirror what you draw about axes that start
in the middle of the canvas. Drag an axis to move it.

## Shapes that stay shapes

A rectangle, ellipse, line, polygon or curve keeps its parameters. Pick it in the
**Elements** panel and its handles appear on the canvas: drag a corner to resize, drag a
point to reshape. **Properties** shows its numbers, rounding, outline and shadow, and
changes them without redrawing anything. `Shift` keeps a rectangle square and an ellipse round.

> Drawing a shape on a layer does not touch what was there. Painting over a colour lays
> the new colour on top, and removing the top element in the Elements panel brings the
> old one back exactly as it was.

## Text

Choose the Text tool, click where it should go and type. The result is an element made of
words, a place and a size: select it in the Elements panel and edit the text in
**Properties**, and the pixels are rebuilt. The face is Fast's own five-by-seven pixel font.

## Outlines, shadows and dither

In **Properties**, **Outline** draws a line around what a layer draws, or around the whole
figure across layers, with no seam where parts meet; **Drop shadow** casts a shadow from
the layer or the whole figure. Both are generated from the drawing as it is now, so they
follow any change.

**Dithered fill** turns a layer's colour into a dither between two colours: constant
density, or a linear, radial or angular gradient, with twelve patterns. The **anchor**
decides what the pattern does when the artwork turns: Local turns with it, Global stays
level with the canvas, and Fixed pins it to the canvas as the artwork moves.

## Next

[Colour and palettes](topic:colour), [Layers and elements](topic:layers).
