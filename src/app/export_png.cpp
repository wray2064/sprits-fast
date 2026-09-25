// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

#include "app/export_png.h"
#include "app/file_io.h"
#include "app/zlib.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION

// stb writes to C stdio by default, and those entry points open the file with
// fopen(const char*) -- which on Windows reads the path in the active code page
// and silently mangles anything outside ASCII. Fast has already been through
// that bug once. Compiling the file-writing half out entirely means it cannot be
// reached by accident: only the to_func and to_mem forms remain.
#define STBI_WRITE_NO_STDIO

#include "stb_image_write.h"

#include <vector>

namespace fast {
namespace {

// stb hands its output over in pieces; this collects them.
void collect(void* context, void* data, int size) {
    auto* out = static_cast<std::vector<uint8_t>*>(context);
    const auto* bytes = static_cast<const uint8_t*>(data);
    out->insert(out->end(), bytes, bytes + size);
}

// Whole-number magnification, by duplication. Never interpolation: a pixel-art
// sprite at 4x is four identical pixels per side, and anything else defeats the
// point of the format it is being written in.
ls::RasterBuffer magnify(const ls::RasterBuffer& source, uint32_t scale) {
    if (scale <= 1) {
        return source;
    }
    ls::RasterBuffer out = ls::makeRaster(source.width * scale, source.height * scale);
    if (out.empty()) {
        return out;
    }
    for (uint32_t y = 0; y < source.height; ++y) {
        for (uint32_t x = 0; x < source.width; ++x) {
            const ls::Color pixel = ls::readPixel(source, static_cast<int32_t>(x),
                                                  static_cast<int32_t>(y));
            for (uint32_t dy = 0; dy < scale; ++dy) {
                for (uint32_t dx = 0; dx < scale; ++dx) {
                    ls::writePixel(out,
                                   static_cast<int32_t>(x * scale + dx),
                                   static_cast<int32_t>(y * scale + dy), pixel);
                }
            }
        }
    }
    return out;
}

} // namespace

ls::RasterBuffer magnifyRaster(const ls::RasterBuffer& source, uint32_t scale) {
    return magnify(source, scale);
}

bool encodeRasterToPng(const ls::RasterBuffer& raster, const ExportSettings& settings,
                       std::vector<uint8_t>* out, std::string* error) {
    if (out == nullptr) {
        return false;
    }
    out->clear();

    if (raster.empty() || raster.width == 0 || raster.height == 0) {
        if (error) { *error = "there is nothing to export"; }
        return false;
    }
    if (settings.scale == 0 || settings.scale > ExportSettings::kMaxScale) {
        if (error) { *error = "that scale is out of range"; }
        return false;
    }

    const ls::RasterBuffer scaled = magnify(raster, settings.scale);
    if (scaled.empty()) {
        if (error) { *error = "the image is too large at that scale"; }
        return false;
    }

    // Set every time rather than trusting whatever this global holds. It is
    // process-wide state in a library, and the same sprite has to export to the
    // same bytes.
    stbi_write_png_compression_level = 8;
    stbi_flip_vertically_on_write(0);

    const int written = stbi_write_png_to_func(
        collect, out,
        static_cast<int>(scaled.width), static_cast<int>(scaled.height),
        4,                                  // RGBA, which is how the engine compiles
        scaled.pixels.data(),
        static_cast<int>(scaled.stride));

    if (written == 0 || out->empty()) {
        out->clear();
        if (error) { *error = "the image could not be encoded"; }
        return false;
    }
    return true;
}

bool exportSpriteToPng(Document& doc, ls::SpriteId sprite, const std::string& path,
                       const ExportSettings& settings, std::string* error) {
    auto size = doc.engine().getCanvasSize(doc.id());
    if (size.fail() || size.value.x <= 0 || size.value.y <= 0) {
        if (error) { *error = "this document has no canvas"; }
        return false;
    }

    // Export quality, not what is on screen. Preview and Export are different
    // profiles on purpose.
    const ls::CompileProfile profile = compileProfile(
        ls::CompileProfileType::Export, static_cast<uint32_t>(size.value.x), static_cast<uint32_t>(size.value.y));

    auto compiled = doc.engine().compileSprite(sprite, profile);
    if (compiled.fail()) {
        if (error) {
            *error = "the sprite could not be compiled: " +
                     std::string(ls::lsErrorString(compiled.error));
        }
        return false;
    }

    std::vector<uint8_t> png;
    if (!encodeRasterToPng(compiled.value.raster, settings, &png, error)) {
        return false;
    }
    return writeFileAtomic(withExtension(path, ".png"), png, error);
}

bool zlibDeflate(const std::vector<uint8_t>& data, std::vector<uint8_t>* out) {
    if (out == nullptr || data.size() > 0x7FFFFFFF) {
        return false;
    }
    int length = 0;
    unsigned char* packed = stbi_zlib_compress(const_cast<unsigned char*>(data.data()),
                                               static_cast<int>(data.size()), &length, 8);
    if (packed == nullptr || length <= 0) {
        STBIW_FREE(packed);
        return false;
    }
    out->assign(packed, packed + length);
    STBIW_FREE(packed);
    return true;
}

} // namespace fast
