// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

#include "app/clip_image.h"

#include <algorithm>
#include <map>

namespace fast {

namespace {

uint32_t packed(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

ls::Color unpacked(uint32_t c) {
    return ls::Color{ static_cast<uint8_t>(c & 0xFF), static_cast<uint8_t>((c >> 8) & 0xFF),
                      static_cast<uint8_t>((c >> 16) & 0xFF),
                      static_cast<uint8_t>((c >> 24) & 0xFF) };
}

uint32_t packedColour(ls::Color c) {
    return static_cast<uint32_t>(c.r) | (static_cast<uint32_t>(c.g) << 8) |
           (static_cast<uint32_t>(c.b) << 16) | (static_cast<uint32_t>(c.a) << 24);
}

// The slot nearest a colour, by squared distance over all four channels.
size_t nearestSlot(const std::vector<PaletteEntry>& palette, ls::Color c) {
    size_t best = 0;
    int64_t bestDistance = INT64_MAX;
    for (size_t i = 0; i < palette.size(); ++i) {
        const ls::Color& p = palette[i].color;
        const int64_t dr = int64_t(p.r) - c.r;
        const int64_t dg = int64_t(p.g) - c.g;
        const int64_t db = int64_t(p.b) - c.b;
        const int64_t da = int64_t(p.a) - c.a;
        const int64_t distance = dr * dr + dg * dg + db * db + da * da;
        if (distance < bestDistance) {
            bestDistance = distance;
            best = i;
        }
    }
    return best;
}

void put16(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(static_cast<uint8_t>(v & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
}

void put32(std::vector<uint8_t>& out, uint32_t v) {
    put16(out, v & 0xFFFF);
    put16(out, v >> 16);
}

} // namespace

bool layerImage(Document& doc, ls::LayerId layer, ls::RasterBuffer* out) {
    if (out == nullptr) {
        return false;
    }
    auto size = doc.engine().getCanvasSize(doc.id());
    if (size.fail() || size.value.x <= 0 || size.value.y <= 0) {
        return false;
    }
    const ls::CompileProfile profile =
        compileProfile(ls::CompileProfileType::Export, static_cast<uint32_t>(size.value.x),
                       static_cast<uint32_t>(size.value.y));
    auto compiled = doc.engine().compileLayer(layer, profile);
    if (compiled.fail() || compiled.value.raster.empty()) {
        return false;
    }
    *out = std::move(compiled.value.raster);
    return true;
}

ls::RasterBuffer imageOfMask(const ls::RasterBuffer& canvas, const ls::IntervalSet& mask) {
    ls::Rect2i box = ls::geom::bounds(mask);
    box.min.x = std::max(box.min.x, 0);
    box.min.y = std::max(box.min.y, 0);
    box.max.x = std::min(box.max.x, static_cast<int32_t>(canvas.width));
    box.max.y = std::min(box.max.y, static_cast<int32_t>(canvas.height));
    if (box.empty()) {
        return {};
    }
    ls::RasterBuffer image = ls::makeRaster(static_cast<uint32_t>(box.width()),
                                            static_cast<uint32_t>(box.height()));
    if (image.empty()) {
        return {};
    }
    for (const ls::Interval& run : mask.intervals) {
        if (run.y < box.min.y || run.y >= box.max.y) {
            continue;
        }
        const int32_t x0 = std::max(run.x0, box.min.x);
        const int32_t x1 = std::min(run.x1, box.max.x);
        if (x0 >= x1) {
            continue;
        }
        const uint8_t* from = canvas.row(static_cast<uint32_t>(run.y)) + size_t(x0) * 4;
        uint8_t* to = image.row(static_cast<uint32_t>(run.y - box.min.y)) +
                      size_t(x0 - box.min.x) * 4;
        std::copy(from, from + size_t(x1 - x0) * 4, to);
    }
    return image;
}

bool clipFromImage(const ls::RasterBuffer& image, ls::Vec2i at,
                   const std::vector<PaletteEntry>& palette, PixelClip* out,
                   ImageClipReport* report) {
    if (out == nullptr || image.empty()) {
        return false;
    }
    // The colours first, so the decision to reduce is made before any piece.
    std::map<uint32_t, size_t> colours;
    for (uint32_t y = 0; y < image.height; ++y) {
        const uint8_t* row = image.row(y);
        for (uint32_t x = 0; x < image.width; ++x) {
            if (row[x * 4 + 3] != 0) {
                colours.emplace(packed(row + x * 4), 0);
            }
        }
    }
    if (colours.empty()) {
        return false;
    }
    const bool reduce = colours.size() > kMaxClipColours;
    if (reduce && palette.empty()) {
        return false;
    }

    // Each colour's ink: its exact slot if the palette has one, the nearest
    // slot when reducing, otherwise the colour itself. Two colours reduced to
    // one slot share a piece.
    std::map<uint32_t, size_t> exact;     // colour -> palette index
    for (size_t i = 0; i < palette.size(); ++i) {
        exact.emplace(packedColour(palette[i].color), i);
    }
    PixelClip clip;
    std::map<int64_t, size_t> pieceOf;    // slot index, or -1 - colour, -> piece
    size_t slots = 0;
    for (auto& [colour, piece] : colours) {
        Ink ink;
        int64_t key = 0;
        const auto hit = exact.find(colour);
        if (hit != exact.end() || reduce) {
            const size_t slot = hit != exact.end() ? hit->second
                                                   : nearestSlot(palette, unpacked(colour));
            ink.role = palette[slot].role;
            ink.colour = palette[slot].color;
            key = static_cast<int64_t>(slot);
        } else {
            ink.colour = unpacked(colour);
            key = -1 - static_cast<int64_t>(colour);
        }
        const auto found = pieceOf.find(key);
        if (found != pieceOf.end()) {
            piece = found->second;
            continue;
        }
        piece = clip.pieces.size();
        pieceOf.emplace(key, piece);
        PixelClip::Piece made;
        made.ink = ink;
        clip.pieces.push_back(made);
        if (ink.usesSlot()) {
            ++slots;
        }
    }

    // Then the runs: a run of one colour is one interval of its piece.
    for (uint32_t y = 0; y < image.height; ++y) {
        const uint8_t* row = image.row(y);
        uint32_t x = 0;
        while (x < image.width) {
            if (row[x * 4 + 3] == 0) {
                ++x;
                continue;
            }
            const size_t piece = colours[packed(row + x * 4)];
            uint32_t end = x + 1;
            while (end < image.width && row[end * 4 + 3] != 0 &&
                   colours[packed(row + end * 4)] == piece) {
                ++end;
            }
            const int32_t py = at.y + static_cast<int32_t>(y);
            clip.pieces[piece].pixels.intervals.push_back(
                { py, at.x + static_cast<int32_t>(x), at.x + static_cast<int32_t>(end) });
            clip.mask.intervals.push_back(
                { py, at.x + static_cast<int32_t>(x), at.x + static_cast<int32_t>(end) });
            x = end;
        }
    }
    for (PixelClip::Piece& piece : clip.pieces) {
        piece.pixels = ls::geom::normalize(std::move(piece.pixels));
    }
    clip.mask = ls::geom::normalize(std::move(clip.mask));
    if (report != nullptr) {
        report->colours = clip.pieces.size();
        report->slots = slots;
        report->reduced = reduce;
    }
    *out = std::move(clip);
    return true;
}

ls::Vec2i pastePosition(uint32_t imageWidth, uint32_t imageHeight, uint32_t canvasWidth,
                        uint32_t canvasHeight) {
    const auto place = [](uint32_t image, uint32_t canvas) {
        return image >= canvas ? 0 : static_cast<int32_t>((canvas - image) / 2);
    };
    return { place(imageWidth, canvasWidth), place(imageHeight, canvasHeight) };
}

std::vector<uint8_t> encodeBmp(const ls::RasterBuffer& image) {
    std::vector<uint8_t> out;
    if (image.empty()) {
        return out;
    }
    const uint32_t headerSize = 124;                     // BITMAPV5HEADER
    const uint32_t offset = 14 + headerSize;
    const uint32_t pixels = image.width * image.height * 4;
    out.reserve(offset + pixels);
    // BITMAPFILEHEADER
    out.push_back('B');
    out.push_back('M');
    put32(out, offset + pixels);
    put32(out, 0);
    put32(out, offset);
    // BITMAPV5HEADER
    put32(out, headerSize);
    put32(out, image.width);
    put32(out, image.height);                            // positive: bottom-up
    put16(out, 1);                                       // planes
    put16(out, 32);                                      // bits per pixel
    put32(out, 3);                                       // BI_BITFIELDS
    put32(out, pixels);
    put32(out, 2835);                                    // 72 dpi, as metres
    put32(out, 2835);
    put32(out, 0);                                       // colours used
    put32(out, 0);                                       // important
    put32(out, 0x00FF0000);                              // red mask
    put32(out, 0x0000FF00);                              // green
    put32(out, 0x000000FF);                              // blue
    put32(out, 0xFF000000);                              // alpha
    put32(out, 0x73524742);                              // LCS_sRGB, 'sRGB'
    for (int i = 0; i < 9; ++i) {                        // endpoints, unused for sRGB
        put32(out, 0);
    }
    put32(out, 0);                                       // gamma red, green, blue
    put32(out, 0);
    put32(out, 0);
    put32(out, 4);                                       // LCS_GM_IMAGES
    put32(out, 0);                                       // profile data, size
    put32(out, 0);
    put32(out, 0);                                       // reserved
    for (uint32_t y = image.height; y-- > 0;) {
        const uint8_t* row = image.row(y);
        for (uint32_t x = 0; x < image.width; ++x) {
            out.push_back(row[x * 4 + 2]);
            out.push_back(row[x * 4 + 1]);
            out.push_back(row[x * 4 + 0]);
            out.push_back(row[x * 4 + 3]);
        }
    }
    return out;
}

} // namespace fast
