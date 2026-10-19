# Gold release: Sprit's'fast, ready for the Microsoft Store

Asked for 2026-10-19: "prepare Fast for gold-level publication, finish the cleanup,
test features, fuzz test and prepare the app help and resources; it must be ready
for Microsoft Store publication." Worked autonomously while the owner was away,
across session limits; **this file is how a session picks up where the last one
stopped.** Read it, then `HANDOFF.md` and `docs/handoff.md`, then continue at
"Resume here".

## Decisions taken (the owner can overrule any of them)

- **Version 1.0.0** (the Store package is `1.0.0.0`; the last number is reserved by
  the Store). One place to change it: `project(... VERSION ...)` in CMake, which
  feeds `FAST_VERSION`, the Windows version resource and the package manifest.
- **Free, no purchases, no telemetry, no network.** Fast contains no network code;
  the only outbound action is `SDL_OpenURL` for a link a person clicks in About.
  The privacy policy says so, and `tests/` keeps it true (no network API may
  appear in `src/`).
- **MSIX, x64, full-trust desktop app** (`runFullTrust`), signed by the Store at
  submission. A test-signed package is built only to check it; no certificate is
  ever installed or trusted on the owner's machine by this work.
- **Built-in, searchable, offline user guide** (Help > User guide), in English;
  the interface itself is Spanish-capable. The guide is Markdown under
  `assets/help/`, compiled into the program.
- **Only the owner can:** open the Partner Center account and reserve the name
  (that gives the package's `Identity Name` and `Publisher`, left as placeholders in
  the manifest template), host the privacy policy at a public URL, accept the Store
  terms, answer the age-rating questionnaire, and press Submit.
- Fast and the engine commit to `main` and push (pre-approved). Pract and this
  workspace's repository stay local.

## The checklist

Status: `[ ]` not started, `[~]` partly, `[x]` done. Each item is a commit.

### 1. Cleanup and speed
- [x] Shared text helpers (`app/text_util`), dead functions removed, the gallery
      comparison tool (`tools/compare_gallery.ps1`)
- [x] Idle redraw (21% of a core idle -> about 1%): the window drew every frame at vsync even when nothing changed
      (about 1.4 ms CPU a frame, 8% of a core); draw only when something did
- [x] `app_window.cpp` (5,700 lines) split into menu_bar, dialogs, preferences_panel,
      file_actions, stroke_input, shortcuts, headless and self_test (what they share is
      `ui/app_internal.h`); 1,100 lines remain, the window and the main loop; unneeded
      includes pruned by trying; checked against the 49 scripts and the gallery
- [ ] `panels.cpp` (3,700 lines) split by panel

### 2. Fuzzing
- [x] A fuzz harness for every parser of untrusted input, with a seed corpus, run
      under AddressSanitizer: 20 targets in `tests/fuzz` (package, Aseprite parse and
      import, images, palettes, themes, keymap, preferences, catalogue, cycles, guides,
      slices, references, recent colours, view state, SVG, batch line, string helpers,
      document metadata); `build-fuzz.bat` + `tools/fuzz.ps1`
- [x] Quick seeded run in ctest (`fast_fuzz_quick`), regressions folder run every time
- [x] Long guided runs recorded in `docs/fuzzing.md` (run 1: every target 120 s, 4.9
      million executions; run 2: the heaviest targets 5 to 20 minutes): no crash, hang or
      invariant break found, so `tests/fuzz/regressions/` holds nothing yet

### 3. Features tested
- [x] A matrix (`docs/qa-matrix.md`, `python tools/qa_matrix.py`) of every menu item,
      keymap command and core module against the scripts and tests that touch it,
      written from the code; CI checks it is current
- [x] Gaps filled: 63 menu items, 22 commands and one module were untouched; now none
      (Paste as reference needs a system-clipboard picture and says so). Nine new UI
      scripts, `text_util_tests`; 49 scripts and 53 suites pass
- [x] A script fails on any ImGui error; that found a real bug (hiding the Preview left
      the canvas window with a cursor ImGui flagged every frame), fixed
