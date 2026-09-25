// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors
#pragma once

// image_io.h — reading an image somebody else made.
//
// Everything else Fast reads it also wrote. A reference image did not come from
// here: it is a photograph, a screenshot, a sprite sheet off the internet, a
// file with the wrong extension, or a file crafted to break a decoder. So this
// is the one place that hands untrusted bytes to a parser, and the bounds are
// part of the interface rather than a detail of it:
//
//   * the size is read first, from the header alone, and refused before any
//     pixels are decoded -- so a file claiming 60000 x 60000 costs nothing;
//   * a decode that would need more than kMaxReferencePixels is refused rather
//     than attempted, because "out of memory" arrives as a crash and "that
//     image is too large" arrives as a sentence;
//   * failure is always a message, never a half-filled buffer.
//
// What comes back is an ls::RasterBuffer, RGBA8, which is what the rest of Fast
// already draws.

#include <livesprite/livesprite.h>

#include <cstdint>
#include <string>
#include <vector>

namespace fast {

// Eight megapixels: a 4K photograph with room to spare, and about 32 MB once
// it is RGBA. Larger than any pixel-art reference needs and small enough that
// refusing is honest rather than stingy.
constexpr uint32_t kMaxReferenceDimension = 16384;
constexpr uint64_t kMaxReferencePixels    = 8ull * 1024ull * 1024ull;

// What a file says it is before anything is decoded.
struct ImageInfo {
    uint32_t width = 0;
    uint32_t height = 0;
    int      channels = 0;      // as stored, before conversion to RGBA
};

// Reads the header only. False, with a reason, when the bytes are not an image
// this build reads or the size is refused.
bool imageInfo(const std::vector<uint8_t>& bytes, ImageInfo* out, std::string* error);

// Decodes to RGBA8. PNG, JPEG, BMP and GIF; anything else is refused by name
// rather than guessed at.
bool decodeImage(const std::vector<uint8_t>& bytes, ls::RasterBuffer* out,
                 std::string* error);

// Every frame of an image: an animated GIF's frames with their holds in
// milliseconds, or any other image as one frame. The frames of a GIF are
// counted from its structure before any is decoded, and refused when all of
// them together would pass kMaxAnimationPixels -- a small file can describe a
// great many large frames, and the decoder allocates every one.
constexpr uint64_t kMaxAnimationPixels = 64ull * 1024ull * 1024ull;
bool decodeFrames(const std::vector<uint8_t>& bytes, std::vector<ls::RasterBuffer>* frames,
                  std::vector<int>* holdsMs, std::string* error);

// How many frames a GIF holds, by walking its blocks without decoding any.
// Zero for bytes that are not a well-formed GIF.
size_t countGifFrames(const std::vector<uint8_t>& bytes);

// Encodes RGBA8 as PNG. References are stored in the document package, and a
// JPEG re-encoded as PNG is both smaller to draw from and one format for the
// package to carry.
bool encodeImageAsPng(const ls::RasterBuffer& raster, std::vector<uint8_t>* out,
                      std::string* error);

// A copy no larger than `longestSide` on either axis, by dropping whole pixels
// -- nearest, never averaged. Used for thumbnails, where a smooth shrink turns
// pixel art into mush, and for taking a very large reference down to something
// a canvas can hold. Returns the original when it already fits.
ls::RasterBuffer downscaleNearest(const ls::RasterBuffer& raster, uint32_t longestSide);

// Whether a name looks like an image this build reads. For filtering a folder
// listing, not for deciding how to decode one -- that is done by content.
bool looksLikeImageName(const std::string& utf8Path);

} // namespace fast
