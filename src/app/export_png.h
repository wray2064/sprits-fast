// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors
#pragma once

// export_png.h — getting the work out of the program.
//
// The engine has no image codecs and should not: it compiles operations into an
// RGBA buffer and stops there. Turning that buffer into a file somebody else can
// open is the application's job, and this is it.
//
// Two details that are not details:
//
// The compile uses `Export`, not `Preview`. They are different profiles for a
// reason -- Export refuses non-deterministic plugins, among other things -- and
// an editor that exports what it happened to have on screen is exporting the
// wrong thing.
//
// Scaling is by whole-number pixel duplication. A pixel-art sprite delivered at
// 4x must be exactly four identical pixels per side, never four pixels of
// interpolation, so the scaling happens here rather than being left to whatever
// opens the file.

#include "app/document.h"

#include <cstdint>
#include <string>

namespace fast {

struct ExportSettings {
    // Whole-number magnification. 1 writes the sprite at its own size.
    uint32_t scale = 1;

    // Beyond this an export is a mistake rather than a large image. It also
    // keeps `scale` from turning a legal canvas into an illegal raster.
    static constexpr uint32_t kMaxScale = 64;
};

// Whole-number magnification by duplicating pixels, never interpolating: at 4x
// a pixel is exactly four identical pixels per side. Empty when the result
// would be too large to allocate.
ls::RasterBuffer magnifyRaster(const ls::RasterBuffer& source, uint32_t scale);

// Compiles `sprite` at export quality and writes a PNG.
//
// Written atomically, like every other file Fast produces: a failure partway
// through leaves whatever was there before rather than a truncated image.
bool exportSpriteToPng(Document& doc, ls::SpriteId sprite, const std::string& path,
                       const ExportSettings& settings, std::string* error);

// The same, but handing back the bytes instead of writing them. Useful for a
// thumbnail kept inside the document, and it is what makes the writing testable
// without a filesystem.
bool encodeRasterToPng(const ls::RasterBuffer& raster, const ExportSettings& settings,
                       std::vector<uint8_t>* out, std::string* error);

} // namespace fast
