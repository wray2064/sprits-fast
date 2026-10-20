# Handoff

Written for whoever picks up Sprit's'fast next, human or model, starting
cold. It covers the rules the code keeps, how to build and test it, the state
it is in, where everything lives, and the traps that have already cost time.
Last rewritten 2026-10-18. `docs/parity.md` is the checklist and the log,
newest first; read it next.

Fast is one of three projects kept side by side in one folder:
- the **LiveSprite engine** (`../livesprite-engine`, `wray2064/livesprite-engine`);
- **Fast** itself (this repository);
- **Sprit's'pract**, the proprietary suite that runs Fast's window as a
  library and adds its own toolkits.

The workspace's own handoff, kept beside the three, covers the plan across
them. This file covers Fast's code and the engine as Fast uses it.

---

## 1. The rules the code keeps

**The math is the truth; pixels are only output.** Strokes are paths with a
brush, fills are faces found between walls, erasing is subtractive geometry,
transforms move geometry, and patterns and dithers are evaluated at output.
- Never store pixel sets as truth. Never derive one layer's content from a
  compiled picture. Never bake or flatten to get a feature done.
- A defect in what a transform draws is fixed in the geometry and the
  rasterisers, never by moving pixels -- not even for quarter turns.
- Imported pictures are sources, traced into paths and faces on the way in.

**What Fast is.** Everything Aseprite, LibreSprite, Spriteloop and Pixelorama
do for a pixel artist, done better wherever the engine allows, plus a
complete cutout puppet system, posed and captured by hand. Multiview, 3D,
video, palette and silhouette extraction, atlases, IK and key poses belong to
Pract.

**Puppets stay engine-native.** A Fast puppet must open in Pract and work as
it is:
- parts are sprites;
- joints are pivots;
- sockets and hanging are engine sockets and attachments;
- a pose is the joints' rotations.
Nothing another program would need may live in Fast-only side data.

**Seams, not names.** Fast knows nothing of Pract. What Pract needs is a
generic seam: `fast::AppExtension` (menus, panels, timeline, icon, title,
preferences folder) and `fast::claimEntryPrefix`.

**Colours are roles.** An operation names a palette slot, and the palette
turns it into a colour at compile time. That is why palette swapping works
at all.

**Everything from a file is untrusted**: packages, palettes, images, UI
state. Bound it before allocating, refuse rather than repair, and say why in
words a person can act on.

---

## 2. Build, test, ship

From PowerShell in this folder:

