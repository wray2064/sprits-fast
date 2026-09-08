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

Early, but it runs: a window with a canvas, a pencil and an eraser, layers with
rename and delete, undo/redo, a paint bucket, dithered fills with pattern anchoring, and real file
handling -- native Open and Save
dialogs, recent files, drag-and-drop, atomic saves, a prompt before anything
discards unsaved work, PNG export at whole-number scales, and files that open
from the command line.

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
src/app/     the document, undo history, file handling — no interface
src/ui/      the interface — empty, awaiting a toolkit decision
src/main.cpp the smoke run
tests/       what fast_core promises, checked without a window
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
