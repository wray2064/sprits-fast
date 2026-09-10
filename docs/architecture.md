# How Fast is put together

Written so the decisions here are made on purpose
rather than by accident.

## The one rule

**The engine holds the truth; Fast holds the intent.**

Fast never stores pixels as the authoritative state of anything. When the canvas
needs to show something, it asks the engine to compile it. When the user draws,
Fast adds an operation and asks again. The moment Fast starts keeping its own
pixel buffer as truth and syncing it back, the entire premise of the product is
gone — a rotation becomes a resample, and quality starts draining away exactly
like every other editor.

This is easy to violate accidentally under performance pressure. The correct
response to a slow canvas is a cache with a clear invalidation rule, never a
second source of truth.

## Layers

```
  src/ui/         windows, panels, input        (toolkit-specific)
      │
      ▼
  src/app/        document, history, tools      (fast_core — no toolkit)
      │
      ▼
  livesprite      operations → pixels           (a separate repository)
```

`fast_core` is a static library that knows nothing about the interface. Two
reasons, and the second matters more than it looks:

1. It can be tested without a window, which is where most of the suites live.
2. The toolkit stays replaceable. If the first choice turns out badly, what gets
   rewritten is `src/ui/`, not the editor.

## The document

`fast::Document` owns an `ls::LSContext`, the document inside it, and the undo
history. One per open tab.

### Actions

Every change is bracketed:

```cpp
doc.beginAction("Draw");
//  ... engine calls ...
doc.endAction();
```

- `beginAction` captures the state before the change.
- `endAction` commits it to the history and clears the redo branch.
- `abandonAction` puts the document back and leaves no history entry — this is
  what a tool calls when a drag is cancelled with Escape.

Nesting is counted, so a compound tool whose helpers also bracket their work
produces one history entry rather than five.

**Why snapshots rather than commands.** The usual editor design is a command
pattern: every action knows how to undo itself. That is more code, and every new
tool is another chance to write an inverse that is subtly wrong. The engine's
`snapshotDocumentState` copies engine state in about 0.1 ms and restores every id
exactly, so a snapshot per action is affordable and cannot be wrong. The cost is
memory rather than correctness, and `setHistoryLimit` bounds it.

The id-exactness is the part that makes this work at all. If undo renumbered
entities, every handle the interface was holding — the selected layer, the
operation shown in a panel — would be dangling after one undo.

## The canvas, and why there is no thread

Measured on this machine with `livesprite_bench`, a compile is **O(area) and
barely notices layers**:

| Canvas | 8 layers | | Layers at 64x64 | |
|---|---|---|---|---|
| 32x32   | 0.14 ms | | 1  | 0.45 ms |
| 64x64   | 0.82 ms | | 4  | 0.69 ms |
| 128x128 | 3.9 ms  | | 16 | 0.89 ms |
| 256x256 | 16.5 ms | | 32 | 1.22 ms |

A cached compile is 0.001 ms. Driving one parameter costs the same as compiling
everything -- the engine has an incremental path, but it does not help here.

So for every sprite a person actually draws, a full compile fits inside a frame
with room to spare. The canvas is live, synchronously, and needs no help. The
budget breaks at 256x256, which is where a drag starts costing a frame of
latency.

**A background thread was the wrong answer, and would have been even if the
numbers were worse.** `LSContext` is not thread-safe, so a background compile
means either handing it the context for the compile's duration -- blocking the
very edit the compile exists to show -- or copying the document per compile,
which costs more than compiling. And it does not touch the problem that actually
arrives with frames: a timeline shows eight frames at once, and eight compiles a
frame is 31 ms at 128x128 whether they happen on one thread or four.

### One texture per frame

What fixes that is not compiling them. `FrameCache` keeps a texture per frame
and asks the engine which sprites actually changed. The engine tracks that
exactly -- editing one frame leaves its neighbours clean, a geometry edit reaches
the sprite that draws it, and an undo dirties everything -- so trusting it is
both correct and the only thing that makes a timeline affordable.

Editing frame 3 therefore costs one compile of frame 3. The timeline thumbnails,
the onion skin, the corner preview and playback are all textured quads over
textures that already exist.

The number that says so is on the status bar: **compiles this frame**. Zero at
rest, one while drawing, and never once per frame on screen. `--expect-idle`
makes it a CI failure rather than a claim: a settled editor that compiles
anything is one that has quietly lost this design, and no screenshot would show
it.

### The strip has a budget

