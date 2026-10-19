# Tiles, grids and slices

## Tiled mode

**View > Tiled mode** draws the canvas again beside itself, across, down or both ways, so the seams of a
repeating tile show while you draw it. A stroke that runs off one edge comes back on the opposite edge.

## The tile grid, and snapping

**View > Tile grid** lays a grid of any size and offset over the pixel grid. **Isometric** makes it a grid of
2:1 diamonds. **Snap to grid** (`Shift+S`) then makes selections cover whole tiles, shapes put their corners on
tile lines, and a moved selection step from tile to tile.

**View > Pixel grid** shows the pixel grid itself; its colour and opacity are in Preferences. **Rulers** run
along the canvas, and **guides** dragged out of them stay with the document, turn with the canvas and are
snapped to too (**View > Clear guides** removes them).

## Tilemap layers

**Sprite > New tilemap layer...** adds a grid of tiles over the canvas. Each tile is drawn once and shown
wherever it is placed.

- **Draw pixels into a cell** and the tile under it changes everywhere it is used. An empty cell you draw into
  gets a tile of its own.
- Or switch to **placing tiles**: put the chosen tile down, flipped or turned, erase cells, fill with the bucket,
  or pick a tile with the picker.
- Each frame has its own cells; the tiles are shared.

Aseprite files with tilemap layers open as tilemap layers, flips and all.

## Slices

The **Slice** tool (`C`) draws a named rectangle: a button, a hit box, a panel to stretch by its middle. Give
a slice an optional **nine-slice centre** and **pivot**. Slices move with the canvas when it is cropped, turned
or resized, and go out with every sheet description (Aseprite's `meta.slices` included). **View > Slices...**
lists and edits them.
