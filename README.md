# Sprit's'fast

A lightweight sprite editor built on the [LiveSprite engine](../sprit%20s%20pract).

Sprites are stored as mathematical operations rather than as pixels. A fill is a
standing rule about how a region gets its colour; a rotation is a parameter.
Pixels are compiled from that stack on demand, so nothing degrades no matter how
many times you change your mind.

Fast is the small editor that introduces the idea. **Sprit's'pract** is the full
production suite.

![Sprit's'fast](docs/screenshot.png)

## Status

Early, but it is a real editor.

**Drawing** — pencil, eraser, paint bucket, eyedropper, and rectangle, ellipse
and line tools, all with keyboard shortcuts; layers with rename, delete,
visibility and a colour swatch each; a corner preview at true size over any
background.

**Animation** — a frame strip with duplicate, delete and drag-to-reorder, a hold
per frame, named cycles with their own loop mode, playback, and an onion skin.

**What makes it different** — shapes that stay editable after they are drawn,
outlines that follow the artwork and can trace a whole figure across layers,
non-destructive transforms, dithered fills with pattern anchoring, and a palette
whose entries recolour every layer that uses them.

**Files** — native Open and Save dialogs, recent files, drag-and-drop, atomic
saves, a prompt before anything discards unsaved work, PNG export at whole-number
scales, sprite sheets with a description beside them, and files that open from
the command line.

```bash
build-gui/sprits_fast hero.lsprite
```

```bash
./build.bat test        # fast_core and its tests — no toolkit, seconds
./build-gui.bat         # the editor — fetches and builds SDL3, minutes
build-gui/sprits_fast
```

The core and the interface build separately on purpose. `fast_core` — the
document, undo history, tools and file handling — has no toolkit and no window,
so it can be tested without one and so the interface stays replaceable. Most of
the work happens there, and it builds in seconds.

`build/fast_smoke` drives a document end to end and prints what it sees at each
step, with no window at all.

### The palette

Every document has one. Colours are **roles** rather than values: a layer paints
through a palette slot, and changing that slot recolours every layer using it on
the next compile -- from the drawing, not over it. Swapping a character's palette
for a night version or a second team colour is one edit, not a reselect and
repaint.

Click a swatch to point the layer at it, double-click to change what the slot
means. A layer can also carry its own colour, which is what files written before
palettes existed do.

### Getting work out

**File -> Export PNG** writes a picture at 1x through 16x. Scaling is pixel
duplication, never interpolation: a sprite at 4x is exactly four identical pixels
per side, because anything else defeats the point of the format.

Export compiles at `Export` quality rather than writing what is on screen, and
leaves the document untouched -- exporting is not saving.

### The thing to try first

Draw something, add a **Rotate** in the Transform panel, and drag the angle.

![Rotating and returning](docs/rotation.png)

That is one sprite at 0, 24, 90, 137, 300 and 0 degrees again. The last frame is
not *close to* the first, it is byte-for-byte identical — SHA-256 and all. In any
editor that resamples, five turns would have left it soft and chewed.

Nothing is spent because nothing accumulates: the rotation is an entry in the
layer's operation list, and every compile resolves it against the pixels that
were drawn, not against the last frame. Drag the slider through two hundred
angles and there is still exactly one operation. Set it back to zero, or delete
the entry, and the original returns exactly.

The same idea one control over: change the colour in the picker and the drawing
is not repainted. The colour lives on the fill rule rather than in the pixels.

### The corner preview

Working at 26x, you lose all sense of what the sprite reads like at the size it
will actually be seen. The preview shows it at 1x to 4x in the corner of the
canvas, and it costs nothing: it reuses the texture the canvas has already
uploaded rather than compiling the sprite a second time.

The background is the part that earns its keep. A sprite that reads perfectly on
the transparency chequer can vanish against sky blue or lose its outline against
black, and there is no way to find that out except to look. So there are eight
backdrops for the situations a sprite has to survive -- black, dark, grey, white,
sky, grass, sand, blood -- plus any colour you like. Which one is chosen is saved
with the file: a character for a night level should open against a dark one every
time.

