# Testing Sprit's'fast as a system

The unit tests prove each feature on its own. This document is about the
other question: whether the features hold *together* -- a palette loaded after
a frame was duplicated, an undo that walks back through a frame move, a slot
removed while a cycle plays the frame that uses it twice. That is where an
editor actually breaks, and it is what this page is organised around.

Three layers, from cheapest to most human:

1. **`fast_system_tests`** -- one realistic document put through everything
   at once, with pixels as the measure. Runs in `ctest`.
2. **`sprits_fast --self-test`** -- the window's bookkeeping across the same
   seams: indices, handles and playback after undo, delete, New and Open.
   Runs in `ctest` when the GUI is built.
3. **The script below** -- what only a person at the window can judge.

## What the automated layers promise

`tests/system_tests.cpp` builds an animated, role-coloured, outlined, dithered
figure with a cycle that repeats a frame, and holds it to:

| Promise | Why it is a system promise |
|---|---|
| A palette change recolours every frame, whenever the frame was made | A sprite created *after* the palette exists is the one that can miss its binding |
| A cell of a sheet is always what the frame alone compiles to | Frame surgery, cycle repeats and palette swaps all feed the same cells |
| Undo is exact through every feature | Each step back reproduces the pixels recorded on the way forward, across a swap, a load, a move, a delete, a removal |
| A round trip loses nothing that affects a pixel or a control | Names, timing, cycles, labels, roles on ramps and outlines, and the palette bindings themselves |
| Removing a slot changes no pixel; putting it back re-attaches everything | Every layer keeps naming the slot it meant, on every frame |

The self-test in `app_window.cpp` adds the window's half: an empty frame can
be drawn on at once and undoes as one step; playback lands on a frame that
exists after an undo removes the ones it was playing; deleting the edited
frame moves every handle; a frame with fewer layers clamps the layer index;
loading a palette updates the colour control; New forgets every rename,
confirmation, step and cycle that was about the old document, and leaves
nothing to undo.

## What they found

Everything below was caught by writing the system tests, not by using the
program -- which is the argument for having them.

- **The dither panel reset every pattern to Bayer 4x4.** The read-back never
  reported the pattern kind, so the combo showed its default and the first
  edit to any other control wrote that default back over the real one.
- **Every tick of a dither slider leaked a pattern resource** into the file:
  a hundred ticks, a hundred tiles, twenty-six kilobytes.
- **Ramp ends and outlines showed a stale colour.** A swatch bound to a slot
  showed the literal the slot replaced, so after a palette change the panel
  disagreed with the canvas, and *detach* snapped the picture to a colour
  nobody had seen since the slot was assigned. Both now report what is drawn.
- **`+ Empty` made a frame the pencil did nothing to**, silently: a sprite
  with no layers. It gets a layer in the current colour, in one undo step.
- **Ctrl+Z in a new document removed its only layer.** Setting up the document
  was its first history entry.
- **New and Open left renames, confirmations and a selected step pointing into
  the previous document.**
- **Textures for deleted frames were only dropped by one of the doors** a
  frame can leave through. Retention is now checked every UI frame.
- **A layer whose slot had been removed had no way back.** The engine kept
  the role on the layer -- that is what makes restoring the slot re-attach
  everything -- but no swatch was ringed and nothing said why. The palette
  panel now says which slot is missing and offers to put it back at the
  colour currently shown, which changes no pixel.
- **A palette change left every frame but the first stale.** The engine
  dirtied only sprites bound to the palette by name; a duplicated frame and a
  frame added later resolve through the document's binding and were never
  told. Fixed in the engine (`markPaletteDirty`), with a test on both sides:
  the engine's says which sprites go dirty, the system test says every frame
  does after each palette write the panel makes.
- **The canvas was blurred.** Dear ImGui's SDL_Renderer backend (from 1.92.8)
  re-applies its own sampler to every texture it binds, linear by default, so
  the texture's nearest mode was ignored. The canvas now brackets its artwork
  with the backend's nearest callback. No automated test could see this; it
  came from looking at a `--shot`.

## The script: at the window

Run each flow in order; each assumes the state the previous one left. Where a
line says *expect*, that is the check. Anything else you notice is a finding.

