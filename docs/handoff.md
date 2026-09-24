# Handoff

Written for whoever picks this up next, human or model, starting cold. It says
what the three products are, what is actually built against the specification,
where every part lives, and the traps that have already cost time.

Two repositories, both public, both green on every platform:

| | Repository | Licence | HEAD at writing |
|---|---|---|---|
| **LiveSprite** (engine) | `wray2064/livesprite-engine`, local `C:\Users\wray2\AppDev\sprit s pract` | BSL 1.1, Change Date 2030-09-06 | `cd9cce0` |
| **Sprit's'fast** (editor) | `wray2064/sprits-fast`, local `C:\Users\wray2\AppDev\sprits-fast` | Apache-2.0 | `4edef05` |

**Sprit's'pract** is the third product: proprietary, not started, no repository.
The engine specification already reserves work for it — see *Deviations* below,
because one decision has already brushed against that boundary.

---

## 1. Start here

```bash
cd "C:\Users\wray2\AppDev\sprit s pract" && cmd /c .\build.bat test
```

```bash
cd C:\Users\wray2\AppDev\sprits-fast && cmd /c .\build-gui.bat
```

Fast has two build trees on purpose. `build/` is `fast_core` and its tests, and
takes seconds. `build-gui/` adds SDL3 and Dear ImGui, which take minutes the
first time. Most work does not need the GUI tree.

Run the editor with content already in it:

```bash
cd C:\Users\wray2\AppDev\sprits-fast && .\build-gui\sprits_fast.exe --demo-stroke
```

Headless flags, all of which CI uses: `--frames N` runs a fixed number and
exits, `--shot <path>` writes what was drawn (a BMP, whatever the extension),
`--self-test` runs the window's own bookkeeping checks, `--expect-idle` fails
if anything was still compiling on the last frame, `--play`, `--library`,
`--sheet <path>`, `--autosave <seconds>`.

Fast finds the engine through `LIVESPRITE_DIR`, defaulting to `../sprit s
pract`. CI clones it from `vars.LIVESPRITE_REPO` instead.

---

## 2. The rules that govern everything

Break any of these and the design stops meaning anything. They are not style
preferences; each one is load-bearing.

**Operations are the truth, pixels are output.** Nothing is ever baked. A
rectangle drawn an hour ago is still a rectangle with an origin and a corner
radius. A dither is a rule for colouring a drawing, not the coloured drawing.
Changing a palette slot recolours every layer that uses it *from the drawing,
rather than over it*. Any feature that would rasterise and forget is wrong by
default — see the README's note on why there is no *merge down*.

**Colours are roles.** An operation names a `ColorRole`; the palette turns it
into a colour at compile time. This is why palette swapping works at all.

**The engine never calls back into the app.** Data flows one way: app → engine
→ compiled output. The engine owns all entity storage; apps hold opaque IDs.

**Everything from a file is untrusted.** Packages, palettes, reference images,
UI state, cycles. The pattern throughout is: bound it before allocating, refuse
rather than repair, and say why in words a person can act on.

**`ls::Result<T>` everywhere, no exceptions across the API boundary, no RTTI.**

**Determinism.** The same sprite compiles to the same bytes on MSVC, gcc, clang
and Apple clang. `livesprite_determinism` holds golden hashes; if you change
rasterisation and that test fails, you have changed what every existing file
looks like.

---

## 3. What each part is

### The engine — `C:\Users\wray2\AppDev\sprit s pract`

| File | What it is |
|---|---|
| `include/livesprite/ls_types.h` | IDs, `Result<T>`, enums, `Vec2i/f`, `Mat3f`, `Color`, `RasterBuffer`, the 2 GiB raster ceiling |
| `include/livesprite/ls_operations.h` | The 39 operation structs — fills, strokes, outlines, transforms, deforms, `PluginOp` |
| `include/livesprite/ls_api.h` | `LSContext`, the whole public surface, PIMPL |
| `include/livesprite/livesprite_c.h` | The C ABI. **Every mirrored enum is pinned by `static_assert`** — see Traps |
| `src/ls_geometry.cpp` | Rect, ellipse, polyline, curve, interval sets, boolean ops |
| `src/ls_context.cpp` | CRUD for documents, sprites, layers, groups, palettes, ramps, patterns; `cloneSprite`/`cloneLayer` |
| `src/ls_dependency.cpp` | The dirty graph and its propagation |
| `src/ls_compile.cpp` | The pipeline: regions → fills → layers → groups → sampling → quantisation |
| `src/ls_serialize.cpp` | JSON round trip, unknown-field preservation, version migration |
| `src/ls_package.cpp` | The ZIP container: stored entries only, validated names, CRCs |
| `src/ls_c_api.cpp` | The C ABI implementation |
| `src/ls_reflect.h` | Field tables, so clone and serialize walk operations generically |

### The editor — `C:\Users\wray2\AppDev\sprits-fast`

`src/app/` is `fast_core`: no SDL, no ImGui, fully testable headless.

