# Panels, themes and preferences

## Panels

Every panel docks, tabs, floats and resizes. Drag one by its tab to the side of another to dock beside it, onto it to
share its place as a tab, or out over the canvas to float it; drag the line between two to size them. The **Window**
menu opens and closes each one. The canvas keeps the middle and cannot be covered or moved.

The arrangement is kept between sessions, and the **Window > Layouts** menu has three arrangements built in:

- **Drawing**: tool options and palette on the left; layers, elements, properties and transform on the right.
- **Animation**: the timeline pinned under the canvas, and the other panels grouped as tabs.
- **Compact**: for a small screen; one group of tabs on the right.

**Save current layout...** keeps the arrangement under a name of your own, and choosing it brings it back.
**Reset layout** puts the default back.

## Themes

**Edit > Preferences... > Appearance** chooses how the program looks. Nine themes ship:

| Theme | Character |
|---|---|
| Slate | cool grey-blue with amber, the default |
| Graphite | neutral grey with a blue accent |
| Midnight | deep blue-black that lets the canvas glow |
| OLED | true black, so a pixel that is not lit is off |
| High contrast dark | black, white and one yellow; every pair above 7:1 |
| Paper | the same arrangement turned over for a bright room |
| Warm paper | a soft sepia, gentle for long sessions |
| Famicom | cream plastic and deep red |
| High contrast light | white, black and one blue |

- **Accent**: nine presets and a picker. The accent is moved only as far as it must be to be seen and to carry text.
- **Interface size** (75% to 150%) scales text and spacing, never the canvas; **density** (Compact, Comfortable,
  Spacious) changes how much air there is.
- **Readability** says which colour pairs are hard to read and offers **Fix them**.
- **Colours** lists every part of the interface with its own picker, hex value and a reset. Edits show at once; **Save as
  new theme** keeps them as a theme of your own (a `.lstheme` text file you can copy, edit and send). **Import...** and
  **Export...** exchange theme files.

## Preferences

**General**:

- *A new document*: the size, background and palette a new document starts with.
- *Working*: how often autosave writes, and the undo depth (*undo steps*).
- *The canvas*: the pixel grid's colour and opacity, and the chequer's colours and square size.
- *Language*: English or Spanish.

**Keys**: every shortcut is a named command. Choose one and press the new keys (up to four per command); you are warned
where two commands share a key; reset one or all. The menus show whatever keys you have. **Help > Keyboard shortcuts**
lists them all.

Preferences and keys are plain text files in the settings folder (`preferences.txt`, `keys.txt`) that the program reads
defensively: a hand-edited file can make Fast odd, never broken.

## Command search

`Ctrl+Shift+P` (or **Help > Search commands...**) finds any command by a few letters and runs it with Enter.

## Languages

The menus, headings, tool and command names go through a plain-text catalogue per language; English and Spanish ship
with the program. The catalogue format is documented in the project's `docs/translating.md`.
