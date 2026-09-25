// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors
#pragma once

// pixel_font.h — a small face for writing in pixels.
//
// Text in a pixel-art sprite is pixels: a label on a sign, a score, a name. A
// system font rasterised at eight pixels is a smudge, so Fast carries a face
// drawn for the grid -- five by seven, proportional, every printable ASCII
// character -- drawn for this program, so it carries nobody else's licence.

#include <livesprite/livesprite.h>

#include <string>
#include <vector>

namespace fast {

// The pixels `text` covers with its top-left at `at`, each font pixel a
// `scale`-square block. Newlines start a new line. A character the face lacks
// is drawn as a question mark rather than silently dropped. `width` and
// `height`, when given, receive the laid-out size.
std::vector<ls::Vec2i> layOutText(const std::string& text, ls::Vec2i at, int scale,
                                  int* width = nullptr, int* height = nullptr);

// Rows from one line's top to the next's.
int pixelFontLineHeight();

} // namespace fast
