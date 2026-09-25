// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

// import_tests.cpp — pictures made elsewhere, opened as documents.
//
// The promises: a picture opened and compiled is the same picture, pixel for
// pixel, frame for frame; its colours become the palette when they fit, so a
// slot edit recolours it; one with more colours keeps them as values; a
// photograph is refused with a reason that says what to do instead; a sheet
// slices into frames and drops the empty tail; a GIF's frame count is read
// from its structure before anything is decoded.

#include "app/animation.h"
#include "app/document.h"
#include "app/element.h"
#include "app/image_io.h"
#include "app/import_image.h"
#include "app/palette.h"

#include <cstdio>
#include <string>
#include <vector>

static int failures = 0;

#define CHECK(...)                                                            \
    do {                                                                      \
        if (!(__VA_ARGS__)) {                                                 \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #__VA_ARGS__);\
            ++failures;                                                       \
        }                                                                     \
    } while (0)

#define REQUIRE(...) do { if (!(__VA_ARGS__)) { CHECK(__VA_ARGS__); return; } } while (0)

namespace {

using namespace fast;

void put(ls::RasterBuffer& raster, uint32_t x, uint32_t y, ls::Color c) {
    uint8_t* p = raster.row(y) + static_cast<size_t>(x) * 4u;
    p[0] = c.r; p[1] = c.g; p[2] = c.b; p[3] = c.a;
}

ls::RasterBuffer compiled(Document& doc, ls::SpriteId sprite) {
    auto size = doc.engine().getCanvasSize(doc.id());
    const ls::CompileProfile profile =
        compileProfile(ls::CompileProfileType::Export, static_cast<uint32_t>(size.value.x),
                       static_cast<uint32_t>(size.value.y));
    auto result = doc.engine().compileSprite(sprite, profile);
    return result.ok() ? result.value.raster : ls::RasterBuffer{};
}

bool identical(const ls::RasterBuffer& a, const ls::RasterBuffer& b) {
    if (a.width != b.width || a.height != b.height) {
        return false;
    }
    for (uint32_t y = 0; y < a.height; ++y) {
        for (uint32_t x = 0; x < a.width; ++x) {
            const uint8_t* p = a.row(y) + static_cast<size_t>(x) * 4u;
            const uint8_t* q = b.row(y) + static_cast<size_t>(x) * 4u;
            // Fully transparent is transparent, whatever colour it carries.
            if (p[3] == 0 && q[3] == 0) {
                continue;
            }
            if (p[0] != q[0] || p[1] != q[1] || p[2] != q[2] || p[3] != q[3]) {
                std::printf("  at %u,%u: %d %d %d %d vs %d %d %d %d\n", x, y,
                            p[0], p[1], p[2], p[3], q[0], q[1], q[2], q[3]);
                return false;
            }
        }
    }
    return true;
}

ls::RasterBuffer figure() {
    ls::RasterBuffer raster = ls::makeRaster(8, 6);
    const ls::Color skin { 230, 180, 140, 255 };
    const ls::Color hair { 60, 30, 20, 255 };
    const ls::Color glass { 90, 160, 255, 128 };      // half transparent, kept as such
    for (uint32_t x = 1; x < 7; ++x) { put(raster, x, 0, hair); }
    for (uint32_t y = 1; y < 5; ++y) {
        for (uint32_t x = 2; x < 6; ++x) { put(raster, x, y, skin); }
    }
    put(raster, 3, 2, glass);
    put(raster, 4, 2, glass);
    return raster;
}

void testAPictureOpensAsItself() {
    Document doc;
    ImportReport report;
    std::string error;
    const ls::RasterBuffer picture = figure();
    REQUIRE(documentFromFrames(doc, "figure", { picture }, {}, &report, &error));
    CHECK(report.frames == 1 && report.colours == 3 && report.throughPalette);
    CHECK(identical(compiled(doc, doc.sprite()), picture));
    CHECK(!doc.modified() && !doc.canUndo());       // opening is not editing

    // One layer, one element per colour, every one through its slot.
    auto info = doc.engine().getSpriteInfo(doc.sprite());
    REQUIRE(info.ok() && info.value.layers.size() == 1);
    CHECK(elementsOf(doc, info.value.layers.front()).size() == 3);
    const std::vector<PaletteEntry> slots = paletteEntries(doc);
    REQUIRE(slots.size() == 3);
    CHECK(slots[0].color.r == 60);                   // hair, the first colour met

    // And so a slot edit recolours the imported pixels, from the drawing.
    doc.beginAction("Slot");
    REQUIRE(setPaletteEntry(doc, slots[0].role, ls::Color{ 250, 250, 0, 255 }));
    doc.endAction();
    const ls::RasterBuffer after = compiled(doc, doc.sprite());
    CHECK(after.row(0)[4] == 250 && after.row(0)[5] == 250);
}

void testManyColoursStayValues() {
    ls::RasterBuffer busy = ls::makeRaster(20, 20);
    for (uint32_t y = 0; y < 20; ++y) {
        for (uint32_t x = 0; x < 20; ++x) {
            put(busy, x, y, ls::Color{ static_cast<uint8_t>(x * 12), static_cast<uint8_t>(y * 12),
                                       77, 255 });
        }
    }
    Document doc;
    ImportReport report;
    std::string error;
    REQUIRE(documentFromFrames(doc, "busy", { busy }, {}, &report, &error));
    CHECK(report.colours == 400 && !report.throughPalette);
    CHECK(identical(compiled(doc, doc.sprite()), busy));
}

void testAPhotographIsRefused() {
    ls::RasterBuffer photo = ls::makeRaster(80, 80);
    for (uint32_t y = 0; y < 80; ++y) {
        for (uint32_t x = 0; x < 80; ++x) {
            put(photo, x, y, ls::Color{ static_cast<uint8_t>(x * 3), static_cast<uint8_t>(y * 3),
                                        static_cast<uint8_t>((x * y) & 0xFF), 255 });
        }
    }
    Document doc;
    std::string error;
    CHECK(!documentFromFrames(doc, "photo", { photo }, {}, nullptr, &error));
    CHECK(error.find("reference") != std::string::npos);
}

void testFramesKeepTheirHolds() {
    ls::RasterBuffer a = figure();
    ls::RasterBuffer b = figure();
    put(b, 0, 5, ls::Color{ 255, 0, 0, 255 });       // a colour only the second frame has
    Document doc;
    ImportReport report;
    std::string error;
    REQUIRE(documentFromFrames(doc, "walk", { a, b }, { 80, 0 }, &report, &error));
    const std::vector<Frame> frames = readFrames(doc);
    REQUIRE(frames.size() == 2);
    CHECK(frames[0].durationMs == 80);
    CHECK(frames[1].durationMs == kDefaultFrameMs);   // a zero hold takes the default
    CHECK(identical(compiled(doc, frames[0].sprite), a));
    CHECK(identical(compiled(doc, frames[1].sprite), b));
    CHECK(report.colours == 4);
}

void testSheetsSliceAndDropTheEmptyTail() {
    ls::RasterBuffer sheet = ls::makeRaster(12, 8);     // 3 x 2 cells of 4 x 4
    const ls::Color ink { 10, 20, 30, 255 };
    put(sheet, 1, 1, ink);            // cell 0
    put(sheet, 5, 1, ink);            // cell 1
    put(sheet, 9, 1, ink);            // cell 2
    put(sheet, 1, 5, ink);            // cell 3; cells 4 and 5 are empty
    std::vector<ls::RasterBuffer> cells;
    std::string error;
    REQUIRE(sliceSheet(sheet, 4, 4, &cells, &error));
    CHECK(cells.size() == 4);
    CHECK(cells[1].row(1)[4 + 3] == 255);             // cell 1's pixel at (1,1)
    CHECK(!sliceSheet(sheet, 20, 4, &cells, &error));
}

void testGifFramesAreCountedBeforeDecoding() {
    // A minimal two-frame GIF, 1x1, written out by hand: header, screen,
    // two images of one pixel each, trailer.
    const std::vector<uint8_t> gif = {
        'G', 'I', 'F', '8', '9', 'a', 1, 0, 1, 0, 0x80, 0, 0,      // 2-colour global table
        0, 0, 0, 255, 255, 255,
        0x21, 0xF9, 4, 0, 10, 0, 0, 0,                               // 100 ms
        0x2C, 0, 0, 0, 0, 1, 0, 1, 0, 0, 2, 2, 0x4C, 0x01, 0,
        0x21, 0xF9, 4, 0, 10, 0, 0, 0,
        0x2C, 0, 0, 0, 0, 1, 0, 1, 0, 0, 2, 2, 0x44, 0x01, 0,
        0x3B,
    };
    CHECK(countGifFrames(gif) == 2);
    std::vector<ls::RasterBuffer> frames;
    std::vector<int> holds;
    std::string error;
    REQUIRE(decodeFrames(gif, &frames, &holds, &error));
    CHECK(frames.size() == 2);
    CHECK(holds.size() == 2 && holds[0] == 100);

    // Truncated in the middle of a block: not a GIF, whatever it says.
    std::vector<uint8_t> cut(gif.begin(), gif.begin() + 30);
    cut[29] = 200;
    CHECK(countGifFrames(cut) == 0);
    CHECK(countGifFrames({ 'P', 'N', 'G' }) == 0);
}

} // namespace

int main() {
    testAPictureOpensAsItself();
    testManyColoursStayValues();
    testAPhotographIsRefused();
    testFramesKeepTheirHolds();
    testSheetsSliceAndDropTheEmptyTail();
    testGifFramesAreCountedBeforeDecoding();
    if (failures == 0) {
        std::printf("import: all passed\n");
        return 0;
    }
    std::printf("import: %d failure(s)\n", failures);
    return 1;
}