Build with the GUI and start it with the demo, which is the figure the
automated layers use with six frames, a cycle and an outline:

```bash
sprits_fast --demo-stroke
```

### 1. The palette is one thing, everywhere

1. Press `T` if the timeline is hidden. Press *Play*. Watch the swing.
2. Double-click the dark blue slot the dither uses (the highlighted one) and
   drag the picker to red. *Expect:* every frame on the strip and the playing
   canvas recolour together, and the ramp's *dark* swatch in the Element
   panel (the *Dither* row of *Layer 1*) shows red -- not the old blue.
3. Press `Ctrl+Z`. *Expect:* one undo puts it all back, strip included.
4. *Load…* a `.gpl` with fewer colours than the palette (write one in a text
   editor: `GIMP Palette`, then `255 0 0 red` and `0 0 255 blue`). *Expect:*
   the status line says how many slots in use were not in the file; the
   frames that used them keep their colours; nothing looks different where a
   slot was missing; the layers that were in the file recolour on every frame.
5. `Ctrl+Z`. *Expect:* the whole palette returns, names included (hover a
   slot).

### 2. Frames under a cycle

1. Select the *swing* cycle. In the *Plays* row, click a step that names a
   frame the cycle plays twice. Press *Delete* on the strip (not the step
   row). *Expect:* both steps naming that frame vanish from the row, the
   duration total updates, playback continues without a hitch, and the
   canvas is on a frame that exists.
2. Drag a frame thumbnail to the front of the strip. *Expect:* the *Plays*
   row renumbers -- the cycle plays the same pictures in the same order.
3. `Ctrl+Z` twice. *Expect:* the exact strip and the exact step row.
4. Press `+ Empty`. *Expect:* the new frame is selected, the Layers panel
   shows *Layer 1*, and the pencil draws on it immediately. `Ctrl+Z` once
   removes the frame and its layer together.

### 3. The dither panel tells the truth

1. Select *Layer 1*, then its *Dither* row in the Element panel. Change the
   pattern to *Checker*. Drag the
   density slider back and forth. *Expect:* the pattern combo stays on
   *Checker* throughout. (It used to snap to Bayer 4x4 on the first drag.)
2. Save, reopen the file. *Expect:* the combo still says *Checker*.
3. With a ramp end bound to a slot, click *detach*. *Expect:* nothing on the
   canvas changes. The end now holds the colour it was showing.

### 4. Removal, and the way back

1. Double-click a slot that layers use and press *Remove slot*. *Expect:* a
   warning naming the consequence, and *Remove anyway* / *Keep it*.
2. *Remove anyway*. *Expect:* no pixel changes anywhere -- every layer keeps
   the colour it showed. With the row of pixels that used it selected in the
   Element panel, the panel says they name a slot the palette no longer has,
   and offers *Put the slot back*.
3. *Add current colour*. *Expect:* it lands in a new slot, not the hole;
   nothing recolours by accident.
4. *Put the slot back*. *Expect:* nothing changes on the canvas; the slot
   reappears ringed; editing it now recolours those layers on every frame.
5. `Ctrl+Z` three times. *Expect:* back to before the removal.

### 4b. The swap

1. The demo has *day*, *night* and *flash*; frame 4 is bound to *flash*. Press
   `Ctrl+P`. *Expect:* every frame on the strip and the canvas recolour to
   night in one step -- except frame 4, which stays white -- and the row at
   the top of the palette panel says *night*.
2. `Ctrl+P` again. *Expect:* back to day; frame 4 still white.
3. Open *Palettes*. Set *This frame* to *the document's* on frame 4.
   *Expect:* it joins the others at once.
4. *copy* on *day*, double-click the copy to rename it, edit one of its slots
   while the document still uses day. *Expect:* nothing on the canvas changes
   until you switch to it.
5. *x* on the palette in use. *Expect:* the document moves to another, every
   frame recolours accordingly, and `Ctrl+Z` brings the palette and the
   binding back.

### 4c. The stack

1. On the demo, `Ctrl+J` on *Layer 1*. *Expect:* *Layer 1 copy* above it,
   selected, inside the *figure* group. Draw on it: the original is untouched.
