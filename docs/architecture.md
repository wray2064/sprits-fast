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
