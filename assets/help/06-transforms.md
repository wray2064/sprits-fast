# Rotating, scaling and the canvas

## Transforms are numbers on a layer

Pick a layer and open the **Transform** panel. **Rotate**, **Scale**, **Mirror** and **Offset** add an
entry to the layer's list of operations: an angle, a factor, a number of whole pixels. The picture
is not turned; the drawing is walked again at the new angle every time it is shown. So five turns
and a return to zero give back the original, byte for byte, and deleting the entry brings it back too.

On the canvas, the **Move** tool (`V`) shows a gizmo at the pivot of a layer that has a Rotate or a Scale:

- Drag the **ring** to turn it. Hold `Shift` for steps of 15 degrees: 90 degrees is a stop, and a
  quarter turn puts every pixel exactly where a quarter turn sends it.
- Drag the **red arrow** to stretch across and the **green arrow** to stretch down. `Shift` for quarter steps.
- Drag the **square** between them to move the layer by whole pixels. `Shift` keeps to one axis.
- `Esc` in the middle of a drag puts it back.

The Transform panel takes the same numbers typed.

## Rotation, pixel-art style

A turned layer is not a picture turned. Its strokes are walked again at the new angle, one pixel wide
and joined; its fills are found again inside their outlines, with no gap and no leak; a brush keeps its
size; a dither stays level and crisp while its gradient turns with the shape. No colour appears that
the drawing did not have. (Pixels that come from outside, an imported image before you draw over
it, are turned with the RotSprite algorithm.)

> One honest note: a single-pixel diagonal breaks into dashes at arbitrary angles. The solid body
> turns crisply; thin features do not. That is what drawing without anti-aliasing means, and it is
> the right trade for pixel art, but look before you rely on it.

## Tweens

Give a layer the same transforms (rotate, scale, an offset) in two frames, `Shift`+click to select the
run between them in the timeline, and press **Tween**. Every frame in between gets the values in
between, linear or eased in and out. Each in-between is still a live transform of the drawing, so a
frame can be nudged by hand afterwards and nothing is resampled twice.

## The canvas

**Sprite** menu, each one undo step, across every frame at once:

- **Canvas size...** grows or shrinks the canvas about any of nine anchors.
- **Crop to selection** and **Trim** (to the smallest rectangle anything is drawn in).
- **Sprite size...**, **Enlarge** and **Reduce**: resize to any size or percentage by nearest neighbour,
  shapes scaled as shapes.
- **Rotate canvas** by 90, 90 the other way, or 180, and **Flip canvas** horizontally or vertically. Guides,
  slices and puppets turn with it.

The largest canvas is 16,384 pixels on a side and 16 million pixels in all (4096 x 4096).