2. Set the copy to *Multiply*, opacity 0.5. *Expect:* the canvas darkens
   where it overlaps; the row says *Multiply 50%*; `Ctrl+Z` twice undoes each.
3. Drag the copy above *Layer 2*, then below the group. *Expect:* it leaves
   the group when it leaves the run, and the fold count drops.
4. Select the group row. *Expect:* the strip edits the group; opacity 0.5 on
   the group is not the same picture as 0.5 on each layer.
5. `Ctrl+C` on a layer, step to frame 4, `Ctrl+V`. *Expect:* the layer
   appears there, above the active one, and editing it leaves frame 1 alone.
6. Tick *clip to below* on *Layer 2*. *Expect:* it draws only inside
   *Layer 1*. Move it: the clip clears.
7. Tick *lock*, then draw. *Expect:* nothing changes; the status line says
   why.
8. Ctrl+click two layers, `Ctrl+G`, then `Ctrl+Shift+G`. *Expect:* grouped
   next to each other, then back where they were.

### 4d. Elements and drag-to-group

1. Select *Layer 1*. Rectangle tool: drag one out. *Expect:* no new layer; the
   Element panel lists the new *Rectangle* at the top, in the current colour,
   above the *Line*, *Rectangle* and *Dither* already there, and edits it; the
   layer thumbnail shows all of it.
2. Pencil on the same layer, across the new rectangle. *Expect:* the stroke
   shows over the rectangle, as a *Pixels* row of its own at the top; the
   rectangles stay rectangles (widen one in the panel).
3. *x* beside the new rectangle. *Expect:* it goes; the rest stay; `Ctrl+Z`
   brings it back.
4. Tick *Each shape on its own layer*, drag an ellipse. *Expect:* a new layer
   *Ellipse*, selected.
5. Drag that layer onto the *figure* group row. *Expect:* it is in the group,
   at its top. Right-click → *Remove from group*. *Expect:* out, just above.
6. Drag it onto *Layer 2* with Ctrl held. *Expect:* a new group of the two.

### 4h. Many colours on one layer

1. New document. Click the palette's first slot and draw a line; click the
   third and draw across it. *Expect:* one layer; where they cross, the second
   colour; the Element panel lists two *Pixels* rows, each naming its slot.
2. Right-click the fifth slot, then right-drag on the canvas. *Expect:* the
   fifth slot's colour, on the same layer. Press `X`. *Expect:* the two
   swatches in the Tool panel trade places.
3. Double-click the first slot and drag its colour. *Expect:* the first line
   recolours as you drag; the second does not.
4. Eyedropper (`I`) on the second line. *Expect:* the Tool panel says *slot 3*,
   and the palette rings it.
5. Erase the whole of the first line. *Expect:* its row leaves the Element
   panel. `Ctrl+Z`. *Expect:* the line and the row come back together.
6. Select the second line's row, press *= current* with a different colour
   chosen. *Expect:* the line changes colour and nothing else does.

### 4i. Selections

1. New document. Paint a few colours. `M`, drag a box over part of it.
   *Expect:* marching ants; the Tool panel says the size.
2. Drag inside the box. *Expect:* the pixels follow; where they pass over other
   pixels, those are untouched after they pass. Press the arrows. *Expect:* a
   pixel a press. Press Enter. *Expect:* the pixels land, replacing what was
   under them. `Ctrl+Z` once. *Expect:* everything back as it was before the
   drag, in one step.
3. Drag again, then Escape. *Expect:* back where they started; nothing in the
   Edit menu's Undo from it.
4. `Ctrl+C`, then `Ctrl+V`. *Expect:* the move tool is in hand and the copy
   floats over the original. Drag it away and click on an empty panel.
   *Expect:* it drops; the original is still there.
5. Draw a rectangle with `R`, then box it and a few pixels beside it with `M`
   and drag. *Expect:* the rectangle travels as a rectangle -- select it in the
   Element panel afterwards and its from/to have moved.
6. Shift-drag a second box. *Expect:* the two join. Alt-drag across both.
   *Expect:* a bite taken out. `Ctrl+Shift+I`. *Expect:* everything else.
7. `W` on a region of one colour. *Expect:* exactly that area. Tick *Whole
   canvas* and click again. *Expect:* every pixel of that colour.
