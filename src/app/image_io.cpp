// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

#include "app/image_io.h"

#include "app/export_png.h"
#include "app/file_io.h"

#include <algorithm>
#include <cstring>

// See third_party/README.md for why these are set. STBI_NO_STDIO in particular
// is load-bearing: it removes every entry point that takes a path, so the
// Windows code-page trap that the writer avoids by convention cannot be walked
// into here at all.
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_BMP
#define STBI_ONLY_GIF
#define STBI_MAX_DIMENSIONS 16384
#include "stb_image.h"

namespace fast {
namespace {

// stb takes an int length, so a file larger than INT_MAX cannot be handed to
// it at all. Well under any limit here, but the cast has to be safe.
constexpr size_t kMaxEncodedBytes = 64u * 1024u * 1024u;

bool sizeAllowed(int width, int height, std::string* error) {
    if (width <= 0 || height <= 0) {
        if (error) { *error = "the image has no size"; }
        return false;
    }
    const uint64_t w = static_cast<uint64_t>(width);
    const uint64_t h = static_cast<uint64_t>(height);
    if (w > kMaxReferenceDimension || h > kMaxReferenceDimension ||
        w * h > kMaxReferencePixels) {
        if (error) {
            *error = "the image is " + std::to_string(width) + " x " +
                     std::to_string(height) + ", which is larger than this "
                     "build will open";
        }
        return false;
    }
    return true;
}

bool usable(const std::vector<uint8_t>& bytes, std::string* error) {
    if (bytes.empty()) {
        if (error) { *error = "the file is empty"; }
        return false;
    }
    if (bytes.size() > kMaxEncodedBytes) {
        if (error) { *error = "the file is too large to be an image"; }
        return false;
    }
    return true;
}

} // namespace

bool imageInfo(const std::vector<uint8_t>& bytes, ImageInfo* out, std::string* error) {
    if (out == nullptr || !usable(bytes, error)) {
        return false;
    }
    int width = 0;
    int height = 0;
    int channels = 0;
    if (stbi_info_from_memory(bytes.data(), static_cast<int>(bytes.size()),
                              &width, &height, &channels) == 0) {
        if (error) {
            const char* why = stbi_failure_reason();
            *error = std::string("not an image this build reads") +
                     (why != nullptr ? std::string(": ") + why : std::string());
        }
        return false;
    }
    if (!sizeAllowed(width, height, error)) {
        return false;
    }
    out->width = static_cast<uint32_t>(width);
    out->height = static_cast<uint32_t>(height);
    out->channels = channels;
    return true;
}

bool decodeImage(const std::vector<uint8_t>& bytes, ls::RasterBuffer* out,
                 std::string* error) {
    if (out == nullptr) {
        return false;
    }
    // The header first, so a file that claims an enormous size is refused
    // before a single row is decoded.
    ImageInfo info;
    if (!imageInfo(bytes, &info, error)) {
        return false;
    }

    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_uc* pixels = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()),
                                            &width, &height, &channels, 4);
    if (pixels == nullptr) {
        if (error) {
            const char* why = stbi_failure_reason();
            *error = std::string("the image could not be read") +
                     (why != nullptr ? std::string(": ") + why : std::string());
        }
        return false;
    }
    // The header is a claim; what was decoded is the truth. Check again.
    if (!sizeAllowed(width, height, error)) {
        stbi_image_free(pixels);
        return false;
    }

    ls::RasterBuffer raster = ls::makeRaster(static_cast<uint32_t>(width),
                                             static_cast<uint32_t>(height));
    if (raster.pixels.empty()) {
        stbi_image_free(pixels);
        if (error) { *error = "there was not enough memory for the image"; }
        return false;
    }
    const size_t rowBytes = static_cast<size_t>(width) * 4u;
    for (uint32_t y = 0; y < raster.height; ++y) {
        std::memcpy(raster.row(y), pixels + static_cast<size_t>(y) * rowBytes, rowBytes);
    }
    stbi_image_free(pixels);
    *out = std::move(raster);
    return true;
}

bool encodeImageAsPng(const ls::RasterBuffer& raster, std::vector<uint8_t>* out,
                      std::string* error) {
    if (out == nullptr) {
        return false;
    }
    ExportSettings settings;
    settings.scale = 1;
    return encodeRasterToPng(raster, settings, out, error);
}

ls::RasterBuffer downscaleNearest(const ls::RasterBuffer& raster, uint32_t longestSide) {
    if (raster.empty() || longestSide == 0 ||
        (raster.width <= longestSide && raster.height <= longestSide)) {
        return raster;
    }
    // One whole-number step per output pixel, chosen so the longer side lands
    // on or just inside the limit. Nearest, because averaging a pixel-art
    // reference into a thumbnail turns it into mush -- and a reference is
    // usually pixel art.
    const double scale = static_cast<double>(longestSide) /
                         static_cast<double>(std::max(raster.width, raster.height));
    const uint32_t width  = std::max(1u, static_cast<uint32_t>(raster.width * scale));
    const uint32_t height = std::max(1u, static_cast<uint32_t>(raster.height * scale));

    ls::RasterBuffer out = ls::makeRaster(width, height);
    if (out.pixels.empty()) {
        return raster;
    }
    for (uint32_t y = 0; y < height; ++y) {
        const uint32_t sourceY = std::min(raster.height - 1,
            static_cast<uint32_t>(static_cast<uint64_t>(y) * raster.height / height));
        for (uint32_t x = 0; x < width; ++x) {
            const uint32_t sourceX = std::min(raster.width - 1,
                static_cast<uint32_t>(static_cast<uint64_t>(x) * raster.width / width));
            std::memcpy(out.row(y) + static_cast<size_t>(x) * 4u,
                        raster.row(sourceY) + static_cast<size_t>(sourceX) * 4u, 4u);
        }
    }
    return out;
}

bool looksLikeImageName(const std::string& utf8Path) {
    static const char* kNames[] = { ".png", ".jpg", ".jpeg", ".bmp", ".gif" };
    for (const char* name : kNames) {
        if (hasExtension(utf8Path, name)) {
            return true;
        }
    }
    return false;
}

} // namespace fast