Toggle it with **P**, or from the View menu.

### Frames, and a canvas that stays live

Press **T** for the strip, **space** to play, **,** and **.** to step, **O** for
onion skin.

A frame is a sprite, and the document's sprite order is the timeline -- so there
is no second list to fall out of step with the first, and undo puts the frames
back without being told a timeline exists. Duplicating a frame copies the
drawing and shares the palette, which is why recolouring a slot recolours every
frame at once.

The reason this is affordable is worth stating, because the obvious answer is
the wrong one. Compiles here are **O(area)**: 0.14 ms at 32x32, 0.82 ms at
64x64, 3.9 ms at 128x128, and going from 4 layers to 32 moves 0.69 ms to 1.22 ms.
So the canvas is live synchronously and needs no background thread -- and a
thread would not have helped anyway, since a timeline showing eight frames is
eight compiles a frame whether they run on one core or four.

Instead each frame keeps its texture, and the engine is asked which frames
actually changed. Editing frame 3 costs one compile of frame 3; the strip, the
onion skin, the corner preview and playback are all textured quads over textures
that already exist. The status bar shows **compiles this frame**: zero at rest,
one while drawing. `--expect-idle` makes that a CI failure rather than a claim.

Playback is a function of elapsed time rather than a playhead that is stepped,
so a dropped frame costs nothing and scrubbing lands on exactly what playing
showed.

### Cycles

A cycle is a named run of frames -- "walk", "hurt" -- with its own loop mode, so
one plays once and another loops in the same document at the same time. They are
saved with the file.

The thing worth knowing is that a cycle is a **sequence, not a subset**. A step
names a frame, and two steps may name the same one, so a four-frame walk is drawn
once and played `0 1 2 1`. That is why the editor is a second row of steps rather
than a checkbox per frame: a checkbox cannot say a frame twice, and it cannot say
what order.

The strip stays connected to it -- a frame carries one dot per time the cycle
plays it, and a frame no step names is dimmed rather than flagged, because an
in-between kept for later is an ordinary thing to have.

### Sheets

**File → Export sheet** lays the frames into one image: a grid, a row or a
column, at any whole-number scale, from the selected cycle or from every frame.

The promise is that **a cell is byte-for-byte what exporting that frame alone
would have produced**, and it is not automatic. The engine can pin a pattern to
where a frame sits inside something larger, so compiling a frame for cell (2, 1)
rather than at the origin really does change its pixels. Every cell is therefore
compiled at the origin, and the other behaviour -- one dither running
continuously across the whole sheet -- is a tickbox rather than an accident. No
other editor can offer that choice at all, because in every other editor the
dither was baked into pixels long before the sheet existed.

A sheet of a cycle repeats a cell where the cycle repeats a frame, so it plays
correctly by stepping through its cells in order.

Beside the image goes a small `.json` naming every cell's position and hold, the
frame names and the cycles, so a consumer has more than a picture. The format is
versioned from its first line and is not stable yet.

### Shapes that stay shapes

Drag out a rectangle with **R**, an ellipse with **U**, a line with **L**.

![Editing a shape after drawing it](docs/shapes.png)

That is one rectangle. Drawn once, then widened, given rounded corners, then
moved and reshaped -- and the outline follows it every time, because the outline
is generated during the compile from whatever the layer draws rather than being
stamped in when you asked for it.

In every other pixel editor a shape becomes pixels the moment you release the
mouse. Getting it two pixels wider means undoing and drawing it again, and the
outline you added has to be redone as well. Here the rectangle is still a
rectangle an hour later: it still has an origin, a width and a corner radius,
and all three are in the Shape panel.

A layer drawn freehand has no shape to edit, and the panel says so rather than
offering controls that would do nothing. The outline works on it regardless.

### Outlines that know what they are going round

Tick **Outline** in the Shape panel. It is generated during the compile from
whatever is drawn, so it follows the artwork instead of being stamped where the
artwork used to be.