8. With a selection, pencil across its edge. *Expect:* only the inside is
   painted. Bucket outside it. *Expect:* nothing happens outside.
9. On the demo's *Layer 1* (it is rotated) try to move a selection. *Expect:*
   the status line says the layer has a transform, and nothing moves.

### 4j. Pictures in and out

1. Open a PNG made in another editor. *Expect:* one layer, the palette panel
   showing the picture's colours, the Element panel one row per colour. Edit a
   swatch. *Expect:* those pixels recolour.
2. Open an animated GIF. *Expect:* the strip opens with one frame per GIF frame,
   each with its hold; Play matches the GIF in a browser.
3. *File -> Import sprite sheet* on a strip of frames. *Expect:* the cell size
   guessed square, the frame count said; *Open as frames* asks about unsaved
   work first.
4. *File -> Export animation*, GIF, 4x, from a ping-pong cycle. Open the file in
   a browser. *Expect:* it plays there and back without stalling on either end,
   at four times the size, crisp.
5. The same as Animated PNG with a layer at 50% opacity. *Expect:* the browser
   shows the half-transparency; the GIF of the same thing does not, and the
   status line said so when it was written.

### 4k. The canvas

1. The demo. *Sprite -> Rotate canvas 90 clockwise* four times. *Expect:* the
   picture is back exactly as it was, and each turn was one undo step.
2. *Canvas size*, 20 x 20, anchor bottom-right. *Expect:* the figure in the
   bottom-right of a larger canvas. Then 16 x 16, anchor top-left. *Expect:*
   part of the figure is cut off. Then 32 x 32 again. *Expect:* it is all back.
3. Draw a rounded rectangle, then *Enlarge 2x*. *Expect:* the rectangle is
   twice the size with twice the corner radius, and still listed as a
   rectangle in the Element panel.
4. *Trim* on a sprite with a margin. *Expect:* the canvas hugs the drawing on
   every frame, including frames drawn further out than this one.

### 4l. Drawing aids

1. *View -> Symmetry across*. *Expect:* an orange dashed axis down the middle;
   a stroke on the left appears mirrored on the right. Move the axis in the
   Tool panel. *Expect:* the line moves in half-pixel steps.
2. Pencil: click, then Shift+click elsewhere. *Expect:* a straight line
   between them, pixel-perfect at size 1.
3. Rectangle with Shift. *Expect:* square. Line with Shift. *Expect:* snaps to
   0, 15, 30, 45... degrees.
4. *View -> Tiled mode -> Both ways*, zoom out. *Expect:* the canvas repeated
   around itself, the copies darker. Stroke off the right edge. *Expect:* it
   continues from the left edge, with no line across the canvas at the seam.
5. *View -> Tile grid*, 8 x 8, offset 4. *Expect:* blue lines every 8 pixels,
   starting at 4.

### 4m. Arranging the palette

1. Drag the last swatch onto the first. *Expect:* it moves; nothing on the
   canvas changes. Save, reopen. *Expect:* the order is as you left it.
2. *Sort -> By lightness*. *Expect:* dark to light; the canvas unchanged.
3. Click a dark slot, right-click a light one, *Ramp*, 4 steps. *Expect:* four
   evenly spaced slots appear between the two.
4. *Adjust*, drag hue. *Expect:* the whole sprite recolours as you drag, on
   every frame. Drag back to 0. *Expect:* exactly as before. `Ctrl+Z` undoes
   each drag as one step.
5. Paint with colours picked in the picker (not from the palette), then *Make
   every colour a slot*. *Expect:* new swatches for them; editing one
   recolours those pixels.
6. *Load...* a PNG. *Expect:* its colours become the palette, in the order they
   appear in it.

### 4n. Inks and the extra tools

1. Lay a ramp of four slots out in order (dark to light). Paint an area in the
   second. Pencil, ink *Shading*, scrub over half of it. *Expect:* that half is
   now the third slot, and scrubbing again does not push it further in the
   same stroke; right-button scrubbing steps it back. Edit the third swatch.
   *Expect:* the shaded half recolours.
2. Ink *Lock alpha*, paint across the edge of a drawn shape. *Expect:* nothing
   lands outside it.
