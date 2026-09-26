// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

#include "app/import_image.h"

#include "app/animation.h"
#include "app/file_io.h"
#include "app/image_io.h"
#include "app/palette.h"

#include <cstring>
#include <unordered_map>

namespace fast {

namespace {

uint32_t pack(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) << 24 | static_cast<uint32_t>(p[1]) << 16 |
           static_cast<uint32_t>(p[2]) << 8 | static_cast<uint32_t>(p[3]);
}

ls::Color unpack(uint32_t v) {
    return ls::Color{ static_cast<uint8_t>(v >> 24), static_cast<uint8_t>(v >> 16),
                      static_cast<uint8_t>(v >> 8), static_cast<uint8_t>(v) };
}

// Every colour of every frame, in the order it is first met reading the
// frames in turn, top to bottom, left to right -- the order a person would
// list them looking at the picture.
bool collectColours(const std::vector<ls::RasterBuffer>& frames, std::vector<uint32_t>* order,
                    std::unordered_map<uint32_t, size_t>* index) {
    for (const ls::RasterBuffer& frame : frames) {
        for (uint32_t y = 0; y < frame.height; ++y) {
            const uint8_t* row = frame.row(y);
            for (uint32_t x = 0; x < frame.width; ++x) {
                const uint8_t* p = row + static_cast<size_t>(x) * 4u;
                if (p[3] == 0) {
                    continue;
                }
                const uint32_t key = pack(p);
                if (index->find(key) == index->end()) {
                    if (order->size() >= kMaxImportColours) {
                        return false;
                    }
                    index->emplace(key, order->size());
                    order->push_back(key);
                }
            }
        }
    }
    return true;
}

} // namespace

bool documentFromFrames(Document& doc, const std::string& name,
                        const std::vector<ls::RasterBuffer>& frames,
                        const std::vector<int>& holdsMs,
                        ImportReport* report, std::string* error) {
    const auto fail = [&](const std::string& why) {
        if (error) { *error = why; }
        return false;
    };
    if (frames.empty() || frames.front().width == 0 || frames.front().height == 0) {
        return fail("there is no picture in it");
    }
    const uint32_t width = frames.front().width;
    const uint32_t height = frames.front().height;
    for (const ls::RasterBuffer& frame : frames) {
        if (frame.width != width || frame.height != height) {
            return fail("its frames are not all the same size");
        }
    }
    if (width > kMaxCanvasDimension || height > kMaxCanvasDimension ||
        static_cast<uint64_t>(width) * height > kMaxCanvasPixels) {
        return fail("at " + std::to_string(width) + " x " + std::to_string(height) +
                    " it is larger than a canvas Fast works on");
    }
    if (frames.size() > kMaxFrames) {
        return fail("it has " + std::to_string(frames.size()) + " frames; Fast holds " +
                    std::to_string(kMaxFrames));
    }

    std::vector<uint32_t> colours;
    std::unordered_map<uint32_t, size_t> index;
    if (!collectColours(frames, &colours, &index)) {
        return fail("it has more than " + std::to_string(kMaxImportColours) +
                    " colours, which makes it a photograph rather than a sprite -- "
                    "import it as a reference to draw from instead");
    }
    const bool throughPalette = !colours.empty() && colours.size() <= kMaxImportSlots;

    if (!doc.create(name, width, height)) {
        return fail("the engine would not make a canvas that size");
    }
    ls::LSContext& engine = doc.engine();
    auto info = engine.getDocumentInfo(doc.id());
    if (info.fail() || info.value.sprites.empty()) {
        return fail("the document could not be set up");
    }
    const ls::SpriteId first = info.value.sprites.front();

    // The picture's own colours are its palette, when they fit in one.
    if (throughPalette) {
        ls::PaletteDesc desc;
        desc.name = name.empty() ? std::string("palette") : name;
        for (size_t i = 0; i < colours.size(); ++i) {
            ls::PaletteColorEntry entry;
            entry.role = static_cast<ls::ColorRole>(i);
            entry.color = unpack(colours[i]);
            desc.entries.push_back(entry);
        }
        auto palette = engine.createPalette(doc.id(), desc);
        if (palette.fail() || engine.bindDocumentPalette(doc.id(), palette.value).fail()) {
            return fail("the palette could not be made");
        }
    } else if (!ensurePalette(doc, first)) {
        return fail("the palette could not be made");
    }

    std::vector<ls::SpriteId> sprites { first };
    for (size_t i = 1; i < frames.size(); ++i) {
        auto made = engine.createSprite(doc.id());
        if (made.fail()) {
            return fail("frame " + std::to_string(i + 1) + " could not be made");
        }
        sprites.push_back(made.value);
    }

    // Each frame: one layer, one ink per colour it uses, each a region of
    // exactly that colour's pixels -- read as runs, so a flat area is a few
    // intervals rather than a pixel apiece.
    std::vector<ls::IntervalSet> byColour(colours.size());
    for (size_t f = 0; f < frames.size(); ++f) {
        const ls::RasterBuffer& frame = frames[f];
        for (ls::IntervalSet& set : byColour) {
            set.intervals.clear();
        }
        for (uint32_t y = 0; y < height; ++y) {
            const uint8_t* row = frame.row(y);
            uint32_t x = 0;
            while (x < width) {
                const uint8_t* p = row + static_cast<size_t>(x) * 4u;
                if (p[3] == 0) {
                    ++x;
                    continue;
                }
                const uint32_t key = pack(p);
                uint32_t end = x + 1;
                while (end < width && pack(row + static_cast<size_t>(end) * 4u) == key) {
                    ++end;
                }
                byColour[index[key]].intervals.push_back(
                    { static_cast<int32_t>(y), static_cast<int32_t>(x), static_cast<int32_t>(end) });
                x = end;
            }
        }

        auto layer = engine.createLayer(sprites[f], { "Layer 1" });
        if (layer.fail()) {
            return fail("a layer could not be made");
        }
        bool any = false;
        for (size_t c = 0; c < colours.size(); ++c) {
            if (byColour[c].empty()) {
                continue;
            }
            const ls::RegionId region = createFreehandRegion(doc, byColour[c]);
            if (!region.valid()) {
                return fail("the pixels could not be read in");
            }
            ls::FillSolidOp fill;
            fill.targetRegion = region;
            fill.fallbackColor = unpack(colours[c]);
            fill.paletteRole = throughPalette ? static_cast<ls::ColorRole>(c)
                                              : ls::kColorRoleNone;
            if (engine.addOperation(layer.value, fill).fail()) {
                return fail("the pixels could not be read in");
            }
            any = true;
        }
        if (!any) {
            // An empty frame still gets something to draw into, as every layer
            // Fast makes does.
            const ls::RegionId region = createFreehandRegion(doc);
            ls::FillSolidOp fill;
            if (region.valid()) {
                fill.targetRegion = region;
                fill.fallbackColor = colours.empty() ? ls::Color{ 0, 0, 0, 255 }
                                                     : unpack(colours.front());
                engine.addOperation(layer.value, fill);
            }
        }
    }

    for (size_t f = 0; f < frames.size(); ++f) {
        const int hold = f < holdsMs.size() ? holdsMs[f] : 0;
        if (hold > 0) {
            setFrameDuration(doc, static_cast<int>(f), hold);
        }
    }

    // Reading a file in is not editing it: undo starts here, and nothing is
    // waiting to be saved until something changes.
    doc.clearHistory();
    doc.markUnmodified();

    if (report != nullptr) {
        report->frames = frames.size();
        report->colours = colours.size();
        report->throughPalette = throughPalette;
    }
    return true;
}

