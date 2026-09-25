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
