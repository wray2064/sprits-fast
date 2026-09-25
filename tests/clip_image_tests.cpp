// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

// clip_image_tests.cpp — pictures on the system clipboard, both ways.
//
// Out: a layer's pixels under a mask become an image the size of the mask,
// transparent outside it, dithers and all; the BMP of an image is one a
// decoder reads back pixel for pixel, alpha included.
//
// In: an image becomes one piece per colour, a colour a slot has exactly
// taking the slot; transparent pixels are left out; too many colours reduce
// to the nearest slots, and with no palette to reduce to, refuse; a pasted
// clip floats and drops like any other, following the palette afterwards.

#include "app/clip_image.h"
#include "app/document.h"
#include "app/floating.h"
#include "app/image_io.h"
#include "app/ink.h"
#include "app/paint.h"
#include "app/palette.h"
#include "app/selection.h"

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

constexpr uint32_t kSize = 16;

const ls::Color kRed   { 200, 30, 30, 255 };
const ls::Color kBlue  { 30, 30, 200, 255 };
const ls::Color kGhost { 10, 200, 10, 128 };

bool same(ls::Color a, ls::Color b) {
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

ls::Color at(Document& doc, int x, int y) {
    ls::CompileProfile profile =
        compileProfile(ls::CompileProfileType::Export, kSize, kSize);
    auto compiled = doc.engine().compileSprite(doc.sprite(), profile);
    if (compiled.fail()) { return ls::Color{ 0, 0, 0, 0 }; }
    return ls::readPixel(compiled.value.raster, x, y);
}

bool paint(Document& doc, ls::LayerId layer, ls::Color colour,
           const std::vector<ls::Vec2i>& pixels) {
    Ink ink;
    ink.colour = colour;
    doc.beginAction("Pencil");
    InkStroke stroke;
    if (!beginInkStroke(doc, layer, ink, &stroke) || !strokeInk(doc, stroke, pixels)) {
        doc.abandonAction();
        return false;
    }
    doc.endAction();
    return true;
}

void testMaskedImageIsTheMasksSize() {
    Document doc;
    REQUIRE(doc.create("out", kSize, kSize));
    PaintLayer layer;
    REQUIRE(createPaintLayer(doc, doc.sprite(), "body", kRed, &layer));
    REQUIRE(paint(doc, layer.layer, kRed, {{ 2, 2 }, { 3, 2 }, { 4, 2 }}));
    REQUIRE(paint(doc, layer.layer, kBlue, {{ 3, 3 }}));

    ls::RasterBuffer canvas;
    REQUIRE(layerImage(doc, layer.layer, &canvas));
    CHECK(canvas.width == kSize && canvas.height == kSize);
    // A mask of two rows, with a hole where (4, 2) is left out.
    ls::IntervalSet mask;
    mask.intervals.push_back({ 2, 2, 4 });
    mask.intervals.push_back({ 3, 2, 5 });
    const ls::RasterBuffer image = imageOfMask(canvas, mask);
    REQUIRE(image.width == 3 && image.height == 2);
    CHECK(same(ls::readPixel(image, 0, 0), kRed));
    CHECK(same(ls::readPixel(image, 1, 0), kRed));
    CHECK(ls::readPixel(image, 2, 0).a == 0);                   // (4, 2): not masked
    CHECK(same(ls::readPixel(image, 1, 1), kBlue));
    CHECK(ls::readPixel(image, 0, 1).a == 0);                   // masked, but empty

    // A mask wholly off the canvas makes nothing.
    ls::IntervalSet off;
    off.intervals.push_back({ -5, -5, -2 });
    CHECK(imageOfMask(canvas, off).empty());
}

void testBmpReadsBack() {
    ls::RasterBuffer image = ls::makeRaster(3, 2);
    ls::writePixel(image, 0, 0, kRed);
    ls::writePixel(image, 2, 0, kBlue);
    ls::writePixel(image, 1, 1, kGhost);
    const std::vector<uint8_t> bmp = encodeBmp(image);
    REQUIRE(bmp.size() == 14 + 124 + 3 * 2 * 4);
    CHECK(bmp[0] == 'B' && bmp[1] == 'M');
    ls::RasterBuffer back;
    std::string error;
    REQUIRE(decodeImage(bmp, &back, &error));
    REQUIRE(back.width == 3 && back.height == 2);
    for (int y = 0; y < 2; ++y) {
        for (int x = 0; x < 3; ++x) {
            CHECK(same(ls::readPixel(back, x, y), ls::readPixel(image, x, y)));
        }
    }
}

void testColoursBecomePiecesAndSlots() {
    std::vector<PaletteEntry> palette(1);
    palette[0].role = ls::ColorRole{ 7 };
    palette[0].color = kBlue;

    ls::RasterBuffer image = ls::makeRaster(4, 2);
    ls::writePixel(image, 0, 0, kRed);
    ls::writePixel(image, 1, 0, kRed);
    ls::writePixel(image, 2, 0, kBlue);
    ls::writePixel(image, 3, 1, kGhost);
    PixelClip clip;
    ImageClipReport report;
    REQUIRE(clipFromImage(image, { 5, 6 }, palette, &clip, &report));
    CHECK(report.colours == 3 && report.slots == 1 && !report.reduced);
    CHECK(ls::geom::pixelCount(clip.mask) == 4);
    CHECK(ls::geom::contains(clip.mask, { 5, 6 }) && ls::geom::contains(clip.mask, { 8, 7 }));
    CHECK(!ls::geom::contains(clip.mask, { 5, 7 }));          // transparent, left out
    int slotted = 0;
    for (const PixelClip::Piece& piece : clip.pieces) {
        if (piece.ink.usesSlot()) {
            ++slotted;
            CHECK(piece.ink.role == palette[0].role);
            CHECK(ls::geom::pixelCount(piece.pixels) == 1 &&
                  ls::geom::contains(piece.pixels, { 7, 6 }));
        } else if (same(piece.ink.colour, kRed)) {
            CHECK(ls::geom::pixelCount(piece.pixels) == 2);
        } else {
            CHECK(same(piece.ink.colour, kGhost));
        }
    }
    CHECK(slotted == 1);

    // Nothing opaque, nothing to paste.
    PixelClip none;
    CHECK(!clipFromImage(ls::makeRaster(2, 2), { 0, 0 }, palette, &none, nullptr));
}

void testTooManyColoursReduce() {
    // Four hundred colours, every pixel its own: reds on the left half and
    // blues on the right, each nudged by where it is.
    ls::RasterBuffer image = ls::makeRaster(20, 20);
    for (int y = 0; y < 20; ++y) {
        for (int x = 0; x < 20; ++x) {
            const uint8_t ux = static_cast<uint8_t>(x);
            const uint8_t uy = static_cast<uint8_t>(y);
            ls::writePixel(image, x, y,
                           x < 10 ? ls::Color{ 200, ux, uy, 255 } : ls::Color{ ux, uy, 200, 255 });
        }
    }
    std::vector<PaletteEntry> palette(2);
    palette[0].role = ls::ColorRole{ 1 };
    palette[0].color = kRed;
    palette[1].role = ls::ColorRole{ 2 };
    palette[1].color = kBlue;
    PixelClip clip;
    ImageClipReport report;
    REQUIRE(clipFromImage(image, { 0, 0 }, palette, &clip, &report));
    CHECK(report.reduced);
    CHECK(clip.pieces.size() == 2 && report.slots == 2);
    for (const PixelClip::Piece& piece : clip.pieces) {
        CHECK(ls::geom::pixelCount(piece.pixels) == 200);
        const ls::Rect2i box = ls::geom::bounds(piece.pixels);
        CHECK(piece.ink.role == palette[0].role ? box.max.x == 10 : box.min.x == 10);
    }

    PixelClip refused;
    CHECK(!clipFromImage(image, { 0, 0 }, {}, &refused, nullptr));
}

void testAPastedImageFloatsAndFollowsThePalette() {
    Document doc;
    REQUIRE(doc.create("in", kSize, kSize));
    REQUIRE(ensurePalette(doc, doc.sprite()));
    const ls::ColorRole slot = addPaletteEntry(doc, doc.sprite(), kBlue);
    PaintLayer layer;
    REQUIRE(createPaintLayer(doc, doc.sprite(), "body", kRed, &layer));

    ls::RasterBuffer image = ls::makeRaster(2, 1);
    ls::writePixel(image, 0, 0, kBlue);
    ls::writePixel(image, 1, 0, kRed);
    const ls::Vec2i where = pastePosition(image.width, image.height, kSize, kSize);
    CHECK(where.x == 7 && where.y == 7);
    PixelClip clip;
    REQUIRE(clipFromImage(image, where, paletteEntries(doc), &clip, nullptr));

    doc.beginAction("Paste");
    Floating floating;
    REQUIRE(floatClip(doc, layer.layer, clip, &floating));
    REQUIRE(dropFloating(doc, floating));
    doc.endAction();
    CHECK(same(at(doc, 7, 7), kBlue));
    CHECK(same(at(doc, 8, 7), kRed));

    doc.beginAction("Slot");
    REQUIRE(setPaletteEntry(doc, slot, kGhost));
    doc.endAction();
    CHECK(same(at(doc, 7, 7), kGhost));
    CHECK(same(at(doc, 8, 7), kRed));

    // Larger than the canvas: at its corner.
    const ls::Vec2i big = pastePosition(40, 4, kSize, kSize);
    CHECK(big.x == 0 && big.y == 6);
}

} // namespace

int main() {
    testMaskedImageIsTheMasksSize();
    testBmpReadsBack();
    testColoursBecomePiecesAndSlots();
    testTooManyColoursReduce();
    testAPastedImageFloatsAndFollowsThePalette();
    if (failures == 0) {
        std::printf("clip_image: all passed\n");
        return 0;
    }
    std::printf("clip_image: %d failure(s)\n", failures);
    return 1;
}