Two cases would otherwise stall. Opening a long animation has nothing cached, and
an **undo dirties the whole document** -- the engine restores state wholesale and
cannot know what actually differs -- so every thumbnail goes stale at once. At
128x128 with twenty frames, refreshing them all in one pass is a 78 ms hitch
after every Ctrl+Z.

So the strip compiles at most two frames per pass and draws what it already has
in the meantime. A thumbnail one pass behind is invisible; a hitch is not. The
canvas is never delayed by this, because it compiles before the strip is reached.

### The rule that has not changed

Use `CompileProfileType::Preview` while editing and `Export` when writing a file,
and drive parameters through `setOperationParameter` during a drag rather than
rebuilding operations.

## Transforms are a list, not a history

The part of Fast with no equivalent elsewhere, and the reason the engine exists.

A rotation is an entry in the layer's operation stack. The Transform panel shows
that stack and lets it be edited: drag the angle and a parameter changes, then
the layer recompiles from the region and fill that were authored. Set it back to
zero and the original pixels return exactly. Remove the entry and the same. Two
hundred drags of the slider leave one operation, because nothing was ever
applied to anything -- there is only one transform resolved against untouched
source.

`tests/transform_tests.cpp` states that as something that can fail: not "roughly
as good" but the same bytes, after wandering through forty angles and back.

Two consequences for the interface:

**The pencil has to be mapped back.** A layer shown rotated is still drawn on
straight -- the pencil writes into the region and the transform then acts on it.
So a point under the cursor is carried through the inverse of the layer's
composed transform before it becomes a pixel. Without that, cursor and mark part
company the moment an angle is set. A transform with no inverse, such as a scale
of zero, refuses to be drawn through rather than putting marks somewhere
arbitrary.

**A slider drag is one history entry.** The same bracket the colour picker uses:
open on grab, close on release. Without it a drag is either not undoable at all
or leaves hundreds of entries.

## Files

A `.lsprite` file is a LiveSprite package: a ZIP with stored (uncompressed)
entries, holding `livesprite.json` plus whatever applications keep beside it.

### Paths are UTF-8, and that is not a formality

`std::ofstream(const char*)` on Windows reads the path in the active ANSI code
page. Handed UTF-8 it does not fail -- it silently creates a file with a mangled
name. A user saving into a folder with an accent in it gets a file they cannot
find; a file dialog, which hands back UTF-8, produces a garbled duplicate rather
than opening what was chosen. Round-tripping inside one program hides it
completely, because the same wrong name is used to write and to read.

So all file access goes through `app/file_io`, which converts to UTF-16 and uses
the wide API on Windows. `tests/file_io_tests.cpp` writes through an accented
path deliberately.

### Saving is atomic

A save writes `target.saving` beside the destination, then renames it over the
top. An interruption -- a crash, a full disk, a pulled cable -- leaves either the
old file or the new one, never half of either. Writing straight into the
destination means a failure halfway through destroys the artwork that was already
there, which is the worst thing a save can do.

The temporary sits beside the target rather than in a system temp directory, so
the rename stays within one filesystem and is therefore atomic.

### The preview costs nothing on purpose

The corner preview draws the sprite again at a different scale, which sounds like
a second compile and must not be one. `CanvasView` already holds the texture it
uploaded for the canvas, so `drawSample` reuses it: a preview is a textured quad
and a background rectangle. Compiling twice a frame for the same picture would be
the obvious implementation and the wrong one.

### Where you were when you closed it

Zoom, pan and the selected layer are not part of the artwork, so none of it goes
near the engine's document. It rides in the package as a `fast/` entry, which is
what namespaced app entries are for: another application ignores it, and Fast
writing the file back does not disturb what that application keeps beside it.

**It is also untrusted input.** It arrives from whoever sent the file, and a zoom
of 1e30, a NaN pan or a layer index of four billion all take about ten seconds to
produce in a text editor. `app/ui_state` parses a deliberately small subset of
JSON -- a flat object of numbers, no nesting, no escapes -- refuses infinities at
the parser, and clamps everything else before it reaches the canvas. A pan far
outside any plausible window puts the artwork somewhere the user cannot scroll
back to, which looks exactly like a file that failed to open.

Unknown fields are ignored rather than refused, so a newer Fast can record
something this build has never heard of without making the file unopenable.

### Asking before discarding

New, Open, opening a recent file, a dropped file and the window's close button
all pass through one guard. If the document has unsaved changes the question is
asked and the action is *parked* until it is answered -- including across an
asynchronous Save As dialog, which is the case that usually gets this wrong.
Cancel abandons the pending action rather than quietly proceeding with it.