| What | Command | Notes |
|---|---|---|
| Core and tests | `.\build.bat test` | `build\`, no window, a minute and a half; 53 suites |
| The window | `.\build-gui.bat` | `build-gui\sprits_fast.exe`; SDL3 the first time takes minutes |
| UI scripts | `.\tools\run_scripts.ps1` | 52 scripts, 938 checks; all must pass, and none may draw an ImGui error; CI runs them (one: `.\build-gui\sprits_fast.exe --script tests\ui\<name>.txt`) |
| What no test touches | `python tools\qa_matrix.py` | writes `docs\qa-matrix.md`: every menu item, command and core module against its tests; the gaps list is empty |
| Fuzzing | `.\tools\fuzz.ps1` | 21 targets under libFuzzer and AddressSanitizer; see `docs\fuzzing.md` |
| The gallery | `.\tools\gallery.ps1` | every screen in every theme at 100% and 150%, 270 images and a contact sheet per screen in `build-gui\gallery`; look at it before calling a visual change done |
| Release | `.\build-release.bat` | `dist\SpritsFast`: windowed, optimised, with `lang\` and the C++ runtime DLLs; copy the folder anywhere |

The engine is found at `../livesprite-engine` (`-DLIVESPRITE_DIR` to point
elsewhere); CI clones it from `vars.LIVESPRITE_REPO`. Build trees record
absolute paths, so after moving the folders, delete them and build again.

`tools\run_scripts.ps1` prints each script with its check count, and the lines
of any that failed; its exit code is how many did (`-Only menu_` runs the ones
with that in the name).

Useful flags:
- `--self-test`, `--script FILE`;
- `--shot PATH` (a BMP), `--frames N`, `--zoom N`, `--light`;
- `--reference FILE` (imports and opens the section window);
- `--export ... --to ...` (batch export, see `app/batch.h`);
- `--demo-stroke`.

The script language is in `src/ui/ui_script.h`: `click-on LABEL`, `drag`,
`drag-from`, `key`, `type`, `reference FILE`, and `expect ...` for pixels,
colours, elements, selection, tool, layers, frames, rotation, scale, offset
and section.

---

## 3. State

The parity checklist in `docs/parity.md` is **complete**: tiers 1 to 4. The
conversion to vector truth is complete as well, and old documents are
converted as they open. The newest work:

- **Cutout puppets** (`src/app/puppet.h`, `src/ui/puppet_panel.cpp`). The
  Puppet panel has three modes: Frames, Draw part and Pose.
  - A part is a sprite out of the timeline, marked `fast.part`. Its joint is
    its pivot; sockets are named points; a part hangs from a socket by an
    engine attachment, whose joint transform is the pose.
  - Pose mode shows the assembled puppet with bones, a ring that turns the
    picked part (Shift for 15 degrees) and a square that moves the root.
  - **Capture pose** makes a frame of placed shapes (`MatrixTransformOp`,
    `RoundingPolicy::Exact`), each part a track, with the pose written on the
    frame under `fast.pose`.
  - Joints sit in pixel middles (`jointPoint`), so a quarter turn is exact.
    A turned limb's fill keeps every pocket (the engine's `faceThrough`).
- **The transform gizmo.** With the Move tool, a ring turns (Shift for 15
  degrees), the arrows scale and the middle square moves the sprite. The
  Transform panel takes typed numbers.
- **Exact quarter turns and flips** for every kind of mark. Line ends live
  in pixel middles, and the engine resolves turned shapes from their turned
  math (`turning_tests`).
- **Reference sections** (`Reference::clip*`, `setReferenceSection`,
  `stepReferenceSection`, `src/ui/reference_section.cpp`). A reference can
  show one part of its image, such as a cell of a sprite sheet.
  - Importing opens the section window: drag on the preview, type x y w h,
    or give a cell size and gap and click a cell. < Cell / Cell > walk the
    sheet.
  - The whole image stays in the document. A magnified reference is drawn
    with nearest sampling.
- **The app icon** from the logo (`tools/make_icons.py`, `assets/icon/`),
  and the windowed release build.

Still English: status messages, strings built at run time, slider number
formats. The checks in `docs/testing.md` that need a person at the window
have mostly never been run. GitHub issues #2, #3 and #5 are done according
to the parity log and can be closed; #1 (a design pass on the manifest) and
#4 (`actions/checkout` v5) are open.

---

## 4. Where things are

`src/app/` is `fast_core`: no SDL, no ImGui, testable headless. Each file
starts with a comment saying what it is and why.
- **Document and history:** `document`, `recovery`, `file_io`, `ui_state`.
- **Marks:** `paint`, `brush`, `element`, `shape`, `ink`, `text`, `dither`,
  `bucket`, `floating`, `selection`, `clip_image`.
- **Layers and frames:** `layers`, `tracks`, `transform`, `animation`,
  `tilemap`, `slices`, `guides`, `grid_snap`.
- **The palette:** `palette`, `palette_io`, `palette_tools`, `adjust`.
- **The cutout rig:** `puppet`.
- **References:** `reference`.
- **Files in and out:** `import_image`, `import_aseprite`, `export_png`,
  `export_anim`, `webp`, `sheet`, `batch`, `canvas_ops`.
- **Settings and strings:** `keymap`, `preferences`, `i18n`, `text_util` (the
  helpers every parser here uses), `guide` (the user guide's Markdown and search).
- **Look and feel (no ImGui):** `theme_tokens` (the 38 colour roles, the nine
  themes, the contrast checker), `theme_store` (the person's `.lstheme` files),
  `vector_icon` (Lucide SVGs to strokes). Design and progress in
  `docs/ui-overhaul.md`.

`src/ui/` is the window.
- `editor` holds all interface state and the operations panels and
  shortcuts share. It makes no ImGui calls, so the self-test can drive it.
- `app_window.cpp` holds the window's layout and the main loop. What it once held
  is split, and shared through `app_internal.h`: `menu_bar` (the menus), `dialogs`
  (the windows that open from them: canvas and sprite size, adjust colours, the sheet
  import, history, text), `preferences_panel`, `file_actions` (open, save,
  close, quit and the questions in front of them), `stroke_input` (what the mouse
  does on the canvas), `shortcuts` (what the keys do), `headless` (the command line)
  and `self_test`.
- Panels: `panel_tools` (the toolbar and the tool options), `panel_palette`,
  `panel_elements`, `panel_layers`, `panel_references` (and the library),
  `panel_transform`, `panel_view` (the preview and the status bar), sharing
  `panels_internal.h`; `panels.h` is the list. Then `timeline.cpp`, `puppet_panel`,
  `reference_section`, `transform_gizmo`, `shape_tools`, `selection_tools`,
  `tile_tools`, `slice_tool`.
- `timeline_drawer`: the Timeline when it is not pinned -- a tab at the canvas's
  edge and a strip that opens over it (`showTimeline`, `toggleTimeline`,
  `pinTimeline`); the strip itself is `timeline.cpp`.
- `user_guide`: Help > User guide (F1), the guide's window; its text is Markdown
  under `assets/help/`, compiled in, and `tests/guide_tests.cpp` checks every link.
- `theme` (colours, fonts, spacing, the one `drawIcon` call), `widgets` (the
  component kit; Window > Interface kit shows it), `appearance` (Preferences >
  Appearance) and `layout` (the dock: three built arrangements and the
  person's own, saved by name).
- `canvas_view` and `frame_cache` draw the canvas.
- `ui_script` runs the scripts.
- `app_extension.h` is the seam another program builds on.

Other docs:
- `docs/architecture.md`: why Fast is built the way it is.
- `docs/testing.md`: what the automated layers promise, and the hand-run
  script.
- `docs/translating.md`: adding a language.
- `docs/icons-needed.md`: the icon set (Lucide) and the icons still to add.
- `docs/visual-language.md`: the working reference for building a screen --
  roles, type, space, components, icons, themes, and what a program built on
  Fast adds. Read it before drawing anything.
- `docs/ui-overhaul.md`: the design, the stages and where they stand.
- `README.md`: the feature tour.

Inside a `.lsprite` package, under `fast/`:
- `ui-state.json` and `thumbnail.png`;
- `references.txt`, where a reference's section is fields 9 to 12, written
  only when it is clipped;
- `ref/<id>.png`.
Anything not under `fast/` belongs to another application and is written
back untouched. User settings live in a `SpritsFast` folder, or in the folder
an extension names: preferences, keys, recent files, libraries, `layout.ini`,
`recovery/`.

The engine's model is in `../livesprite-engine/docs/concepts.md`.
`LIVESPRITE_ENGINE_SPEC.md` is its implementation contract;
`Livesprite architecture.txt` is the wider design.

---

## 5. Traps that have already cost time

- **Stale objects.** After changing a header in the engine or Fast, delete
  the `.obj` files in the build tree before trusting a run. For
  `build-gui\`, deleting `build-gui\livesprite` and the
  `fast_core`/`sprits_fast` object folders avoids rebuilding SDL.
- **Stale links.** A crash before `main` prints anything is a stale
  incremental link: delete the `.exe` and `.ilk` files. Both repositories
  link with `/INCREMENTAL:NO` because of it.
- **A failed build leaves the old `.exe`**, so check that the link step ran.
  A running window locks its own `.exe`.
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
  really is 0 in its first second.
- **The engine's snapshot does not carry Fast's package entries.**
  `Document`'s history carries them, or undoing an import would leave its
  image behind.
- **`ImDrawList::AddRect` takes thickness before flags** since ImGui 1.92.8.
  `AddRect(a, b, col, round, ImDrawFlags_RoundCornersTop)` compiles and means a
  48-pixel outline (the enum converts to the float). Pass both
  (`round, 1.f, flags`); the six-argument call in the old order still works.
- **A window that may scroll sideways widens its work rect** to the last
  frame's widest content, so full-width rows follow the widest thing and never
  shrink, and one that always may shows a bar with nothing to scroll after a
  vertical bar has come and gone. Open Fast's panels with `beginPanel`
  (`ui/layout.h`), and measure overflow with `window->ScrollMax.x`, not by
  comparing against `WorkRect`.
- **A window cannot be smaller than `WindowMinSize`** (32 pixels, scaled): a
  24-pixel tab drawn in one hangs over what is below it.
- **A script can fail once at random while the desktop is in use** (see
  `docs/testing.md`): rerun it before looking for a cause.
- **The window is idle when nothing happens.** The main loop draws at full rate
  while there is input, a drag, typing or playback, and for 600 ms after, and
  otherwise waits for an event for up to 100 ms (about 1% of a core instead of 20%).
  Anything that changes what is on screen *without* an event -- on a timer, as
  the timeline drawer's hover delay does -- must call `editor.stayAwake(ms)` so the
  change settles at full rate. Scripted runs and flag-counted runs are never idle.
- **Shell escapes.** Never write code through a Bash heredoc, `python -c`,
  or `sed` with `\n`. Use an editor, or a script written with
  `newline='\n'` (the repository is LF).

---

## 6. How the work is done here

- Comments say why, not what, and the density is high on purpose. Match it.
- Every promise gets a test that can fail, and every interaction a UI
  script. `tests/system_tests.cpp` holds one realistic document to several
  promises at once.
- One undo step per user action; a drag is bracketed into one.
- Commit messages are prose explaining the decision.
- `docs/parity.md` is ticked, and its log written, in the commit that lands
  the change.
- CI is the gate.
