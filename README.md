# Sprit's'fast

A lightweight sprite editor built on the [LiveSprite engine](../sprit%20s%20pract).

Sprites are stored as mathematical operations rather than as pixels. A fill is a
standing rule about how a region gets its colour; a rotation is a parameter.
Pixels are compiled from that stack on demand, so nothing degrades no matter how
many times you change your mind.

Fast is the small editor that introduces the idea. **Sprit's'pract** is the full
production suite.

## Status

Early. There is no user interface yet.

What exists is `fast_core`: the document, the undo history, and file handling —
the part of an editor that has nothing to do with drawing windows, built first so
that it can be tested without one and so the choice of toolkit stays replaceable.

```bash
./build.bat            # Windows; or use CMake directly, below
build/fast_smoke       # drives a document end to end and prints the result
ctest --test-dir build # 11 checks over undo, redo, save, reopen
```

`fast_smoke` makes a canvas, draws a square, undoes it, redoes it, saves a file
and reopens it — printing what it sees at each step. It has no window on purpose:
it proves the foundation works before any of it is hidden behind an interface.

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
is for.

Being open does not make the whole product open source: Fast depends on the
LiveSprite engine, which is **source-available under the Business Source License
1.1**, becoming Apache-2.0 four years after each version is published. Building
an editor on the engine — including a commercial one, including one that
competes with this — is explicitly allowed. See the engine's `LICENSING.md`.

That is stated here rather than left to be discovered later.
