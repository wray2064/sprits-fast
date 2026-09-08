# Contributing

Fast is Apache-2.0 and meant to be forked. Contributions back are welcome.

## Sign your commits

```
Signed-off-by: Your Name <your@email>
```

`git commit -s` adds it, certifying the
[Developer Certificate of Origin](https://developercertificate.org/) — that you
wrote the change, or have the right to submit it.

Fast is Apache-2.0 and stays that way, so this is a much lighter obligation than
the engine's. Note that the engine is a separate repository with its own,
stricter, contribution terms.

## The rule worth stating twice

**The engine holds the truth; Fast holds the intent.** Fast never keeps its own
pixel buffer as the authoritative state of anything. When the canvas needs to
show something, it asks the engine to compile it.

The tempting moment to break this is when the canvas feels slow. The right answer
there is a cache with a clear invalidation rule, never a second source of truth.
See [docs/architecture.md](docs/architecture.md).

## Where code goes

- `src/app/` — anything that is not a user interface. Testable without a window.
- `src/ui/` — the interface. Toolkit-specific, and deliberately the only place
  that is.

If a piece of logic could be tested without a window, it belongs in `src/app/`.

## Changes to the document

Every change to a document is bracketed with `beginAction` / `endAction`, or
`abandonAction` if it is cancelled. A tool that forgets is a tool the user cannot
undo, and nothing will fail loudly to tell you.

## Building and testing

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
ctest --test-dir build --output-on-failure
```

Warnings are errors. New behaviour in `fast_core` comes with a test.