Fast's own data is namespaced under `fast/`. **Entries it does not recognise are
read, kept, and written back out unchanged.** Opening a file that Pract wrote and
saving it must not destroy Pract's data — that is the whole reason the engine
owns the container and applications own the entries, and there is a test for it
because it is the kind of promise that quietly stops being true during a
refactor.

Everything in a file that came from elsewhere is untrusted input. The engine
bounds-checks the container, verifies every entry's CRC, refuses compressed
entries and validates entry names rather than repairing them. What it hands back
is still only a *claim* about what those entries contain: a content type in a
manifest is what the writer said, not a fact.

## Frames

**A frame is a sprite.** A LiveSprite document already holds an ordered list of
sprites, each with its own layers, operations and transforms, and that list is
what a saved file carries. So Fast keeps no frame list of its own: the document's
sprite order *is* the timeline.

That is worth stating because the alternative is so tempting. A parallel
`std::vector<Frame>` in the app is easier to write and wrong in a way that shows
up weeks later -- it has to be kept in step with the engine through every add,
delete, undo, reorder and reload, and the first time it slips the editor is
showing frame 3 while drawing into frame 4. With one list there is nothing to
keep in step, and undo -- which restores engine state wholesale -- puts the
timeline back without being told a timeline exists.

Everything the engine has no opinion about is stored beside it, and where it goes
follows from what it is:

| | Lives in | Because |
|---|---|---|
| The frames, in order | The document's sprite list | It is the thing itself |
| How long a frame is held, its name | Engine metadata on the sprite | Part of the artwork: undone with it, saved with it |
| Cycles -- named runs like "walk" | Engine metadata on the document | Same |
| Which frame is being looked at | The `fast/` view entry | Where you were, not what you made |

### Duplicate, then change one thing

That gesture is what animation is made of, and it is `cloneSprite`. When this was
built the engine's clone shared what it should have copied: the copy's operations
still named the original's regions and geometry, so moving the arm in frame 2
moved it in frame 1, with nothing on screen to say why. The engine now copies the
drawing (geometry and regions) and shares the document's resources (palettes,
ramps, patterns) -- a palette role that recolours every frame at once is the
reason roles exist. `tests/animation_tests.cpp` draws a square, duplicates it,
moves the copy and checks the original is byte-for-byte unchanged.

### A cycle names pictures, not positions

Drag frame 3 to the front and a cycle that played `0, 2` has to still play the
same two pictures. So every reorder and delete renumbers the cycles that
reference them, and a cycle left with no frames is dropped rather than kept
empty -- an empty cycle is a trap for every loop downstream that reads it. This
is the bug every timeline written in a hurry has, so it is a test rather than a
comment.

### Playback is stateless

`frameAt(frames, cycle, elapsedMs)` is a function of time, not a playhead that
accumulates. Scrubbing backwards therefore lands on exactly what playing forwards
showed, which an accumulating playhead cannot promise. Loop, Once and PingPong
are the three modes, and ping-pong does not play either end twice -- four frames
is six steps, not eight.

### Cycles are stored as text, not JSON

The view entry's parser reads a flat object of numbers and nothing else, on
purpose: it parses data from files other people send, and a parser nobody can
check is a bad place for that. Cycles need names and lists, so rather than grow
that parser into a general one they use a small line format of their own, with
`|`, newline and `%` percent-escaped in names so a name cannot say a new field or
a new line. Malformed lines are dropped rather than failing the read: one bad
cycle should not cost a person the other seven.

### The interface

A strip along the bottom of the canvas, toggled with **T**. Every thumbnail is a
texture the cache already holds, drawn at a whole-number scale -- whole numbers
matter as much here as on the canvas, because a thumbnail at 3.7x has pixels of
two different widths, which is exactly the artefact someone is looking at a
thumbnail to check for.

Selection and the playhead are marked differently -- an amber ring and a bar --
because during playback they are different frames, and conflating them makes
playback look like it is moving the selection. Drawing is refused while playing
rather than landing silently on a frame nobody is looking at.

Onion skin draws the neighbours under the live frame, warm behind and cool
ahead. It is an underlay hook inside `CanvasView::draw` rather than a second call
after it, because *under* is the point: ghosts painted over the live frame haze
the thing being judged. It only ever draws frames that already have textures --
paying a compile for a convenience would undo the reason it is affordable.

### A cycle is a sequence, not a subset

