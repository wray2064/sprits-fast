# Handoff

Written for whoever picks this up next, human or model, starting cold. It says
what the three products are, the decisions the user has made that govern all
of them, where everything lives, what state each one is in, what comes next,
and the traps that have already cost time. Last rewritten 2026-10-18, for a
move to a fresh session.

**Read in this order:** this file; then `docs/parity.md` (Fast's checklist
and log, newest first); then, for Pract work, `../sprits-pract/CLAUDE.md`
and `../sprits-pract/docs/plan.md`; for engine work, the engine's
`docs/concepts.md`.

---

## 1. The three products

| | Where | Remote | Licence | Tests |
|---|---|---|---|---|
| **LiveSprite** (engine) | `C:\Users\wray2\AppDev\sprit s pract` (spaces in the name) | `wray2064/livesprite-engine` (public) | BSL 1.1, Change Date 2030-09-06 | 19 suites |
| **Sprit's'fast** (free editor) | `C:\Users\wray2\AppDev\sprits-fast` | `wray2064/sprits-fast` (public) | Apache-2.0 | 43 suites + 23 UI scripts |
| **Sprit's'pract** (paid suite) | `C:\Users\wray2\AppDev\sprits-pract` | **none, ever** | proprietary | 3 suites + 1 UI script |

- **The engine** holds a sprite as operations (fills, strokes, faces,
  transforms, deforms, outlines -- 43 operation types) and compiles them to
  pixels. Apps hold opaque IDs and never get called back.
- **Fast** is the editor and the engine's free showcase. It is also the
  Canvas toolkit of Pract: Pract runs Fast's window as a library.
- **Pract** is Fast's editor plus what Fast leaves out: References
  (interpreting source material), the Arranger (atlases, engine exports),
  a better animation system, and Practipuppets (IK, key poses, ramps).
- `C:\Users\wray2\AppDev\spritspract` is an old C# prototype. Leave it alone.

Fast finds the engine through `LIVESPRITE_DIR`, defaulting to `../sprit s
pract`; Fast's CI clones it from `vars.LIVESPRITE_REPO`. Pract builds on
`../sprits-fast` (`fast_core`, `fast_ui`) and through it on the engine.

---

## 2. The user's decisions, which govern everything

These came from the user directly. They are not style preferences.

**The math is the truth; pixels are only output.** In the user's words, "at
no point pixels are source of truth, always the math, and the math must
always resolve to accurate pixels, not the other way around."
- Strokes are paths with a brush, fills are faces found between walls,
  erasing is subtractive geometry, transforms move geometry, and patterns and
  dithers are evaluated at output.
- Never store pixel sets as truth. Never process a compiled picture to make
  another layer's content. Never bake or flatten to get a feature done.
- A defect in what a transform draws is fixed in the geometry and the
  rasterisers, never by rasterising first and moving pixels -- not even for
  exact quarter turns.
- The only pixels that come in are sources: imported images and references.
  An imported picture is traced into paths and faces on the way in.

**What goes where (decided 2026-09-25 and 2026-09-27):**
- **Fast:** everything Aseprite, LibreSprite, Spriteloop and Pixelorama do,
  done better wherever the engine allows. That includes a still reference
  image (now with sections of a sheet), "export as sheet", and the **entire
  cutout puppet system**: parts, joints, sockets, posing by hand, and
  capturing each pose as a frame. Animation in Fast is hand-made.
- **Pract:** multiview, 3D, video, palette and silhouette extraction, the
  Arranger, and on top of Fast's puppets IK resolvers, movement ramps, key
  poses and automated motion.
- **Critical:** a Fast puppet must open in Pract and work as it is. Puppet
  data stays engine-native, in the document: parts are sprites, joints are
  pivots, sockets and hanging are engine sockets and attachments, a pose is
  the joints' rotations. Nothing Pract would need may live in Fast-only side
  data.

**Rules of work:**
- Engine and Fast changes are pre-approved. Commit straight to `main` and
  **push** both (CI gates them). Pract commits locally only: never add a
  remote, never push, never publish its code.
- Commit messages are prose explaining the decision, ending with
  `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>` (or the line the
  current harness gives).
- Never copy GPL code (LibreSprite included) or presets with unclear
  licences: reimplement from the behaviour. Never write `.aseprite` files;
  reading them is fine.
