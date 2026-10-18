# Fonts

The interface font is **Inter** by the Inter Project Authors, under the
SIL Open Font License 1.1 (`Inter-OFL.txt`, which must stay beside the fonts).

| File | Weight | Bytes | SHA-256 |
|---|---|---|---|
| `Inter-Regular.ttf` | 400 | 411640 | `40d692fce188e447...` (see below) |
| `Inter-SemiBold.ttf` | 600 | 419744 | `78a843fade9d4612...` |

Source: `https://github.com/rsms/inter/releases/download/v4.1/Inter-4.1.zip`
(33707794 bytes, SHA-256
`9883fdd4a49d4fb66bd8177ba6625ef9a64aa45899767dde3d36aa425756b11e`), the files
`extras/ttf/Inter-Regular.ttf`, `extras/ttf/Inter-SemiBold.ttf` and
`LICENSE.txt`, taken unmodified. Downloaded 2026-10-18.

Static weights, not the variable font, because the text renderer (stb_truetype)
does not read variable axes. They are compiled into the program
(`cmake/embed_file.cmake`), so it looks the same on every machine and cannot
lose its font. If a face cannot be used the program falls back to the
platform's own (`theme::loadFonts`).

To check a file: `Get-FileHash assets\fonts\Inter-Regular.ttf` against the table
(the first 16 hex digits are shown).
