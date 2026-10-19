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
- [ ] Idle redraw: the window draws every frame at vsync even when nothing changes
      (about 1.4 ms CPU a frame, 8% of a core); draw only when something did
- [ ] `app_window.cpp` (5,400 lines) split into modules: menus, dialogs, preferences,
      stroke input, shortcuts, file actions, self-test; checked against the gallery
- [ ] `panels.cpp` (3,500 lines) split by panel

### 2. Fuzzing
- [ ] A fuzz harness for every parser of untrusted input, with a seed corpus, run
      under AddressSanitizer: `.lsprite` package, Aseprite import, PNG/GIF/WebP
      decode, palette files (GPL, JASC, hex, PNG), theme files, keymap, preferences,
      i18n catalogue, slices/references/cycles metadata, batch command line,
      layout files, UI script
- [ ] Bounded quick runs in ctest (seeded, fast); long runs recorded here
- [ ] Every crash, hang and leak found fixed, with the input kept as a regression test

### 3. Features tested
- [ ] A matrix (`docs/qa-matrix.md`) of every feature in `parity.md` against the test
      that covers it
- [ ] Gaps filled with UI scripts or core tests
- [ ] The hand-run checks in `docs/testing.md` that a script can run, run

### 4. Help and resources
- [ ] In-app user guide: getting started, drawing, layers, animation, puppets,
      palettes, export, shortcuts, preferences and themes, troubleshooting
- [ ] Help menu: User guide, Shortcuts, Report a problem, About (licences)
- [ ] `README.md`, `CHANGELOG.md`, `SUPPORT.md`, `PRIVACY.md`, third-party notices

### 5. Store submission kit (`store/`)
- [ ] Logo set at every size the manifest needs (from `assets/icon`)
- [ ] `AppxManifest.xml` template (identity placeholders), file association
      `.lsprite`, `PerMonitorV2` DPI awareness, minimum Windows 10 1809
- [ ] `tools/make-msix.ps1`: release build, layout, `makeappx pack`, test-sign
- [ ] Store listing text (title, short and long description, features, keywords,
      what's new), screenshots at Store sizes, age-rating answers, certification notes
- [ ] Windows App Certification Kit run, results recorded

### 6. Final
- [ ] Release build from a clean tree; all suites, scripts, fuzz quick runs, gallery
- [ ] `HANDOFF.md` and `docs/handoff.md` updated; this file's "Resume here" current

### 7. Next (not started: the owner starts it)
- [ ] Pract brief: the Importer and the Arranger, written into `sprits-pract/docs/plan.md`

## Resume here

Item 1, idle redraw. (Update this line at every commit.)

## Traps found on the way

(Added as met.)
