# Icons

The tool and visibility icons are **Lucide** (https://lucide.dev), release
**1.59.0**, by Lucide Icons and Contributors. The set is under the ISC licence;
the few icons Lucide inherited from Feather (here `circle`, `move`, `square`,
`type` and `zoom-in`) are under Feather's MIT licence. Both texts are in
`LICENSE`, which must stay beside the icons.

24 files, 13063 bytes with the licence. Each was taken unmodified from
`https://raw.githubusercontent.com/lucide-icons/lucide/1.59.0/icons/<name>.svg`
on 2026-10-18. `LICENSE` came from the repository's `main` at the same time
(it is the file the 1.59.0 icons are published under; it carries the
Feather-derived list).

| File | Bytes | SHA-256 (first 16) |
|---|---|---|
| `blend.svg` | 276 | `cff13925b80ff33a` |
| `circle-dashed.svg` | 590 | `9b964be491a821be` |
| `circle.svg` | 244 | `3a991bd47beaf987` |
| `eraser.svg` | 383 | `838d4355dd493405` |
| `eye-off.svg` | 504 | `774bcd975c5de781` |
| `eye.svg` | 360 | `5bf90197dd7629ca` |
| `hand.svg` | 483 | `fe3771fa2f0a9a3c` |
| `lasso-select.svg` | 597 | `227380bdb112334b` |
| `lasso.svg` | 333 | `6b063dbff366eb47` |
| `move.svg` | 371 | `915074a1292f72d5` |
| `paint-bucket.svg` | 622 | `456e35597fb306f3` |
| `pen-tool.svg` | 590 | `dbdbc461cb669bd8` |
| `pencil.svg` | 377 | `7e1ca7a6f5c1eb94` |
| `pentagon.svg` | 362 | `e27daa75dc936fbb` |
| `pipette.svg` | 461 | `dad18ee113120005` |
| `slash.svg` | 234 | `2e8b636164f057c3` |
| `slice.svg` | 314 | `7fe1258bdf8a62f0` |
| `spline.svg` | 314 | `995d3e71e8c4ebd6` |
| `spray-can.svg` | 518 | `19c310ec728c651f` |
| `square-dashed.svg` | 532 | `bb7958e30d2658e9` |
| `square.svg` | 261 | `bd979354f0ab184b` |
| `type.svg` | 310 | `82c514732330f9c3` |
| `wand.svg` | 443 | `5134f76053783530` |
| `zoom-in.svg` | 376 | `91206ae20013e2c8` |
| `LICENSE` | 3208 | `b495047bd93a9b06` |

## How they are used

They are compiled into the program (`cmake/embed_file.cmake`, listed as
`FAST_ICONS` in `CMakeLists.txt`) and parsed at start by `app/vector_icon.*`,
which reads the small part of SVG Lucide uses: `path` (with arcs and curves),
`line`, `polyline`, `polygon`, `rect`, `circle` and `ellipse`, on a 24 by 24 grid
with a 2 unit round stroke. Each becomes strokes -- paths, not pixels -- drawn
at whatever size and colour is asked for through one call,
`theme::drawIcon(draw, icon, at, size, colour)`. No icon is ever a bitmap.

To check a file: `Get-FileHash assets\icons\lucide\pencil.svg` against the
table.

To add an icon: download it from the same release into this folder, add a row
above, add it to `FAST_ICONS`, to the `Icon` enum in `ui/theme.h` and to
`kIconSources` in `ui/theme.cpp`. `tests/vector_icon_tests.cpp` parses every shipped file and
fails if one leaves the grid or does not parse.
