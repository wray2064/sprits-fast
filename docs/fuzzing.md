# Fuzzing Sprit's'fast

Every place the program reads something it did not write is fuzzed: a file a
person downloaded, a settings file edited by hand, text pasted into a field. The
question is the same each time -- can any input make the program crash, hang, run
out of memory, or read what it should not -- and the answer is found by trying
millions of inputs rather than by thinking of them.

## What is fuzzed

`tests/fuzz/fuzz_targets.cpp` holds the targets; each is a function that takes bytes
and does with them what the program does. `fast_fuzz_driver --list` prints them.

| Target | What it reads |
|---|---|
| `aseprite` | an Aseprite file: the parse |
| `aseprite-import` | an Aseprite file: the parse and the document built from it |
| `image` | PNG, JPEG, BMP and GIF: header, decode, frames |
| `package` | a `.lsprite` package: open it, then read everything Fast keeps in it |
| `metadata` | what Fast keeps on a document (layers, sprites, the document), as hostile text |
| `theme` | a `.lstheme` file: parse, check, repair, write, read back |
| `preferences` | the settings file: read, write, read -- a fixed point |
| `keymap` | the key bindings file and chords |
| `catalogue` | a translation catalogue |
| `palette-text` | GPL, JASC and hex palettes |
| `palette-act` | Photoshop `.act` palettes, and palettes taken from an image |
| `cycles`, `guides`, `slices`, `references` | the lists stored in a document |
| `recent-colours` | the recent colours setting |
| `ui-state` | the view state stored beside a document |
| `vector-icon` | an SVG icon |
| `batch` | the command line of a batch export |
| `guide` | the user guide's Markdown and its search |
| `text-helpers` | trim, split, escape and the other helpers every parser uses |

What is **not** fuzzed, because it takes no input from outside: the drawing tools,
the rasteriser and the engine's own evaluation of a document it built itself. Those
are held by the unit tests, the system tests and the UI scripts. The program has no
network code (a test checks that), so there is no protocol to fuzz.

## What a target checks

A crash, a hang over twenty seconds, a memory error (AddressSanitizer is on for the
program, the engine and the tests), and more than three gigabytes of memory all stop
a run. Beyond those, a target says what must hold of whatever came out, and aborts if
it does not:

- **Round trips are fixed points.** What is written can be read, and what is read can
  be written back identically: themes, settings, key chords, cycles, guides, slices,
  references, recent colours, view state. A reader that accepts something its writer
  cannot reproduce is a reader that has not understood it.
- **Nothing escapes its place.** A theme or layout id read from a file is always a
  plain name, so it can never name a path; an escaped field never contains the
  separator it is escaped from.
- **Bounds hold.** A decoded image has at least as many bytes as its width, stride and
  height say; the search in the user guide only ever names topics that exist.

## Running it

From the project folder, in PowerShell:

```powershell
.\tools\fuzz.ps1 -Seconds 60                       # every target, a minute each
.\tools\fuzz.ps1 -Seconds 600 -Targets aseprite,package
.\tools\fuzz.ps1 -List
```

The first run builds `build-fuzz\` (AddressSanitizer and libFuzzer: `build-fuzz.bat`),
seeds a corpus for each target, and runs them in turn. Corpora are kept in
`build-fuzz\corpus\<target>` and grow between runs, so a second run starts where the
first stopped. A failure leaves its input in `build-fuzz\crashes\` and the script exits
non-zero.

Every ordinary test run also runs a small blind version (`fast_fuzz_quick`, 300
random inputs per target from a fixed seed) and every input in
`tests/fuzz/regressions/`, so the fuzzing cannot silently stop compiling and a bug
fixed stays fixed.

To reproduce an input: `build-fuzz\fast_fuzz_driver.exe --target NAME --run FILE`.
After the bug is fixed, copy the input to `tests\fuzz\regressions\<target>\<what-it-was>.bin`.

## Results

Run 1, on the whole set: every target for 120 seconds, about 4.9 million executions in
all. No crash, no hang, no broken invariant.

Run 2, the targets with the most to find (the ones that decode or build the most from
what they read), for longer:

| Target | Time | Executions | Coverage features | Result |
|---|---|---|---|---|
| `package` | 1200 s | 36,740 | 545 | clean |
| `metadata` | 600 s | 280,961 | 1,277 | clean |
| `aseprite-import` | 600 s | 296,716 | 1,964 | clean |
| `image` | 600 s | 551,630 | 330 | clean |
| `guide` | 600 s | 189,877 | 1,438 | clean |
| `aseprite` | 300 s | 2,351,611 | 1,001 | clean |
| `theme` | 300 s | 84,928 | 1,099 | clean |
| `palette-text` | 300 s | 162,112 | 1,352 | clean |
| `vector-icon` | 300 s | 186,755 | 870 | clean |

Together, about 4.7 million more executions and nothing found. `tests/fuzz/regressions/`
holds no inputs, because there has been nothing to keep: every one would be a bug that
was fixed.

`package` is slow because each input is written to a file and opened by the engine;
it is the one target that touches the disk. Coverage "features" is libFuzzer's own
count of distinct paths it has seen.

The fuzzer is given a start: the seeds for `aseprite` and `aseprite-import` are built
by `tests/ase_builder.h` (the same builder the unit tests use), so it begins from
files that reach the cel, palette, tag and layer readers instead of having to
discover the format by chance.

## When to run it again

After a change to any reader in the list above -- a new file format, a new setting, a
new thing stored in a document -- add a target (or a seed) for it, and run that target
for ten minutes before committing. A reader added without a target shows up as a gap
the next time someone reads this table against `src/app`.
