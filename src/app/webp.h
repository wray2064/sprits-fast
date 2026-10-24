// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors
#pragma once

// webp.h — lossless WebP, one picture or an animation, written and read.
//
// Written from the VP8L bitstream specification rather than pulled in: what
// pixel art needs from WebP is its lossless mode, and that is a palette
// transform, prefix codes and back-references -- a few hundred lines. The
// reader (webp_decode.cpp) is the whole of lossless WebP -- all four
// transforms, the colour cache, several sets of prefix codes -- so a file
// made by any encoder reads, not only ours. Lossy WebP (VP8) is not read, and
// says so. No library, so nothing to licence and nothing to carry.
//
// What the encoder does, and why it is enough:
//
//   * A picture of 256 colours or fewer is written through the colour-indexing
//     transform, its indices packed two, four or eight to a pixel when there
//     are few enough colours -- which is nearly every sprite.
//   * Back-references look at two places only: the pixel before and the pixel
//     above. Runs and repeated rows are where pixel art repeats itself, and
//     those two distances have the shortest codes in the format.
//   * Every value is exact, including the colour under a fully transparent
//     pixel. Lossless means lossless.
//
// An animation is the extended format: a canvas, a loop count, and one full
// frame per step, each shown for its hold and drawn without blending.

#include <livesprite/livesprite.h>

#include <cstdint>
#include <string>
#include <vector>

namespace fast {

// Whether the bytes start as a WebP file does (RIFF....WEBP). Says nothing of
// whether the rest is readable.
bool looksLikeWebp(const std::vector<uint8_t>& bytes);

// A lossless WebP's size, from its header alone. For an animation, the first
// frame's.
bool webpSize(const std::vector<uint8_t>& bytes, uint32_t* width, uint32_t* height,
              std::string* error);

// A lossless WebP as RGBA8; for an animation, its first frame. Untrusted input:
// sizes are capped as image_io's are, and a failure is a message.
bool decodeWebp(const std::vector<uint8_t>& bytes, ls::RasterBuffer* out, std::string* error);

// One picture, as a .webp file.
bool encodeWebp(const ls::RasterBuffer& image, std::vector<uint8_t>* out, std::string* error);

// An animation, as a .webp file: every frame the same size, each held for its
// entry in `holdsMs`; `loop` repeats it for ever, otherwise it plays once.
bool encodeAnimatedWebp(const std::vector<ls::RasterBuffer>& frames,
                        const std::vector<int>& holdsMs, bool loop,
                        std::vector<uint8_t>* out, std::string* error);

} // namespace fast