- [ ] The hand-run checks in `docs/testing.md` that a script can run, run

### 4. Help and resources
- [x] In-app user guide (Help > User guide, F1): 15 topics under assets/help (welcome, getting started,
      drawing, selecting, colour and palettes, layers, transforms, animation, puppets, tiles, references,
      files, appearance, shortcuts, troubleshooting) and the licences; a Markdown parser (app/guide),
      search, links; fuzzed
- [x] Help menu: User guide (F1), Search commands, Keyboard shortcuts, About (licences, report a problem)
- [x] `README.md` (get it, get help), `CHANGELOG.md`, `SUPPORT.md`, `PRIVACY.md`, third-party notices

### 5. Store submission kit (`store/`)
- [x] Logo set at every size the manifest needs (`tools/make_store_assets.py`)
- [x] `AppxManifest.xml.in` (identity placeholders), file association `.lsprite`,
      minimum Windows 10 1809, `runFullTrust`; version 1.0.0 and a Windows version
      resource from the one `project(... VERSION ...)`
- [x] `tools/make-msix.ps1`: release build, layout, resources.pri, `makeappx pack`
      (built and validated: `dist\SpritsFast-1.0.0.0-x64.msix`, 4.4 MB); `-TestSign` is
      written but never run here (it would create a certificate)
- [x] Listing text in English and Spanish, screenshots at 1440x900, certification notes,
      age-rating answers, `store/README.md` (the submission walkthrough)
- [x] `PRIVACY.md`, `SUPPORT.md`, `CHANGELOG.md`, `THIRD_PARTY_NOTICES.txt`
      (`tools/make-notices.ps1`)
- [x] Ship `THIRD_PARTY_NOTICES.txt`, `LICENSE`, `CHANGELOG.md`, `PRIVACY.md` in the package and the
      release folder; per-monitor DPI awareness and long paths in an embedded application manifest
- [ ] Windows App Certification Kit: needs a machine that trusts a test certificate;
      left to the owner (documented in `store/certification-notes.md`)

### 6. Final
- [ ] Release build from a clean tree; all suites, scripts, fuzz quick runs, gallery
- [ ] `HANDOFF.md` and `docs/handoff.md` updated; this file's "Resume here" current

### 7. Next (not started: the owner starts it)
- [x] Pract brief: the Importer and the Arranger, written into `sprits-pract/docs/plan.md`
      (committed locally; Pract is not pushed)

## Resume here

Left: split `panels.cpp` by panel (`ui/panels_*.cpp`, sharing `ui/panels.h`), checked by
the scripts and `tools/compare_gallery.ps1` against a gallery taken before; then the
hand-run checks, then item 6 (clean release build, all suites and scripts, the gallery and
Store screenshots regenerated, `tools\make-msix.ps1` again), `HANDOFF.md` and
`docs/handoff.md`. (Update this line at every commit.)

## Traps found on the way

- A UI script that clicks a menu path must say `menu File > Export PNG > 2x`, not
  `click-on` each label with no wait: a File clicked while the menu is still closing shuts
  it. And the first drawn widget of a name is the menu's (the menu bar is drawn before the
  panels, some of which have a `2x` of their own).
- Scripted runs once read and wrote the person's own recent files and library folders.
  They keep them in memory now (`RecentFiles::keepInMemory`, `LibraryFolders::inMemory`);
  anything new that persists must do the same, or a test will overwrite a real setting.
- `IMGUI_DISABLE_OBSOLETE_FUNCTIONS` is on, so `SetCursorScreenPos` back to a place past
  the last item, with no item after it, is an ImGui error every frame. End such a block
  with an item (`Dummy(0, 0)`); the script runner now fails on it.
- PowerShell 5's `Set-Content -Encoding utf8` writes a BOM and CRLF. The repo is LF without
  a BOM: write files with the Write tool or `[IO.File]::WriteAllText` and a
  `UTF8Encoding($false)`.
