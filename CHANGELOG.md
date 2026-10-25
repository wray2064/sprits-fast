# Changes

## Unreleased

- Lossless WebP pictures can be read wherever an image is (references, imports), not
  only written. Lossy WebP is not read, and says so.
- Pictures with many colours are written to WebP much smaller.
- Edit > Preferences > Experimental > Lift the size limits: the canvas, frames, cycles, puppet
  parts, references, brush and the reach of outlines and shadows are comfortable defaults, and
  one switch removes them, up to what the computer can hold.

## 1.0.0 -- 2026-10

The first release for the Microsoft Store.

**The editor**
- Strokes stay strokes: pencil, spray, eraser, bucket, contour, shapes, text, paths,
  tilemaps and dither are all kept as what they are, so turning, scaling and
  recolouring a layer redraws it cleanly and nothing degrades.
- Layers, groups, blend modes, clipping, selections, floating pieces, rotation at
  any angle in pixel-art style, tweens, canvas and sprite size, symmetry, tiled
  mode, grids, guides, slices.
- Animation: frames, cycles with loop modes, linked cels, onion skin, a timeline
  that opens over the canvas when wanted or pins under it, playback at any speed.
- Cutout puppets: parts, joints and sockets, posed by hand and captured to a frame.
- Palettes in slots, ramps, presets, several palettes and the swap; GPL, JASC, hex,
  ACT and PNG palettes in and out.
- Opens `.lsprite`, PNG, GIF, JPEG, BMP and Aseprite files; exports PNG (1x to 16x,
  indexed), GIF, APNG, animated WebP and sprite sheets with JSON for engines.

**The program**
- A start screen with templates, recent files and a tip; New with every option.
- A new look: nine themes (including OLED, two high-contrast and a Famicom one),
  your own colours for every role, accent colours, interface size and density,
  one font and one set of icons throughout, checked for readability.
- Panels dock, tab, float and resize; named layouts (Drawing, Animation, Compact and
  your own).
- Help menu with a searchable user guide, keyboard shortcuts, command search.
- English and Spanish.
- Automatic recovery copies, atomic saves, a per-user installer, and the Microsoft
  Store package.
- Reads every file defensively; fuzz-tested (see `docs/fuzzing.md`).
- No account, no telemetry, no network.
