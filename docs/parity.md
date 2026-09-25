# Parity with the incumbents

The goal set on 2026-09-25: Sprit's'fast should do at least what Aseprite,
LibreSprite, Spriteloop and Pixelorama do for a pixel artist, and do it better
wherever the engine makes that possible. Fast is the free showcase for
LiveSprite. **Pract** keeps what is out of scope here: multiview references,
references from 3D models and video, palette and silhouette extraction, and the
sheet/atlas Arranger.

This file is both the checklist and the progress log. A session that resumes
cold reads it first, takes the next open item in the current tier, and ticks it
off here in the same commit that lands it.

Legend: `[x]` done · `[~]` partial (see note) · `[ ]` open · **(better)** marks
where Fast should beat the incumbents, not just match them.

Every item has to keep the one rule: operations are the truth, and pixels are
only output. A feature that bakes pixels to get done faster is not done.

---

## Tier 1 — without these it is not a pixel editor yet

### Colour and ink
- [x] **Many colours on one layer.** A layer's freehand pixels used to be one
      region painted one colour, so a character in eight colours needed eight
      layers. Now each colour is one freehand element per ink: painting with a
      colour adds to that ink's region and removes the same pixels from the
      layer's other inks. Pixels stay one fill rule per colour, so a palette
      slot still recolours from the drawing **(better: every pixel is a role,
      as in Aseprite's indexed mode, but without committing the sprite to
      indexed)**
- [x] Foreground and background colour; `X` swaps them; right-click paints
      with the background colour
- [x] The picker takes the slot as well as the colour, so painting with a
      picked colour stays in the palette **(better)**
- [x] Hex entry and HSV/RGB sliders on the brush colour
- [x] Recolour one ink of a layer afterwards (what the old layer-colour control
      did), from the element list

### Selection
- [x] Rectangle marquee, ellipse marquee, lasso, polygon lasso, magic wand
      (contiguous and global, tolerance), select by colour
      *(done: all but the polygon lasso; the wand's Whole canvas is select by
      colour)*
- [x] Add, subtract and intersect (Shift, Alt, Shift+Alt)
- [x] Select all, deselect, reselect, invert
- [x] Marching ants
- [x] Move the selected pixels (drag, arrow keys); it moves the authored
      pixels, not a raster copy of them
- [x] Cut, copy, paste (in place, and as a new layer); paste into another frame
      *(done: in place and into another frame; as a new layer is open)*
- [x] Delete clears the selected pixels on the active layer
- [x] Flip the selection horizontally and vertically, and rotate it 90/180
- [x] Tools clip to the selection while one exists
- [x] Copy and paste images through the OS clipboard

### Canvas and sprite
- [x] Canvas size (with an anchor), crop to the selection, trim to content
- [x] Resize the sprite (nearest neighbour, whole numbers and percentages)
      **(better: shapes resize as shapes)**
- [x] Rotate the canvas 90/180, flip the canvas horizontally and vertically
- [x] Zoom with the wheel toward the cursor, fit, reset
- [x] Pan with Space+drag and with the middle mouse button (check which
      already work)
- [x] Pixel grid
- [x] Custom grid (size and offset), snap to grid
- [x] Tiled-mode preview (draw across a wrapped edge) **(Aseprite, Pixelorama)**
- [x] Symmetry drawing: horizontal, vertical, both, with a movable axis
- [x] New document dialog: size, background, palette preset

### Tools
- [x] Pencil, eraser, bucket, eyedropper, rectangle, ellipse, line
- [x] Brush size, round/square, pixel-perfect, pen pressure
- [x] Filled and outlined rectangle and ellipse; Shift constrains to a
      square or circle, and a line to 15° steps
- [x] Spray / airbrush (seeded, so the result is deterministic)
- [x] Gradient tool, dithered **(better: it stays a live FillDitherOp)**
- [x] Shading ink: step a pixel along a ramp of palette slots
- [x] Replace-colour ink, lock-alpha ink
- [~] Contour / polygon fill
- [ ] Curve tool (Bezier) **(better: stays editable, like shapes)**
- [x] Text tool, with a bundled bitmap font
- [x] Custom brush from a selection
- [x] Stroke stabiliser / smoothing
- [x] Hand tool and zoom tool (for pen users)
- [x] Line tool drawn as pixels (Shift+click from the last point)

### Animation
- [x] Frames: add, duplicate, delete, reorder, per-frame duration
- [x] Cycles (tags), with loop, once and ping-pong
- [x] Playback, onion skin
- [x] Onion skin settings: range, tint, show only within the cycle
- [x] Reverse frames, and set the duration of a range of frames at once
- [ ] Linked cels (the same layer content in several frames)
      **(better: a shared region, so editing one edits all)**