- Every interaction gets a UI script (`tests/ui/*.txt`), because tests that
  call functions directly missed a broken drag the user then found by hand.
- UI strings go through `fast::tr()`; the Spanish catalogue is
  `assets/lang/es.txt`.
- Docs move with the code: tick `docs/parity.md` and add a log line at the
  top of its Log in the same commit.
- Recovery copies and test documents are disposable: "all of this is for
  testing and can be lost."

---

## 3. Build, test, ship

Run the `.bat` files from **PowerShell**, setting the folder first
(`Set-Location <repo>; .\build.bat test`); `cmd /c` from Bash does not find
them.

| What | Command (in the repo) | Notes |
|---|---|---|
| Engine | `.\build.bat test` | 19 suites |
| Fast core | `.\build.bat test` | `build\`, no window, seconds; 43 suites |
| Fast window | `.\build-gui.bat` | `build-gui\sprits_fast.exe`; SDL3 the first time takes minutes |
| Fast UI scripts | `.\build-gui\sprits_fast.exe --script tests\ui\<name>.txt` | all 23 must pass; the language is in `src/ui/ui_script.h` |
| Fast release | `.\build-release.bat` | `dist\SpritsFast`: windowed, optimised, with `lang\` and the C++ runtime DLLs beside it |
| Pract | `.\build.bat test` | 3 suites; `.\build\sprits_pract.exe --script tests\ui\pract_window.txt` |

To run every Fast UI script (exit code 0 each):

```powershell
foreach ($f in Get-ChildItem tests\ui\*.txt) { $q = Start-Process build-gui\sprits_fast.exe -ArgumentList '--script',"tests\ui\$($f.Name)" -NoNewWindow -Wait -PassThru; if ($q.ExitCode) { $f.Name } }
```

Useful flags: `--self-test`, `--script FILE`, `--shot PATH` (BMP), `--frames
N`, `--zoom N`, `--light`, `--reference FILE` (imports and opens the section
window), `--export ... --to ...` (batch export), `--demo-stroke`.

**The test build.** The user took a release build on a train for an offline
test (2026-09-29); its results have not been reported yet. **Ask for them
first.** Copies of that build:
- `D:\SpritsFast` (the USB stick labelled "dropship"; the other folders there
  belong to other projects);
- `C:\Users\wray2\OneDrive\Desktop\SpritsFast` and `SpritsFast.zip` beside it
  (pinned to stay on the device);
- `C:\Users\wray2\SpritsFast`.

All were built from Fast `366738f`, hash-checked, and passed `--self-test`
and the UI scripts from where they sit. Rebuild with `build-release.bat` when
Fast changes; `build-release\` and `dist\` are ignored by git.

---

## 4. State

**All green at last run (2026-09-28):** engine 19/19, Fast 43/43, UI scripts
23/23, Pract 3/3, the release exe's self-test and all 23 scripts.

| Repo | HEAD before this rewrite | Working tree |
|---|---|---|
| engine | `d11922a` Look for pockets only when the flood missed something | clean (a stray `imgui.ini` is untracked) |
| Fast | `366738f` A release build to hand out | clean |
| Pract | `38499b9` Pract starts at WinMain too | clean, local only |

### Fast

The parity checklist in `docs/parity.md` is **complete**: tiers 1 to 4, from
selections, inks and the palette to tilemaps, per-cel opacity, Aseprite
import, RotSprite and tweens. The vector-truth conversion is complete too:
everything drawn is paths, faces and areas, and old documents are converted
as they open. The newest work, all in the log:

- **Cutout puppets** (`src/app/puppet.h`, `src/ui/puppet_panel.cpp`; the
  Puppet panel has Frames / Draw part / Pose):
  - A part is a sprite out of the timeline, marked `fast.part`. Its joint is
    its pivot; sockets are named points; a part hangs from a socket by an
    engine attachment, whose joint transform is the pose.
  - Pose mode shows the assembled puppet with bones, a ring to turn the
    picked part (Shift for 15 degrees) and a square to move the root.
  - **Capture pose** makes a frame of placed shapes (`MatrixTransformOp`,
    `RoundingPolicy::Exact`), each part a track, with the pose written on the
    frame under `fast.pose`.
  - Joints sit in pixel middles (`jointPoint`), so a quarter turn is exact.
  - A turned limb's fill once showed holes (448 on the user's file); the
    engine's `faceThrough` now keeps every pocket, and the count is 0.
- **The transform gizmo**: with the Move tool, a ring turns (Shift for 15
  degrees), the arrows scale, the middle square moves the sprite, and the
  Transform panel takes typed numbers.
- **Exact quarter turns and flips** for every kind of mark: line ends live in
  pixel middles, and the engine resolves turned shapes from their turned math
  (`turning_tests`).
- **Reference sections**: a reference can show one part of its image -- a
  cell of a sprite sheet (`Reference::clip*`, `setReferenceSection`,
  `stepReferenceSection`). Importing opens the Reference section window:
  - drag on the preview, type x y w h, or give a cell size and gap and click
    a cell;
  - < Cell / Cell > walk the sheet;
  - Section... in the References panel reopens the window later.
  The whole image stays in the document. A magnified reference is drawn with
  nearest sampling.
- **The app icon** from the user's logo (`tools/make_icons.py`,
  `assets/icon/`), and the windowed release build.

Still English: status messages, strings built at run time, slider number
formats.

### Engine

Spec-complete (`LIVESPRITE_ENGINE_SPEC.md`), with the wider design in
`Livesprite architecture.txt`, which also defines Pract. The recent
additions, all documented in `docs/concepts.md` and `docs/operations.md`:
- pivots, sockets, attachments and assemblies (`compileAssembly`,
  `isAssemblyCurrent`, `dropAssembliesAbove`);
- `RoundingPolicy::Exact`;
- `MatrixTransformOp`;
- faces with walls and pockets (`FaceDesc`, `fillableIn`);
- region clips, strokes with erases, `geom::traceStrokes`;
- tilemaps, `FadeOp`, `ClearRegionOp`.

### Pract

Founded 2026-09-27 on Fast through generic seams (`fast::AppExtension`,
`fast::claimEntryPrefix`, a preferences folder, the Timeline window). It has:
- References: palette extraction, silhouette, and trace, all reading the
  shown section;
- the Arranger: MaxRects atlases, JSON hash and array, Godot SpriteFrames,
  `--arrange`;
- a versioned home for the animation system.
The roadmap is `docs/plan.md`.

---

## 5. What comes next

1. **Ask how the train test went**, and fix what it found first.
2. **Pract's animation system**: ask the user what "better" means before
   designing it; the candidate list is in Pract's `docs/plan.md`. Then
   Practipuppets on Fast's rig (IK, key poses, ramps). Pract also needs a
   logo from the user for its own icon.
3. **GitHub housekeeping**:
   - Fast issues #2 (selection), #3 (indexed PNG) and #5 (palette) are done
     according to the parity log but still open; confirm, then close them.
   - Still genuinely open: Fast #1 (manifest design pass), Fast #4 and
     engine #3 (`actions/checkout` v5), engine #4 (layer groups in the C
     ABI).
   - Engine #1 and #2 are licensing and need the user: the company as
     Licensor, and a lawyer's review of the Additional Use Grant.
4. Smaller: the remaining English strings; the hand-run checks in
   `docs/testing.md`, several of which have never been run at the window.

---

## 6. Where things are

### Fast

`src/app/` is `fast_core` -- no SDL, no ImGui, testable headless. Each file
starts with a comment saying what it is and why. The main ones:
- **document and history:** `document`, `recovery`, `file_io`, `ui_state`;
- **marks:** `paint`, `brush`, `element`, `shape`, `ink`, `text`, `dither`,
  `bucket`, `floating`, `selection`, `clip_image`;
- **layers and frames:** `layers`, `tracks`, `transform`, `animation`,
  `tilemap`, `slices`, `guides`, `grid_snap`;
- **the palette:** `palette`, `palette_io`, `palette_tools`, `adjust`;
- **the cutout rig:** `puppet`;
- **references:** `reference`;
- **files in and out:** `import_image`, `import_aseprite`, `export_png`,
  `export_anim`, `webp`, `sheet`, `batch`, `canvas_ops`;
- **settings and strings:** `keymap`, `preferences`, `i18n`.

`src/ui/` is the window:
- `editor` holds all interface state and the shared operations, free of
  ImGui calls;
- `app_window.cpp` has the loop, menus, shortcuts and flags;
- the panels: `panels.cpp`, `timeline.cpp`, `puppet_panel`,
  `reference_section`, `transform_gizmo`;
- the tools: `shape_tools`, `selection_tools`, `tile_tools`, `slice_tool`;
- `canvas_view` and `frame_cache` draw the canvas;
- `ui_script` is the script runner;
- `app_extension.h` is the seam Pract uses.

The other docs:
- `docs/architecture.md`: why Fast is built the way it is;
- `docs/testing.md`: what the automated layers promise, and the hand-run
  script;
- `docs/translating.md`: adding a language;
- `docs/icons-needed.md`: icons the user is to source;
- `README.md`: the user-facing feature tour.

Inside a `.lsprite` package, under `fast/`: `ui-state.json`, `thumbnail.png`,
`references.txt` (a reference's section is fields 9-12, written only when it
is clipped), `ref/<id>.png`. Anything not under `fast/` belongs to another
application and is written back untouched; Pract claims `pract/`. User
settings live in Fast's settings folder (`SpritsFast`; Pract uses
`SpritsPract`): preferences, keys, recent files, libraries, `layout.ini`,
`recovery/`.

### Engine

- `include/livesprite/`: the public API (`ls_api.h`), operations
  (`ls_operations.h`), types, and the C ABI (`livesprite_c.h`);
- `src/`: geometry, context, dependency graph, compile, serialisation,
  package, C ABI;
- `docs/`: concepts, operations, file format, embedding, getting started,
  performance and limits.

---

## 7. Traps that have already cost time

- **Shell escapes.** Never write code through a Bash heredoc, `python -c`, or
  `sed` with `\n` in the replacement: backslashes and backticks get mangled.
  Use the Edit and Write tools, or a Python script in the scratchpad written
  with `newline='\n'` (the repos are LF). Heredocs are fine for commit
  messages only.
- **PowerShell piping corrupts line endings** (`git diff | git apply`). Use
  Bash with a patch file.
- **Never `git checkout <file>` to undo a temporary edit** in a file with
  other uncommitted work; it once dropped four CMake targets.
- **Stale objects.** After changing a header in the engine or Fast, delete
  the `.obj` files in the build tree before trusting a run. For
  `build-gui\`, deleting `build-gui\livesprite` and the
  `fast_core`/`sprits_fast` object folders avoids rebuilding SDL.
- **Stale links.** A crash before `main` prints anything is a stale
  incremental link: delete the `.exe` and `.ilk` files. Both repos link with
  `/INCREMENTAL:NO` because of it.
- **Stale executables.** A failed build leaves the old `.exe` in place:
  check that the link step ran.
- **A running window locks its `.exe`.** Check the process before
  rebuilding; close it only if it is a test window, not the user's.
- **The compiler is Spanish.** `VSLANG=1033` is set by the build scripts.
- **WinMain.** A windowed (WIN32-subsystem) build needs WinMain;
  `#include <SDL3/SDL_main.h>` in the file with `main` supplies it.