| File | What it is |
|---|---|
| `document.h/.cpp` | The engine handle, the undo stack, the package's companion entries |
| `paint.h/.cpp` | `PaintLayer`, pencil and eraser, layer adoption from a reopened file |
| `brush.h/.cpp` | Brush size and shape, the pixel-perfect corner rule, pen pressure |
| `element.h/.cpp` | Several marks on one layer: its pixels plus any shapes |
| `shape.h/.cpp` | Rectangles, ellipses, lines that stay editable; outlines |
| `layers.h/.cpp` | Blend, opacity, order, copies, groups, clipping, locks |
| `palette.h/.cpp` | Slots as roles; several palettes; the swap |
| `palette_io.h/.cpp` | `.gpl` and `.hex` in and out |
| `dither.h/.cpp` | Dithered fills, ramps whose ends can name palette slots |
| `animation.h/.cpp` | Frames (a frame *is* a sprite), cycles, playback maths |
| `sheet.h/.cpp` | Sprite sheets and the manifest beside them |
| `transform.h/.cpp` | The non-destructive transform list |
| `bucket.h/.cpp` | Flood fill |
| `reference.h/.cpp` | Images to draw from, stored in the package |
| `image_io.h/.cpp` | The only place untrusted image bytes meet a parser |
| `library.h/.cpp` | Folder listings, thumbnails read without opening documents |
| `recovery.h/.cpp` | Autosave to a copy, and finding what a crash left |
| `file_io.h/.cpp` | UTF-8 paths, atomic writes, directory listing |
| `recent_files.h/.cpp` | The File menu's memory |
| `export_png.h/.cpp` | PNG output |
| `ui_state.h/.cpp` | View state stored in the package under `fast/ui-state.json` |

`src/ui/` is the window. It is **not** where behaviour lives — anything the
self-test needs to drive sits in `editor.cpp`, free of ImGui calls.

| File | What it is |
|---|---|
| `editor.h/.cpp` | All interface state, and the operations panels and shortcuts share |
| `app_window.cpp` | Main, the loop, layout, menus, shortcuts, tools, the demo, `--self-test` |
| `panels.cpp` | Tool, palette, layers, shape, transform, references, library |
| `timeline.cpp` | The frame strip, cycle editor, sheet dialog |
| `canvas_view.h/.cpp` | Zoom, pan, the artwork, underlay and overlay hooks |
| `frame_cache.h/.cpp` | One texture per frame, invalidated by the engine's `isDirty` |
| `reference_cache.h/.cpp` | Textures for reference images and library thumbnails |
| `file_commands.h/.cpp` | Native dialogs, which are asynchronous, and the unsaved prompt |
| `theme.h/.cpp` | Palette, metrics, icons |

### Where things are kept

Inside a `.lsprite` package, under `fast/`: `ui-state.json`, `thumbnail.png`,
`references.txt`, `ref/<id>.png`. Anything **not** under `fast/` belongs to
another application and is written back untouched.

In the user's settings directory: `recent.txt`, `library.txt`, `recovery/`.

---

## 4. Completion against the specification

`LIVESPRITE_ENGINE_SPEC.md` in the engine repo is the design document. Section
by section:

| § | Requirement | State |
|---|---|---|
| 2 | Operations as source truth | **Done.** The property the whole design rests on, and the editor demonstrates it |
| 3 | Project structure | **Done**, file for file |
| 4 | Data model — document/sprite/layer/group, shared geometry, operation stack, roles | **Done** |
| 5 | Compilation pipeline, profiles, dither coordinate spaces | **Done.** All four spaces; anchoring has its own test and doc |
| 6 | Dependency graph, dirty propagation, `compileDirtyOnly` | **Done.** One real gap found and fixed in September: a palette write dirtied only sprites bound by name |
| 7 | Serialization, unknown-field preservation, `migrateVersion`, packages | **Done.** Packages are stored-only ZIPs with validated names and CRCs |
| 8 | Plugin interface | **Done** for operations and patterns; `livesprite_plugin` covers it |
| 9 | What the engine must not contain | **Held.** No UI, no timeline, no undo presentation in the engine |
| 10 | Build order | **Complete**, in the order given |
| 11 | C++ conventions | **Held**, enforced by the build |
| 12 | Fast/Pract integration points | **Exercised** by Fast for real, which is how most engine bugs were found |

All 39 declared operations are handled in the compile pipeline — none are
declared-but-unimplemented. 18 engine tests, 24 Fast tests, all green on
Windows, Linux, macOS, plus a consume-the-installed-package job and a Python
`ctypes` job against the C ABI.