The step row under the strip is the cycle editor, and its shape follows from one
fact: **a step names a frame, and two steps may name the same one.** A four-frame
walk is authored once and played `0 1 2 1`; two cycles share drawings without
either owning them.

That is why the obvious design -- a checkbox per frame saying "in this cycle" --
was not built. It cannot say `0 1 2 1`, and it cannot say what order. So the
frames and the steps are two rows, labelled differently, because they are two
lists: what was drawn, and the order it plays in.

The strip still carries the connection. A frame gets one accent dot per time the
cycle plays it, and a frame no step names is dimmed rather than flagged -- an
in-between kept for later is an ordinary thing to have, not a mistake.

The loop mode belongs to the cycle rather than to the editor, because "hurt"
plays once and "walk" loops, in the same document, at the same time.

The last step of a cycle stays, for the same reason the last frame of a document
does: `setCycles` drops a cycle with no steps, so removing the last one would
silently delete the cycle -- and deleting a cycle is a different thing, which a
person asks for differently.

### Sheets

The promise a sheet is built around: **a cell is byte-for-byte what exporting
that frame alone would have produced.**

That is not automatic, and the reason is the interesting part. The engine lets a
pattern be anchored in *export space*, meaning its lattice is pinned to wherever
the output frame sits inside something larger. So compiling a frame for cell
(2, 1) rather than at the origin genuinely changes its pixels -- by design. The
engine's `frame_tests` states it: one pixel of export origin moves a dither, a
whole tile of it puts the dither back, and a pattern in Canvas space ignores it
entirely.

So every cell is compiled at the origin, exactly as a single-frame export is, and
then composited. The other behaviour is offered as a choice rather than happening
by accident: **one pattern across the whole sheet** sets each cell's export origin
to its position, so a screen runs continuously across the image. Nothing else in
the program can do that, and nothing should do it without being asked. Its test
uses a 14-pixel canvas against a 4-pixel tile on purpose -- at 16 the cells land
on whole tiles and the lattice lines up anyway, so a test at 16 would pass
whether or not the export origin was ever passed through.

`planSheet` is separate from compiling anything, so the arithmetic that decides
whether an image is 3 MB or 3 GB is testable on its own, and an absurd sheet is
refused with a sentence rather than attempted and failing on the allocation. The
bounds are the same canvas policy -- 16384 a side, 4096x4096 of area.

A sheet exported from a cycle **repeats a cell when the cycle repeats a frame**.
That is the useful behaviour: the sheet plays by stepping through its cells in
order, which is all a consumer wants to do.

### The manifest

A sheet without a description of where its cells are is half a deliverable, so a
small `.json` is written beside the image under the same name -- `hero.png` and
`hero.json`, not `hero.png.json`. It lists every cell's position and hold, the
frame names, and the cycles.

**The format is young and versioned from its first line** (`sprits-sheet/1`), so
a consumer that reads it can tell which shape it has. It is not a stable
interface yet, and eventual game-engine bridges are the reason to be careful with
it rather than the reason to freeze it now.

A frame name is whatever somebody typed, so it is escaped properly. A manifest a
quotation mark can break is one no consumer can trust, and there is a test that
throws quotes, backslashes and newlines at it.

## Outlines

An outline is an operation resolved during the compile, not a filter that adds
pixels. Move the artwork and the line moves; in a conventional editor it stays
where the artwork used to be.

**What it traces is a real choice, not a preference.** A character built from a
body layer and an arm layer wants one line round the *figure*; a highlight or a
held object wants a line round *that part*. Where two layers touch, a per-layer
outline draws a seam straight through the character and a figure outline does
not.

The engine had the field for this -- `GenerateSilhouetteOutlineOp::targetSprite`
-- and never read it. A silhouette outline always traced the raster its own
layer had built. So the field is now real: `compileSprite` notices when any layer
holds a sprite-scoped outline and, only then, compiles the layers once with those
outlines suppressed to build a silhouette, then compiles again with it in hand.

Three things that had to be right:

- **The outline must not trace itself.** Hence the suppressed first pass; without
  it the second pass would find the first pass's ink and grow a line around the
  line.
- **A layer holding one is never cached alone.** What it draws depends on every
  other layer, and the layer cache key knows only about this one -- a hit would
  hand back an outline of a figure that has since changed shape somewhere else.
- **A layer compiled on its own still draws something.** With no silhouette
  supplied it falls back to tracing its own layer, because an application asking
  for one layer has not asked for a hole.

