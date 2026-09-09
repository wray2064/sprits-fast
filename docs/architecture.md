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

## The canvas

The number that shapes this: a full compile at 256×256 with 20 layers takes
about **37 ms**, while a cached compile costs microseconds.

So the canvas must not compile synchronously on the interaction path. The shape
that follows:

- Compile on a background thread; show the last good raster until the new one
  arrives. `LSContext` is not thread-safe, so either the compile owns the context
  for its duration or there is a lock — decide this before writing the canvas,
  not after it stutters.
- Redraw freely from cache. Idle repaints are effectively free.
- Use `CompileProfileType::Preview` while editing and `Export` when writing a
  file.
- While dragging, drive parameters through `setOperationParameter` rather than
  rebuilding operations, so the dependency graph invalidates narrowly.

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