- [x] Frame and cycle selection by range (Shift+click in the strip)
- [x] Playback speed and a loop-section preview
- [ ] Layers the same across frames (a layer added to one frame appears in
      all of them, as in Aseprite's timeline grid)

### Files
- [x] `.lsprite` open/save, atomic, recent files, autosave and recovery
- [x] PNG export at whole-number scales, sprite sheets with a manifest
- [x] Open a PNG/BMP/GIF/JPG as a new document (as pixels in inks by colour)
- [x] Open an animated GIF as frames
- [x] Export animated GIF
- [~] Export animated PNG and WebP
- [x] Export frames as a numbered PNG sequence
- [x] Import a sprite sheet (grid slicing) as frames
- [x] Open `.aseprite`/`.ase` files **(the most important import for
      winning users over)**
- [x] Export sheet options: padding, trim, JSON hash/array like Aseprite's, by
      cycle
- [x] Command-line batch export (`sprits_fast --export`)
- [x] Save a copy (`Document::saveCopy` already exists)

### Palette
- [x] Slots as roles, several palettes, the swap, per-frame palettes
- [x] `.gpl` and `.hex` load and save
- [x] Palette presets that ship with Fast (licences checked; see *Assets*)
- [x] Reorder slots by dragging; sort by hue, saturation, lightness
- [x] Add or insert a slot, and generate a ramp between two slots
- [x] `.pal` (JASC) and `.act` load and save; a palette from a PNG
- [x] Palette from the sprite's current colours
- [x] Colour-shade bar for the current colour
- [x] Hue-shift, saturate or lighten a whole palette **(better: a palette
      edit, so every frame follows it)**

### Layers
- [x] Blend, opacity, groups, clipping, locks, reorder, duplicate,
      copy/paste
- [x] Merge down, done as grouping: the README's rule holds
- [x] Layer properties dialog (name, blend, opacity)
- [x] Show or hide all others (Alt+click the eye)
- [ ] A reference layer that exports nothing (a reference already covers
      most of this)

### Editing
- [x] Undo and redo
- [x] Undo history panel
- [x] Customisable keyboard shortcuts
- [x] Preferences: checker colours, grid colour, default size, autosave
- [ ] Several documents open at once (tabs)

## Tier 2 — polish that users of the incumbents expect
- [ ] Adjustments: hue/saturation, brightness/contrast, invert, as
      operations or palette edits rather than baked pixels
- [ ] Outline and drop shadow as effects **(better: live operations, not
      filters; the outline already is)**
- [ ] Slices (named rectangles, 9-slice) exported in the manifest
- [ ] Pixel-art rotation (RotSprite-quality) for selections and layers
- [x] Minimap / navigator
- [ ] Guides and rulers
- [x] Isometric grid
- [x] Reference layer from the clipboard
- [x] Light theme
- [ ] Localisation hooks

## Tier 3 — beyond the incumbents
- [x] Palette-indexed PNG export (engine issue; a game can swap colours at run time)
- [ ] Shapes on a curve that stay editable
- [ ] Per-frame transforms as tweens between key frames

---

## Log

Newest first. One line per landed item, with the commit.

- Sheet trim (panel checkbox, batch --trim): every cell cut to the union of what the frames draw, so cells still line up; Fast's manifest gains trim and source, Aseprite's JSON trimmed/spriteSourceSize/sourceSize. A sheet of nothing keeps the whole canvas. Checked end to end through batch export.
- Light theme (Preferences > Look): paper and a darker amber; switching re-applies the style live, and a chequer and grid still at the old theme's defaults follow to the new one's (canvasDefaultsFor), while chosen colours stay. Saved as view.theme; --light for captures.
- Isometric tile grid: View > Tile grid > Isometric draws 2:1 diamonds (width across, height down, with the offset); --isometric for captures.
- The corner preview is the navigator: when the view shows only part of the canvas, that part is outlined on it, and clicking or dragging on it centres the view there (CanvasView::visibleArea, centreOn). --zoom N for headless captures.
- Paste image (References panel) and Edit > Paste as reference: an image copied anywhere becomes a reference, stored in the package like an imported one.
- Shade strip under the palette: seven shades of the current colour, darks turned toward blue with a little more saturation and lights toward yellow with a little less (shadesOf, greys stay grey); click to paint (through a slot when the palette has that colour), right-click for the second colour; the strip holds still while picked from; one button keeps them as slots.
- Sprite size window: any size in pixels or percent, proportion locked or not, nearest neighbour by pixel centres (resizeSprite); a doubling equals enlargeSprite exactly, shapes scale as geometry, off-canvas pixels are carried along, one undo step for every frame.
- Snap to grid (View menu, Shift+S, rebindable): rectangle and ellipse marquees cover whole tiles (a drag inside one tile selects it), shape corners land on tile lines, and moving a selection steps its top-left corner from grid point to grid point. Uses the tile grid's size and offset; turning snap on shows the grid. Math in app/grid_snap with its own tests.
- Layer properties window (row menu > Properties...): name, blend, opacity, visible, locked, a colour tag (eight presets or any colour) drawn as a stripe on the row, and notes shown on hovering it; tag and notes are layer metadata, saved with the file. A lock toggle now marks the document modified.
- System clipboard: copies also go out as PNG and a BITMAPV5 with alpha (Win32 directly on Windows, since SDL offers one image format and misreads V5 bitfield DIBs; SDL elsewhere). Another program's image pastes as a float, one piece per colour, exact palette colours as slots, over 256 colours reduced to the nearest slots. Windows' clipboard sequence number tells Fast's own copies from others, so a Fast-to-Fast paste keeps slots and dithers. Checked end to end via --system-paste/--system-copy against .NET's clipboard reader: PNG and DIB both pixel-exact.
- Customisable shortcuts: every shortcut is a named command (app/keymap) with default chords; Preferences > Keys rebinds by pressing, flags shared chords, resets one or all; keys.txt holds only overrides; menus show the live keys. Preferences window: new-document defaults, autosave, undo depth, pixel-grid colour/opacity, chequer colours and size, saved atomically to preferences.txt. Headless runs ignore both files.
- 2026-09-25 — custom brush from a selection (Ctrl+B), stamped colour by colour or in the current colour.
- 2026-09-25 — polygon lasso; stroke stabiliser (lazy mouse).
- 2026-09-25 — text tool with Fast's own 5x7 face; text stays an element that can be retyped, resized and moved.
- 2026-09-25 — outlined rectangles and ellipses, any width, switchable in the Element panel.
- 2026-09-25 — indexed PNG export: the palette's slots in order are the table's first entries; menu and batch --indexed.
- 2026-09-25 — sheet border and spacing; manifest in Aseprite's JSON (hash/array) with cycles as frame tags; batch --border/--spacing/--json.
- 2026-09-25 — runs of frames: Shift+click selection, duplicate/delete/reverse/one hold, Play loops the run; playback speed.
- 2026-09-25 — command-line batch export: --export in --to out.png/.gif/.lsprite/palette, with --scale, --cycle, --frame, --sheet, --sequence, --animated.
- 2026-09-25 — New document window (size, background, preset), Save a copy, Paste as new layer.
- 2026-09-25 — gradient tool: a drag lays a dithered gradient that stays a live FillDitherOp.
- 2026-09-25 — spray, contour, hand and zoom tools; ink modes: lock alpha, replace colour, shading along palette slots.
- 2026-09-25 — .aseprite/.ase open as work: layers, groups, frames, holds, tags as cycles, palette; indices become slots.
- 2026-09-25 — merge down moves elements into the layer below, folding opacity and blend; refuses what would change the picture.
- 2026-09-25 — history panel, onion skin settings (range, cycle neighbours, wrap, tints), Alt+click solo.
- 2026-09-25 — palette: drag to reorder and sort (engine palette order), ramps, adjust hue/sat/light, colours to slots, presets, .pal/.act/image; RGB/HSV/hex fields; palette gets its own panel.
- 2026-09-25 — symmetry, tiled mode with wrapping strokes, tile grid, Shift+click lines, Shift-constrained shapes.
- 2026-09-25 — Sprite menu: canvas size with anchor (keeps what falls off), crop, trim, enlarge/reduce, rotate and flip canvas; shapes stay shapes.
- 2026-09-25 — images open as documents (colours become the palette), GIF frames, sheet import; export GIF, APNG, PNG sequence; alpha preserved in every compile.
- 2026-09-25 — selections: marquee, ellipse, lasso, wand; add/subtract/intersect; move, nudge, flip, rotate, cut/copy/paste/delete; shapes ride along as shapes.
- 2026-09-25 — many colours on one layer (inks), left/right colours with X, picker reads the slot, Enter plays and Space pans.
- 2026-09-25 — audit written; parity work begins with many colours on one layer.
