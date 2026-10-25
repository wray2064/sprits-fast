# Troubleshooting and limits

## The program closed and I lost work

Start it again. If the last session ended without saving, it asks **Recover your work?** about the work you lost last, with a
picture of it, its name (or *Untitled*), when it was last written, its size and where it was being saved. **Recover** opens
it as an unsaved document: use Save to say where it goes. **Later** leaves it for next time; **Discard** asks twice, because
it cannot be undone. Copies are written while you have unsaved changes (every couple of minutes by default) and are never
written over your file. Copies left by earlier crashes are not offered again: once you have dealt with the latest, they are
moved to an `older` folder beside the recovery copies (the ten newest are kept). See
[Opening, saving and exporting](topic:files).

## Where are my settings?

In a folder called `SpritsFast` in your user profile: preferences, key bindings, your themes (`themes`), saved layouts
(`layouts`), the recent-files list and the recovery copies. The Microsoft Store version keeps the same folder inside the
app's own storage and removes it when you uninstall. Deleting the folder gives you a program as if newly installed.
**Window > Reset layout** puts back just the panel arrangement.

## Everything is too small, too big, or cut off

**Edit > Preferences... > Appearance > Interface size** scales the text and spacing between 75% and 150%, and **density**
changes the air. Windows display scaling is followed on its own. If a panel is too narrow for what it holds,
drag the line beside it wider, or use the **Compact** layout (**Window > Layouts**).

## A key does not do what it says

- Another command may share the key: **Edit > Preferences... > Keys** warns where two do.
- A key typed into a text box goes to the box; click on the canvas first.
- `Space` is the hand, not a command of its own: hold it and drag.

## It will not let me draw

- The layer may be **locked** (the Layers panel), hidden, or a **reference layer**.
- While an animation **plays**, drawing is refused so a stroke does not land on a frame you are not looking at. Press `Enter` to stop.
- A selection keeps the pencil, eraser and fill **inside** it; **Deselect** (`Ctrl+D`) lifts that.

## Limits

| | |
|---|---|
| Canvas | 16,384 pixels on a side, 16 million pixels in all |
| Parts in a puppet document | 256 |
| A reference | 8 MB each, 16 MB in a document |
| A picture opened as artwork | a photograph with thousands of colours is refused, and suggested as a reference instead |
| A GIF | 256 colours a frame, no partial transparency |

The first rows are comfortable defaults, not walls. **Edit > Preferences... > Experimental > Lift the size limits**
removes them, up to what your computer can hold: a larger canvas, more frames, cycles, puppet parts and references, a
bigger brush. It applies to what you make or open afterwards, and a very large canvas can be slow and use a great deal
of memory. A file made with the limits lifted still opens and saves whole when they are on.

## Aseprite files

Everything carries over that the format describes. What does not is said, not dropped: a tileset kept in another file,
and the few blend modes Fast does not have, which come in as the nearest one it does.

## The language is wrong

**Edit > Preferences... > General > Language**.

## Reporting a problem

**Help > About** has the version and a link to report one. Say what you did, what you expected and what happened; attach
the `.lsprite` if it is about a sprite. Nothing is ever sent automatically: the program never connects to the internet.