- **ImGui's SDL_Renderer backend owns the sampler** and re-applies linear
  filtering. The canvas and magnified references bracket their drawing with
  `DrawCallback_SetSamplerNearest`.
- **In UI scripts, `nameForScripts` must be called after the widget is
  drawn.**
- **References into `editor.frames` or `editor.cycles` dangle** after almost
  any call (`selectFrame` re-reads them): take copies.
- **C ABI enums are hand-mirrored**, each pinned by `static_assert`; keep it
  that way.
- **Sentinels that collide with real values.** For example, a clock that
  really is 0 in the first second.
- **The engine's snapshot does not carry Fast's package entries.**
  `Document`'s history carries them, or undoing an import would leave its
  image behind.
- **The Desktop is in OneDrive.** Files there can become online-only, so
  anything needed offline goes outside OneDrive as well.

---

## 8. How the work is done here

- Comments say why, not what, and the density is high on purpose. Match it.
- Every promise gets a test that can fail. Interactions get a UI script.
  `tests/system_tests.cpp` holds one realistic document to several promises
  at once.
- One undo step per user action; a drag is bracketed into one.
- Everything from a file is untrusted: bound it before allocating, refuse
  rather than repair, and say why in words a person can act on.
- `ls::Result<T>` throughout the engine: no exceptions across the API, no
  RTTI. The same sprite compiles to the same bytes on every compiler;
  `livesprite_determinism` holds the golden hashes.
