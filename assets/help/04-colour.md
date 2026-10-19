# Colour and palettes

## Palette slots

Every document has a palette, and colours in it are **slots** rather than values. A pixel
painted through a slot remembers the slot. Change the slot and everything painted through
it changes, on every frame, drawn again from the work, not painted over. Swapping a
character's colours for a night version or a second team colour is one edit.

## The Palette panel

- **Click** a swatch to paint with it. **Right-click** to put it on the second colour.
- **Double-click** a swatch to change what the slot means, name it, or remove it (a warning
  comes first if anything paints through it).
- Drag a swatch onto another to move it; **Sort** orders by hue, saturation or lightness. No
  slot changes its number, so nothing painted through one notices.
- **Add current colour** makes a new slot from the colour you are painting with.
- **Make every colour a slot** turns every colour painted as a value of its own, on every frame,
  into a slot.
- **Ramp** fills evenly spaced slots between a left and right colour.
- **Adjust** turns the hue and pushes saturation and lightness of every slot at once, and can
  be dragged back to exactly where it was.
- **Presets** are palettes generated for Fast: greys, one bit, hue ramps shaded toward blue
  and yellow. Famous palettes load from their own files.

Under the swatches, **Shades** offers the ramp of the current colour, hue-shifted the way pixel
artists shade (darks toward blue, lights toward yellow). Click one to paint with it, or press
**Keep these shades as slots**. **Recent** lists colours you painted with that are not slots, newest first.

## Several palettes, and the swap

A document holds as many palettes as you want. The drop-down at the top of the panel says which
one every frame uses, and `<>` or `Ctrl+P` steps to the next: the whole animation recolours in one
step. Under **Palettes** (closed until you need it) each palette can be renamed, copied and
deleted, and a single frame can be given a palette of its own. One palette per frame along the
timeline is colour cycling.

## Loading and saving palettes

**Load** and **Save** use these formats:

| Format | Where it comes from |
|---|---|
| `.gpl` | GIMP, and what Aseprite reads and writes; keeps the slot names |
| `.hex` | one colour a line; what Lospec serves |
| `.pal` | JASC |
| `.act` | Photoshop's colour table |

Load also reads a palette off any image. Loading replaces the palette the current frame uses, and
recolours a sprite drawn through slots, which is the point of them. A stray line in a palette file
is skipped rather than failing the file.

## The colour you are painting with

The **Tool** panel shows the two colours (left and right button) with a picker. `X` swaps them.
A colour that is not a slot is a plain value; it stays exactly as picked.

## Opening a picture

When you open a PNG, GIF, JPEG or BMP with 256 colours or fewer (pixel art), those colours
**become the palette**, in the order they first appear, and every pixel paints through its slot.
Recolouring a sprite from somewhere else is then one swatch edit. A picture with thousands of
colours is a photograph and is refused with the suggestion to import it as a
[reference](topic:references).

## Adjust colours

**Sprite > Adjust colours** changes hue, saturation, lightness, brightness and contrast, or inverts,
for a layer, a frame or every frame. It changes the elements' own colours and, if asked, the
palette slots they paint through. Nothing becomes pixels, and cancelling puts back exactly what
was there.
