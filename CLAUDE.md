# Sprit's'fast — working here

Read `docs/handoff.md` first: what the three products are (the LiveSprite
engine in `../sprit s pract`, this editor, and Sprit's'pract in
`../sprits-pract`), the decisions that govern them, the state, what comes
next, and the traps. Then `docs/parity.md`: the checklist and the log,
newest first.

The rules, in short:

- **The math is the truth; pixels are only output.** Strokes are paths,
  fills are faces between walls, erasing is subtraction, transforms move
  geometry. Never store pixels as truth, never derive one layer's content
  from a compiled picture, never bake or flatten to get a feature done.
- Fast has everything a pixel editor has, plus the full cutout puppet system
  (posed and captured by hand). Pract (proprietary, local only) adds IK, key
  poses, ramps, 3D, video, extraction and the Arranger. A Fast puppet must
  open in Pract as it is, so its data stays engine-native in the document.
- Commit straight to `main` with prose messages ending in the
  `Co-Authored-By` line, and push. Engine changes are pre-approved: make
  them there, with tests and docs, and push that repo too.
- Never copy GPL code or presets with unclear licences. Never write
  `.aseprite` files.
- Every interaction gets a UI script in `tests/ui/`, run with
  `build-gui\sprits_fast.exe --script FILE`. UI strings go through `tr()`,
  with Spanish in `assets/lang/es.txt`. Tick `docs/parity.md` and log the
  change in the same commit.
- Build from PowerShell with the folder set: `.\build.bat test` (core and
  tests), `.\build-gui.bat` (the window), `.\build-release.bat` (a copy to
  hand out, in `dist\SpritsFast`).
- Never write code through a heredoc, `python -c` or `sed` with `\n`: use
  the Edit and Write tools, or a script written with `newline='\n'`. After a
  header change, delete the `.obj` files before trusting a run.