bool openImageAsDocument(Document& doc, const std::string& path,
                         ImportReport* report, std::string* error) {
    std::vector<uint8_t> bytes;
    if (!readFile(path, bytes, error)) {
        return false;
    }
    std::vector<ls::RasterBuffer> frames;
    std::vector<int> holds;
    if (!decodeFrames(bytes, &frames, &holds, error)) {
        return false;
    }
    return documentFromFrames(doc, fileStem(path), frames, holds, report, error);
}

bool sliceSheet(const ls::RasterBuffer& sheet, uint32_t cellWidth, uint32_t cellHeight,
                std::vector<ls::RasterBuffer>* cells, std::string* error) {
    if (cells == nullptr) {
        return false;
    }
    cells->clear();
    if (cellWidth == 0 || cellHeight == 0 || cellWidth > sheet.width ||
        cellHeight > sheet.height) {
        if (error) {
            *error = "a " + std::to_string(cellWidth) + " x " + std::to_string(cellHeight) +
                     " cell does not fit in a " + std::to_string(sheet.width) + " x " +
                     std::to_string(sheet.height) + " sheet";
        }
        return false;
    }
    const uint32_t columns = sheet.width / cellWidth;
    const uint32_t rows = sheet.height / cellHeight;
    if (static_cast<size_t>(columns) * rows > kMaxFrames) {
        if (error) {
            *error = "that grid makes " + std::to_string(columns * rows) +
                     " frames; Fast holds " + std::to_string(kMaxFrames);
        }
        return false;
    }
    const size_t rowBytes = static_cast<size_t>(cellWidth) * 4u;
    for (uint32_t r = 0; r < rows; ++r) {
        for (uint32_t c = 0; c < columns; ++c) {
            ls::RasterBuffer cell = ls::makeRaster(cellWidth, cellHeight);
            for (uint32_t y = 0; y < cellHeight; ++y) {
                std::memcpy(cell.row(y),
                            sheet.row(r * cellHeight + y) + static_cast<size_t>(c) * rowBytes,
                            rowBytes);
            }
            cells->push_back(std::move(cell));
        }
    }
    // Empty cells at the end are the unused rest of the last row, not frames.
    const auto empty = [](const ls::RasterBuffer& cell) {
        for (uint32_t y = 0; y < cell.height; ++y) {
            const uint8_t* row = cell.row(y);
            for (uint32_t x = 0; x < cell.width; ++x) {
                if (row[static_cast<size_t>(x) * 4u + 3u] != 0) {
                    return false;
                }
            }
        }
        return true;
    };
    while (cells->size() > 1 && empty(cells->back())) {
        cells->pop_back();
    }
    return true;
}

} // namespace fast