The control that matters is what it traces. **This layer** draws a line round
what that layer draws, which is what one part of a character wants. **Whole
sprite** draws one line round the figure however many layers it is built from --
with no seam where two of them meet, and it follows whichever part moves.

Thickness, inside or outside, and a colour that can be a value or a palette slot.
A slot is the better answer when there is one: the outline then joins a palette
swap instead of being the one thing left behind by it.

### Dithering, and the pattern that stays put

Tick **Dithered fill** and the layer's colour rule becomes a dither: a value
compared against a threshold matrix, choosing between two ramp stops. Constant
density gives a classic two-tone screen; Linear, Radial or Angular vary the value
across the shape, which is how a gradient gets built out of dithered pixels.
Twelve prebaked patterns, all of them threshold matrices rather than one-bit
stamps -- which is why the same tile works at any density and at every step of a
gradient.

The control with no equivalent in a bitmap editor is **anchoring**:

![Local and global anchoring](docs/anchoring.png)

Same drawing, same pattern, same rotation. On the top row the screen turns with
the artwork and the checkerboard falls apart, because a checker rotated off-axis
cannot stay a checker on a pixel grid. On the bottom it stays level with the
canvas and survives the turn intact.

In an editor that bakes dithering into pixels there is no choice to make: you get
the top row. Here it is a parameter, and switching it recompiles from the same
drawing. A third setting, **Fixed**, pins the screen to the canvas so the artwork
slides across it -- that one differs from Global only once the artwork *moves*,
so it does not show in a picture of something merely turning.

**One honest note.** Look at the arrowhead in that strip. Single-pixel diagonals
break into dashes at arbitrary angles — the solid body turns crisply, thin
features do not. That is a property of resolving coverage without
anti-aliasing, which is the right trade for pixel art, but it is worth seeing
before you rely on it.

## Build

Requires CMake 3.20, a C++17 compiler, and the LiveSprite engine.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
ctest --test-dir build
```

The engine is found in one of three ways, tried in order:

| Way | When |
|---|---|
| `-DLIVESPRITE_DIR=<path>` to a source checkout | Development. This is the default, pointing at the sibling directory |
| `find_package(LiveSprite)` | An installed engine |
| `-DLIVESPRITE_GIT_REPOSITORY=<url>` | A clean machine, pinned by tag |

The engine is referenced, never copied in. Two copies of a deterministic engine
means two sets of golden hashes that drift apart.

## How it is put together

```
src/app/       the document, undo, tools, palette, files — no interface at all
src/ui/        the interface: theme, panels, canvas, window
src/main.cpp   the smoke run, which has no window
third_party/   stb_image_write, vendored and pinned
tests/         what fast_core promises, checked without a window
```

`fast_core` is a separate library from anything that draws. Every tool, panel and
menu item goes through `fast::Document`, which owns an engine context and the
history of what has been done to it:

```cpp
doc.beginAction("Draw");
//  ... engine calls ...
doc.endAction();          // or abandonAction(), for a cancelled drag
```

That bracket is the only thing a tool has to remember. Undo is an in-memory
snapshot of engine state — about 0.1 ms — so it is taken per action without
thinking about the cost. Snapshots restore ids exactly, so handles the interface
is holding stay valid across an undo.

Files are LiveSprite packages (`.lsprite`): the document, plus whatever an
application keeps beside it. **Fast carries entries it does not understand
through a save unchanged**, so opening a file made by Pract and saving it does
not destroy Pract's data. There is a test for that, because it is the kind of
promise that quietly stops being true.

## Licence

Fast is **Apache-2.0**. Fork it, gut it, ship something better — that is what it
is for. Third-party components and their licences are listed in [NOTICE](NOTICE);
all of them were chosen so that none imposes anything on a fork.

Being open does not make the whole product open source: Fast depends on the
LiveSprite engine, which is **source-available under the Business Source License
1.1**, becoming Apache-2.0 four years after each version is published. Building
an editor on the engine — including a commercial one, including one that
competes with this — is explicitly allowed. See the engine's `LICENSING.md`.

That is stated here rather than left to be discovered later.
