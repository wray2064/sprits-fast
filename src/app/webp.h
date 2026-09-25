// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors
#pragma once

// webp.h — lossless WebP, one picture or an animation.
//
// Written from the VP8L bitstream specification rather than pulled in: what
// pixel art needs from WebP is its lossless mode, and that is a palette
// transform, prefix codes and back-references -- a few hundred lines, with no
// decoder to carry, since nothing here reads WebP.
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

// One picture, as a .webp file.
bool encodeWebp(const ls::RasterBuffer& image, std::vector<uint8_t>* out, std::string* error);

// An animation, as a .webp file: every frame the same size, each held for its
// entry in `holdsMs`; `loop` repeats it for ever, otherwise it plays once.
bool encodeAnimatedWebp(const std::vector<ls::RasterBuffer>& frames,
                        const std::vector<int>& holdsMs, bool loop,
                        std::vector<uint8_t>* out, std::string* error);

} // namespace fast
