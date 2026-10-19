# Opening, saving and exporting

## Your files

Fast's own file is `.lsprite`. It keeps everything: layers, elements, frames, palettes, cycles, slices,
references, puppets and the view you left. A file written by Fast opens in later versions, and anything in a
file that this version does not understand is written back out unchanged.

## Opening

**File > Open...** (`Ctrl+O`), drag-and-drop onto the window, or double-click in Explorer. It reads:

| File | What you get |
|---|---|
| `.lsprite` | the work, exactly |
| `.aseprite`, `.ase` | the work: layers (names, opacity, visibility, blend, groups), frames and their holds, tags as cycles, the palette, tilemaps |
| PNG, JPEG, BMP | a drawing taken apart by colour; at 256 colours or fewer the colours become the palette |
| GIF | every frame, held as long as the GIF said |

Saving an opened picture asks where the `.lsprite` goes: the image it came from is never written over.
A photograph with thousands of colours is refused with the suggestion to import it as a
[reference](topic:references).

**Tabs**: every file opened or made goes in a tab beside the work rather than over it (a blank, untouched document
is simply replaced). `Ctrl+Tab` steps through them, `Ctrl+W` closes one. A file already open is shown, not opened twice.
**File > Open recent** lists the last ones.

## Saving

- **Save** (`Ctrl+S`), **Save as...** (`Ctrl+Shift+S`), **Save a copy...** (writes somewhere else without changing
  which file you are editing).
- Saves are atomic: a file is written whole or not at all, so a failure never leaves half a sprite.
- Closing a tab or quitting asks about every document with unsaved work.

## Recovery

While you have unsaved changes, a copy of the document is written every couple of minutes into the program's
settings folder. **It is never written over the file you are editing.** The copy is deleted when you save properly
or close normally, so a copy found at startup means the last session ended badly. Fast offers it: **Open**
loads it as an unsaved document (you choose where it really goes), **Discard** throws it away, **Later** leaves it for next time.

**File > Autosave** turns it off, or **Write a copy now**. The interval is in Preferences (*Autosave a copy every...*).

## Exporting

Export compiles at export quality, leaves your document untouched, and exporting is not saving.

- **File > Export PNG** writes one picture at 1x to 16x. Scaling duplicates whole pixels, never interpolates.
  **Indexed** writes a colour table and one byte a pixel, with the palette's slots in their order as the table's first
  entries: slot 3 is index 3. A game that swaps palettes at run time swaps the table.
- **File > Export animation...** writes the selected cycle in its loop mode, or every frame, as an **animated GIF**
  (plays everywhere; 256 colours a frame and no partial transparency; the status line says if a frame was reduced),
  an **animated PNG** (every colour and every level of transparency exactly), a lossless **animated WebP**, or a
  numbered **PNG sequence**.
- **File > Export sheet...** lays the frames into one image: a grid, a row or a column, at a whole-number scale, from the
  selected cycle or every frame. **Border** and **spacing** keep a texture filter from bleeding one frame into the next;
  **Trim** cuts every cell to the smallest rectangle holding anything any frame draws (one rectangle for all, so the cells
  line up). Beside it goes a small `.json` naming each cell's position and hold, the frame names, the cycles and the
  slices, in Fast's own layout or in the JSON Aseprite writes (keyed or listed), which game-engine importers built for
  Aseprite already read.
- **File > Export again** (`Ctrl+Shift+E`) repeats the last export to the same file with the settings as they are now.

Every cell of a sheet is byte-for-byte what exporting that frame alone gives.

## Palettes

**Load** and **Save** in the Palette panel read and write `.gpl`, `.hex`, `.pal` and `.act`. See
[Colour and palettes](topic:colour).

## From the command line

The free download (not the Store version, which has no command-line alias) exports without a window, for a build script:

```
sprits_fast --export hero.aseprite --to hero.gif --scale 4 --cycle walk
```

The extension of `--to` says what is written: `.png` is one frame (`--frame N`), or with `--sheet` a sprite sheet and
its description (`--border`, `--spacing`, `--trim`, `--json hash` or `--json array`), `--sequence` a file per step,
`--animated` an animated PNG; `.gif` is the animation; `.webp` is one frame, or with `--animated` the animation;
`.lsprite` converts an `.aseprite`, PNG or GIF to a document; `.gpl`, `.hex`, `.pal` or `.act` writes the palette.
The exit code is 0 for success, and a failure says why.