3. Ink *Replace colour* with a second colour on the right button. *Expect:* only
   pixels of that second colour change.
4. Spray (`Shift+B`): hold still. *Expect:* it keeps spraying where it is held.
5. Contour (`D`): draw a loop. *Expect:* on release the loop and its inside
   fill with the current colour.
6. Hand (`H`) drags the view; Zoom (`Z`) zooms in on a click, out with Alt.

### 4o. Runs of frames

1. Click frame 2, Shift+click frame 4. *Expect:* a band under frames 2 to 4.
   Press Play with no cycle chosen. *Expect:* only those three loop.
2. *Reverse*. *Expect:* 4, 3, 2 in the strip; a cycle that played them still
   plays the same pictures (its steps renumber). `Ctrl+Z` once undoes it.
3. Drag the hold field. *Expect:* all three frames take the hold.
4. *+ Frame* with the run selected. *Expect:* three copies straight after it,
   in order, selected as the new run.
5. Speed at 0.5x. *Expect:* playback at half speed; the holds are unchanged.

### 4p. Keys and preferences

1. *Edit > Preferences*, Keys. Click the `B` beside Pencil and press `P`.
   *Expect:* the chord reads `P`; `P` now takes the pencil and `B` does
   nothing; *File*'s menu items still show their keys.
2. Give Eraser `P` too. *Expect:* both `P`s turn red, with a tooltip saying
   another command has the key. Click *default* beside Eraser. *Expect:* back
   to `E`, and the red goes.
3. Click a chord and press Escape. *Expect:* unchanged. Click it and press
   Backspace. *Expect:* the chord is removed. *+* adds a second chord.
4. Quit and start again. *Expect:* the keys are as left. `keys.txt` in the
   settings folder lists only what differs from the defaults.
5. General: set a new document to 64 x 48 on white. *File > New*. *Expect:*
   the window starts there. Change the grid colour, its opacity and the
   chequer at 16x zoom. *Expect:* the canvas follows at once; *Canvas
   defaults* puts it back.
6. Set undo steps to 10, draw twelve strokes. *Expect:* only ten undo.
7. Theme: *Paper (light)*. *Expect:* the whole interface turns light at
   once, and the chequer and grid, if still at their defaults, turn light
   with it; a chequer colour you chose stays. Restart. *Expect:* still light.

### 4q. The system clipboard

1. Select part of a drawing, `Ctrl+C`, paste into Paint and into a browser
   text box that takes images. *Expect:* the same pixels in both; the browser
   keeps transparency.
2. Copy an image in a browser (right click > Copy image), back in Fast
   `Ctrl+V`. *Expect:* it floats in the middle of the canvas with the Move
   tool in hand, and the status bar counts its colours and how many are
   palette slots. Enter drops it.
3. Copy pixels in Fast, `Ctrl+V` in Fast. *Expect:* Fast's own clip (a
   dithered area stays a dither), not the picture it also put out.
4. Copy a photograph. *Expect:* the paste says there were too many colours
   and each took the nearest of the palette's.
5. `Ctrl+Shift+V` with another program's image copied. *Expect:* it lands
   on a new layer.
6. Copy an image elsewhere, then *Paste image* in the References panel (or
   *Edit > Paste as reference*). *Expect:* a reference fitted to the canvas,
   named for its size, saved with the file.

### 4r. Layer properties

1. Right-click a layer > *Properties...*. Pick the orange tag. *Expect:* an
   orange stripe at the row's left edge. Type notes and click away.
   *Expect:* the row gains a `*`; hovering the stripe shows the notes.
2. Rename, change blend and drag opacity in the window. *Expect:* each is one
   undo step and the Layers panel follows.
3. Save, reopen. *Expect:* tag and notes are still there. *none* clears the
   tag; emptying the notes removes the `*`.
4. Right-click a layer > *Reference layer*. *Expect:* `[ref]` on its row, the
   canvas unchanged. Export a PNG, a sheet and a GIF. *Expect:* the layer is
   in none of them. A whole-sprite outline no longer goes round it. Undo
   takes it back to an ordinary layer.

### 4s. Snap to grid

