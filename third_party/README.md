# Third-party code

Vendored rather than fetched. These are files, not projects: pinning them in the
tree means the build needs no network, the exact bytes are reviewable in a diff,
and `fast_core` keeps building in seconds with no dependencies to resolve.

## stb_image_write.h

| | |
|---|---|
| Version | v1.16 |
| Commit | `2c980bb59875b0d32144a71867fbdebb2f77cd20` |
| SHA-256 | `cbd5f0ad7a9cf4468affb36354a1d2338034f2c12473cf1a8e32053cb6914a05` |
| Source | https://github.com/nothings/stb |
| Licence | Public domain (Unlicense), or MIT at your option |

Unmodified. To update: replace the file, record the new commit and hash here, and
check the export tests still pass — they compare exported bytes for stability, so
a change in stb's deflate will show up as a failure rather than silently altering
every file Fast writes.

### Why this and not a hand-written encoder

Measured on representative pixel art, at 512x512:

| Encoder | Bytes |
|---|---|
| stb, RGBA | 7,805 |
| hand-written RGBA, fixed-Huffman deflate | 19,613 |
| hand-written indexed, fixed-Huffman deflate | 9,926 |
| raw, uncompressed | 1,048,576 |

A hand-written encoder using *stored* deflate -- the only kind that is genuinely
simple -- produces files a hundred times too large. The tractable compressed kind,
fixed-Huffman, is 2.5x worse than stb, and only reaches parity by also adding
indexed output, which means writing two hard things to roughly tie.

Indexed PNG is still worth having eventually: it is smaller again and preserves
the palette, so a game can swap colours at run time, which is the point of a
palette-native engine. That is a separate, focused piece of work rather than a
reason to hand-write the common case now.

### Two things to know when using it

`stbi_write_png` opens the file itself with `fopen(const char*)`, which on Windows
reads the path in the active code page and mangles anything outside ASCII. Fast
uses `stbi_write_png_to_func` and writes the bytes through `app/file_io`, which
handles paths correctly. Do not call the file-writing entry points.

`stbi_write_png_compression_level` is a mutable global. Fast sets it explicitly on
every export rather than inheriting whatever it happens to hold, so the same
sprite exports to the same bytes.