It costs a second compile of every layer, paid only by sprites that ask for one.

### The colour

Either a value or a palette slot. A slot is the better answer when there is one:
the outline then joins a palette swap instead of being the one thing left behind
by it, and it resolves from the drawing rather than being painted over it. The
"use a palette slot" button starts from the slot the layer's own fill uses, so
the line and the fill move together unless told otherwise.

`OutlineSide` is the engine's `{ Inside, Outside, Center }`, and the panel's
labels are written in that order deliberately -- putting them in a nicer-reading
order silently means the wrong thing, which is exactly what happened the first
time this was written.

## The toolkit

**Dear ImGui on SDL3**, drawing through `SDL_Renderer` rather than a hand-rolled
OpenGL path — a 2D pixel-art editor has no need for an OpenGL loader.

Licensing decided as much as anything: SDL3 is zlib, Dear ImGui is MIT, and
neither imposes anything on a fork. **Qt was ruled out on exactly this point.**
Fast is Apache-2.0 and meant to be forked freely; Qt's LGPL path puts relinking
obligations on everyone who ships a fork, and its commercial path costs money per
developer. A toolkit that makes forking Fast legally awkward would quietly cancel
the reason Fast is open at all.

The honest cost of ImGui is that it looks like a developer tool until it is
themed, and its text input and accessibility are weaker than a native toolkit's.
Both were judged acceptable because a sprite editor's real surface — canvas,
palette, layer stack, timeline — is custom drawing under *any* toolkit, so stock
widgets were never going to carry the product.

Both dependencies are pinned to exact tags in `cmake/Dependencies.cmake`. A UI
toolkit that moves under the build is not a thing anyone should have to debug.
`FAST_BUILD_GUI` is off by default, because turning it on builds SDL3 from
source and most work happens in `fast_core`, which needs none of it.

The file dialogs are SDL's own native ones, so there is no extra dependency for
them — but they are **asynchronous**, and the callback may arrive on another
thread. A choice is therefore parked behind a mutex and picked up by the main
loop rather than acted on where it lands.

## Which engine API Fast uses

The **C++ API**. It is the pleasant one and the right default for a C++
application compiled alongside the engine.

An earlier plan had Fast go through the C ABI so that the ABI would have a
serious user proving it. That reasoning no longer holds: the ABI already has
three independent users in the engine's CI — a C test suite compiled by the C
compiler, a Python ctypes run, and the installed-package consumer job. Making
Fast a fourth would slow Fast down for proof that already exists.

## How the interface is put together

```
  src/ui/theme.*        colours, spacing, and the small shared widgets
  src/ui/editor.*       what the interface holds: tool, active layer, drag state
  src/ui/panels.*       one function per panel
  src/ui/canvas_view.*  the compiled sprite on screen, zoom, pan, hit testing
  src/ui/app_window.cpp the loop, the layout, input, and the file commands
```

Three rules the look follows, in `theme.cpp`:

**The interface must not compete with the artwork.** Every colour in the
interface is a low-saturation neutral. The only saturated colour is one accent
used for selection and focus. A blue panel beside a blue sprite makes the sprite
harder to judge, and judging the sprite is what the window is for.

**Contrast only where it carries meaning.** Labels are dim, values bright,
borders barely visible. What is bright is what has been drawn.

**Nothing moves that does not have to.** No animated transitions. A tool that
flickers while you work is a tool you stop trusting.

The interface font is the system's own, found by trying a short list of the faces
each platform actually ships and falling back to ImGui's built-in face. That
built-in face is a 13-pixel bitmap designed for debug output, and it is most of
why an ImGui program looks like one.

### One rule for every live control

A slider drag must be **one** undo step -- not none, and not three hundred. That
is easy to get wrong and invisible until somebody tries to undo, so it is written
once, as `bracketDrag` in `panels.cpp`, and every live control uses it.

## Testing a program that has a window

Three layers, deliberately:

- **`fast_core` suites** cover everything with no toolkit: document, undo, paint,
  file I/O, recent files. Most of the behaviour lives here, and it builds in
  seconds.
- **`--self-test`** drives the window's own bookkeeping — rebuilding the layer
  panel after undo, redo and open, and the save/title/recent-list glue. None of
  that is reachable from a `fast_core` test, and all of it was wrong once.
- **`--frames N --shot`** runs the real interface under SDL's dummy video driver,
  which needs no display, and can write the frame out. This is what notices an
  editor that no longer starts, and it is how the screenshot in the README is
  produced.