1. *View > Snap to grid* (`Shift+S`). *Expect:* the tile grid shows.
2. Rectangle marquee from near one tile corner to near another. *Expect:*
   the selection covers whole tiles; a drag inside one tile selects it.
3. Move the selection. *Expect:* its top-left corner jumps from grid point
   to grid point.
4. Rectangle tool. *Expect:* its corners land on tile lines.
5. Set the tile grid to 10 x 6 with offset 3, 1 and repeat. *Expect:* the
   same, on the new lines.
6. *View > Tile grid > Isometric*. *Expect:* diamonds 16 across and 8 down
   over the canvas; the offset moves them.

### 4t. Sprite size

1. *Sprite > Sprite size...*, 200%. *Expect:* 64 x 64 from 32 x 32, the
   note says it is exact; reducing to 1/2 gives the original back.
2. Unlock the proportion, width 150%, height 100%. *Expect:* every pixel is
   one of the old colours; some columns doubled. A rectangle shape is still
   a rectangle, wider.
3. Undo. *Expect:* one step back to the old size for every frame.

### 4u. Shades

1. Pick a saturated colour. *Expect:* under the palette, seven shades with it
   in the middle, framed; the darks lean blue, the lights lean yellow.
2. Click a dark shade. *Expect:* it becomes the colour and the strip does not
   move. Pick a palette swatch. *Expect:* the strip re-centres on it.
3. *Keep these shades as slots*. *Expect:* the new ones join the palette;
   pressing it again adds none.

### 4v. The navigator

1. Zoom in until the sprite overflows the view. *Expect:* an orange box on
   the corner preview marks what the view shows.
2. Click elsewhere on the preview, then drag across it. *Expect:* the view
   follows; nothing is painted. Fit (`Ctrl+0`). *Expect:* the box goes.

### 4w. A trimmed sheet

1. A 32 x 32 document with a figure in its middle, three frames. *Export
   sheet*, *Trim to what is drawn*, Aseprite JSON (hash). *Expect:* cells
   only as big as the figure's reach across all three frames; the JSON has
   `"trimmed": true` and `spriteSourceSize` where they were cut from.
2. Batch: `sprits_fast --export in.lsprite --to out.png --sheet --trim`.
   *Expect:* the same.

### 4x. WebP

1. *Export animation*, format WebP, 2x. Open the file in a browser.
   *Expect:* it plays with the holds and loops; transparency is kept.
2. Batch: `sprits_fast --export in.lsprite --to out.webp --frame 2` and
   `... --to anim.webp --animated`. *Expect:* one frame, and the animation.