**The engine is spec-complete.** What is left there is polish and reach, not
missing design: the C ABI does not expose layer groups (engine issue #4), and
indexed PNG is still wanted.

### The editor against its own README

Fast has no separate specification; its README's *Status* section is the claim.
That section is now behind — it does not mention elements, the brush, pen
support, references, the libraries, or autosave. **Updating it is the first
small job for whoever picks this up**, and a good way to get oriented.

What Fast has that is genuinely unlike other pixel editors: shapes that stay
shapes, outlines that follow the artwork and can trace a whole figure across
layers, non-destructive transforms, dithers with pattern anchoring, palette
roles that recolour from the drawing, several palettes with a swap and a
per-frame binding, and elements so one layer can hold a rectangle, a line and
some pixels at once.

---

## 5. Deviations and open decisions

**References live in Fast, and the spec assigns a References Toolkit to
Pract.** Section 9 reserves `ImportGLB`, `ScrubVideo`, `ExtractPose` and
`ExtractPaletteFromPhoto` for Pract. What Fast gained is much smaller: import a
still image, place it, fade it, draw from it. The rule the spec is actually
protecting — that the *engine* must not contain it — is intact, since
references are package entries the engine never interprets. But if Pract is
meant to own references as a product boundary, this is the moment to say so,
before more is built on it. **Unresolved; needs a decision.**

**A reference is embedded, not linked.** Deliberate: a linked reference breaks
exactly when a half-finished drawing still needs it. Capped at 8 MB each and
16 MB per document.

**A project is a folder**, with no project file. Nothing to corrupt and nothing
to keep in step with the filesystem.

**There is no cut for layers**, only copy, paste and delete. A cut layer would
have to live somewhere while it waits.

**Elements cannot be reordered within a layer** yet, and a dither applies to
the freehand element only.

**Licensing**, tracked as engine issues #1 and #2: the Licensor is currently
the individual, to be renamed once Manic Trash Panda Software Labs is
registered — and copyright assignment must carry contribution grants. The
Additional Use Grant needs a lawyer before a commercial licence is sold on it.
`LICENSING.md` records both.

---

## 6. What to do next

Open issues, and the order that makes sense:

1. **Fast README status** — a few minutes, and it orients you.
2. **Fast #2, selection and move tools** — the largest missing thing in the
   editor, and the one with real design in it: done *in terms of operations*
   rather than pixels, or the one rule breaks.
3. **Fast #5** — palette presets, reorder, sort. Multiple palettes and the swap
   are done; check the licensing of any preset before shipping it.
4. **Fast #3 / engine indexed PNG** — smaller files that keep the palette, so a
   game can swap colours at run time. This is what a palette-native engine is
   for, and `third_party/README.md` already argues the case.
5. **Engine #4** — groups in the C ABI. Fast uses the C++ API so it is not
   blocked; a Python or Pract caller is.
6. **Fast #1** — the sheet manifest, before a bridge depends on its shape.
7. **Engine #3 / Fast #4** — bump `actions/checkout` to v5.

Not filed, and worth considering: elements reordering; a "save a copy" menu
item (`Document::saveCopy` already exists); thumbnail budgeting if canvases
grow past 256².

---

## 7. Traps that have already cost time

**Heredocs mangle backslashes and `\n`.** Writing C++ through a shell heredoc
silently ate an escape level more than once, producing `'\'` where `'\\'` was
meant. Use the Write and Edit tools, or a script file in the scratchpad. This
has bitten in three separate sessions.

**A failed build leaves a stale executable.** Filtering build output for
`error` and seeing nothing does not mean it built. An afternoon went into
debugging a feature that was working, through a binary that was three builds
old. Check that the link step ran.

**A running app locks its own `.exe`** and the next build fails to link.

**References into `editor.frames` or `editor.cycles` are invalidated by almost
anything.** `selectFrame` re-reads both lists, so a `const Cycle&` held across
a click is a dangling reference. This was a real crash on Play. Take copies.

**C ABI enums are hand-mirrored.** `LS_PALETTE_*` was once in the reverse
order of the C++ enum, so every C caller asking for unconstrained output got
quantisation, invisibly. Every mirror is now pinned by `static_assert`; keep it
that way when adding one.

**Sentinel values that collide with real values.** Autosave used
`lastWriteSeconds_ == 0` to mean "never written", and the clock genuinely is 0
for the first second, so it re-armed every frame and wrote nothing. The unit
test had dodged it by starting its clock at 1000.

**The engine's snapshot does not carry Fast's package entries.** `Document`'s
history entries carry them explicitly; without that, undoing a reference import
would restore the artwork and leave the image behind.

**Dear ImGui's SDL_Renderer backend owns the sampler** from 1.92.8 and
re-applies linear filtering to every texture it binds, so setting a texture's
scale mode is not enough. The canvas brackets its artwork with the backend's
nearest callback.

---

## 8. How the work is done here

Worth matching, because the codebase is consistent about it:

- **Comments say why, not what.** Most explain a decision or name the bug that
  forced it. Match the density; it is high on purpose.
- **Every promise gets a test that can fail.** `tests/system_tests.cpp` in Fast
  holds one realistic document to a few promises through everything at once —
  it is where seams get caught, and most recent bugs were found there rather
  than by using the program.
- **Commit messages are prose** explaining the decision, ending with the
  `Co-Authored-By` line.
- **CI is the gate**, six jobs on the engine and four on Fast.
- `docs/testing.md` is the script for what only a person at the window can
  judge. Several flows there have never been run by hand.
