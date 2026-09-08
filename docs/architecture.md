# How Fast is put together

Written before the interface exists, so that the decisions are made on purpose
rather than by accident once there is a window to fill.

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

1. It can be tested without a window, which is why there are eleven checks over
   undo and file handling before a single pixel has been drawn on screen.
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

## The canvas, when it exists

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

## What is not decided yet

**The toolkit.** `src/ui/` is empty. This is the next decision, and it is a
one-way door in practice, so it is worth making deliberately rather than by
reaching for the familiar.

One constraint that is easy to miss: **Qt's licensing does not fit this project.**
Fast is Apache-2.0 and meant to be forked freely. Qt is LGPL or commercial; the
LGPL path imposes relinking obligations on everyone who ships a fork, and the
commercial path costs money per developer. A toolkit under MIT, zlib or BSD keeps
the promise the licence makes.

**Whether Fast uses the engine's C++ API or its C ABI.** Currently the C++ API,
which is the pleasant one and the right default for a C++ application compiled
alongside the engine. The C ABI already has independent users proving it — a C
test suite, a Python ctypes run, both in the engine's CI — so Fast does not need
to be a fourth.