3. The encoder's own check, against libwebp: `fast_webp_tests --write DIR`
   writes pictures of every kind beside their raw pixels; decoding each with
   any WebP decoder (Pillow's is libwebp) must give the raw pixels back
   exactly.

### 4y. Polygons, curves and handles

1. Polygon (`Shift+D`): click five corners, click the first. *Expect:* a
   filled polygon in the current colour, listed as *Polygon* in the Element
   panel, its corners marked on the canvas.
2. Drag a corner. *Expect:* the polygon follows; one `Ctrl+Z` puts it back.
   With snap on, corners land on grid points.
3. Curve (`Shift+L`): click, then press and drag at the second point, then
   click a third and press Enter. *Expect:* a one-pixel curve with no
   doubled corners, bending through the middle point; its handles show.
4. Drag a point: its handles come with it. Drag a handle: only the bend
   changes. Tick *Closed* in the Element panel. *Expect:* it fills.
5. Backspace while placing takes the last point back; Escape lets the whole
   path go; changing tool does too.
6. A rectangle drawn earlier and made active: its two corners are handles
   under the Move and shape tools. Rotate the canvas, save, reopen.
   *Expect:* polygon and curve still editable, in their new places.

### 4z. Tabs

1. Draw something, then *File > Open* another file. *Expect:* it opens in a
   second tab; the first keeps its drawing, marked unsaved.
2. Select in one tab, switch with `Ctrl+Tab`, undo there. *Expect:* each tab
   has its own selection and history; switching back finds both as left,
   zoom and pan included.
3. Start the program, open a file at once. *Expect:* it replaces the blank
   document rather than opening beside it.
4. Open the same file again. *Expect:* its tab is shown; no second copy.
5. `Ctrl+W` on a tab with changes. *Expect:* the save question; Cancel keeps
   it, Discard closes it and shows its neighbour. Close the last tab.
   *Expect:* a new blank document.
6. Two tabs with changes, `Ctrl+Q`. *Expect:* asked about each in turn, each
   shown while asked; Cancel stops the quit.
7. Kill the program with changes in two tabs. *Expect:* next start offers two
   recovered copies.

### 4aa. Layers in every frame

1. A new document, three frames. Add a layer in frame 2. *Expect:* frames 1
   and 3 have it too, empty, in the same place.
2. Rename it in frame 3, hide it, tag it, drag it to the bottom. *Expect:*
   the same in frames 1 and 2. One `Ctrl+Z` takes each change back
   everywhere.
3. Draw on it in frame 1 only. *Expect:* frames 2 and 3 unchanged.
4. Duplicate it in frame 1. *Expect:* every frame gets a copy of its own
   cel of it.
5. Merge it down. *Expect:* merged in every frame; refused, with the reason,
   if any frame cannot merge -- and then nothing merged anywhere.
6. *+ Linked* on frame 1. *Expect:* frame 2 identical, its rows marked
   `[linked]`. Draw on frame 2: the stroke appears on frame 1 as it is
   drawn; a new colour appears on both when the stroke ends. Unlink a row
   in frame 2 (its menu): it keeps what it showed and stops following.
   Link a layer across a selected run from its row menu.
7. Open a file saved before this (frames with their own layers). *Expect:*
   unchanged; *Sprite > Same layers in every frame* joins layers of the same
   name and adds the missing ones empty, losing nothing.

### 4ab. Adjust colours

1. *Sprite > Adjust colours...*, this layer, hue 120. *Expect:* the layer's
   own colours turn on the canvas as the slider moves; slot colours do not.
   Tick *Palette slots too*: they turn as well, and the palette with them.
2. Every frame, invert. *Expect:* every frame inverted. Reset: exactly as
   before. Cancel: nothing changed, nothing to undo.
3. Apply. *Expect:* one undo step; the Element panel still lists every
   element, a rectangle still a rectangle.

### 4ac. Drop shadow

1. A rectangle; Element panel > *Drop shadow*. *Expect:* a shadow down and
   right, only where the rectangle is not. Offset, colour and opacity change
   it live; each drag one undo step.
2. Move the rectangle. *Expect:* the shadow follows. Draw new pixels on the
   layer. *Expect:* they cast a shadow too. Add an outline. *Expect:* the
   shadow is cast by the outlined shape.
3. *Cast by the figure* on a bottom layer. *Expect:* one shadow for the whole
   sprite, under every layer.

### 4ad. Slices

1. Slice tool (`C`): drag a rectangle. *Expect:* "Slice 1", framed in blue
   with its name. Drag inside it: it moves. Drag a corner: it resizes. Each
   one undo step.
2. *View > Slices...*: rename it, tick *Nine-slice centre* and *Pivot*.
   *Expect:* the centre's lines and the pivot's ring on the canvas.
3. Delete with the tool and `Delete`. Undo brings it back.
4. Export a sheet with Aseprite JSON. *Expect:* `meta.slices` names it with
   its bounds, centre and pivot at the sheet's scale.
5. Rotate the canvas. *Expect:* the slice turns with it.

### 4e. The brush

1. Pencil, size 1, pixel-perfect on. Draw a slow diagonal in steps. *Expect:*
   a clean diagonal; no doubled corners; the last pixel lands on release.
2. `Shift+]` to size 5, tick *Round*. *Expect:* the hover outline grows with
   it; the stroke is a round-ended ribbon with no gaps at speed.
3. Eraser at size 3. *Expect:* erases a 3-wide band.
4. With a pen tablet: bring the pen near. *Expect:* *Pen pressure sets size*
   appears. Tick it; press lightly then hard. *Expect:* thin then thick. Turn
   the pen over. *Expect:* the eraser end erases and the tool comes back on
   lift.

### 4f. References and the libraries

1. The demo starts with a reference called *study* behind the artwork at 0.4.
   *Expect:* it shows through the chequer; the sprite is unaffected.
2. Drag the *opacity* slider. *Expect:* one `Ctrl+Z` undoes the whole drag.
   Tick *behind* off. *Expect:* it draws over the artwork, and that is its own
   single undo step.
3. Hold **Alt** and drag on the canvas. *Expect:* the reference moves, one
   undo step for the drag. Tick *lock*, try again. *Expect:* it does not move
   and the status line says why.
4. *Import image...* and pick a photograph. *Expect:* it lands centred and
   fitted. Pick something that is not an image. *Expect:* a refusal naming the
   reason, and nothing added.
5. Save, close, reopen. *Expect:* both references come back with their
   placement, opacity and order.
6. `Ctrl+Z` back past the import. *Expect:* the reference goes, image and all
   -- not just its entry in the list.
7. `Ctrl+L`. On *Sprites*, choose the folder your work is in. *Expect:* a tile
   per sprite with its picture; the open one is ringed; clicking another opens
   it and asks about unsaved changes first.
8. On *References*, choose a folder of images. *Expect:* a tile per image;
   clicking one imports it. *Up* walks to the parent; *Reload* re-reads.
9. Close and reopen the program. *Expect:* both folders are remembered.

### 4g. Autosave and recovery

Run with a short interval so this takes seconds rather than minutes:

```bash
sprits_fast --demo-stroke --autosave 15
```

1. Draw something. Wait twenty seconds. *Expect:* the status line says
   *Recovery copy written*, and a file appears in the settings folder --
   `%APPDATA%\SpritsFast\recovery` on Windows.
2. **Kill the program** (Task Manager, or close the console). *Expect:* the
   copy is still there.
3. Start it again. *Expect:* an *Unfinished work* prompt naming the file, or
   *untitled*. Press *Later*, quit normally, start again. *Expect:* it is
   offered again -- "later" is not "discard".
4. Start again and press *Open*. *Expect:* the drawing comes back, the title
   bar shows unsaved changes, and *Save* asks where to put it rather than
   writing into the settings folder.
5. Draw, wait for a copy, then *Save* properly. *Expect:* the recovery file
   disappears -- the work is where you put it and a stale copy would only
   offer something older.
6. Quit normally with unsaved work and choose *Don't save*. *Expect:* no
   recovery file is left behind.
7. Hold a slider or drag a stroke across an autosave moment. *Expect:* no
   copy is taken mid-drag; it lands once you let go.

### 5. Replacing the document mid-everything

1. Start renaming a layer (double-click its name). Start renaming a frame.
   Open a slot's popup and press *Remove slot* on one in use, so the
   confirmation is showing. Select a cycle step. Press *Play*.
2. *File > New*, any size, confirm. *Expect:* nothing is mid-rename, no
   confirmation is showing, playback is stopped, the timeline is on frame
   one of one, and `Ctrl+Z` does nothing.
3. Repeat 1, then open a recent file. *Expect:* the same, and the view lands
   where the file was last closed.

### 6. The sheet is the frames

1. *File > Export sheet…* with the cycle selected, grid, 2x. Open the PNG.
   *Expect:* one cell per *step* (a repeated frame appears twice), each cell
   pixel-identical to that frame's own export, and a `.json` beside it naming
   every cell and the cycle.
2. Change a palette slot, export again. *Expect:* the cells changed with it.

### 7. Things that should never happen

- A frame the pencil does nothing to.
- A swatch that disagrees with the canvas.
- A combo that shows something other than what is there.
- An undo that changes more, or less, than the last thing you did.
- A file that grows while you drag a slider.
- A blurred pixel at any zoom.

## Reading a screenshot

`--shot` writes what the window drew. It is a BMP with whatever name you
gave it, so name it `.bmp` or convert it before opening in something strict:

```bash
sprits_fast --frames 40 --demo-stroke --shot demo.bmp --expect-idle
```

`--expect-idle` fails the run if anything was still compiling on the last
frame, which is the cache invalidation check CI runs. `--play` starts the
cycle playing and `--library` opens the library window, so a headless run
draws both. `--autosave <seconds>` shortens the recovery interval, and every
run prints whether autosave is armed.
