// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

// adjust_tests.cpp — adjusting colours without baking pixels.
//
// One colour: invert, brightness, contrast and hue do what they say and keep
// alpha; nothing is identity. A layer: its own colours change, its elements
// stay elements, a slot it paints through changes only when asked, a dither's
// two ends both move, and the adjustment always starts from the record -- so
// back to zero is exactly where it began.

#include "app/adjust.h"
#include "app/dither.h"
#include "app/document.h"
#include "app/element.h"
#include "app/ink.h"
#include "app/paint.h"
#include "app/palette.h"

#include <cstdio>
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

bool same(ls::Color a, ls::Color b) {
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

ls::Color at(Document& doc, int x, int y) {
    auto compiled = doc.engine().compileSprite(
        doc.sprite(), compileProfile(ls::CompileProfileType::Export, 8, 8));
    return compiled.ok() ? ls::readPixel(compiled.value.raster, x, y) : ls::Color{ 0, 0, 0, 0 };
}

void testOneColour() {
    const ls::Color c{ 200, 100, 50, 180 };
    CHECK(same(adjustColour(c, ColourAdjust{}), c));
    ColourAdjust invert;
    invert.invert = true;
    CHECK(same(adjustColour(c, invert), ls::Color{ 55, 155, 205, 180 }));
    ColourAdjust brighter;
    brighter.brightness = 0.2f;
    const ls::Color b = adjustColour(c, brighter);
    CHECK(b.r == 251 && b.g == 151 && b.b == 101 && b.a == 180);
    ColourAdjust flat;
    flat.contrast = -1.f;
    const ls::Color f = adjustColour(c, flat);
    CHECK(f.r == 128 && f.g == 128 && f.b == 128);
    ColourAdjust turned;
    turned.hue = 120.f;
    const ls::Color t = adjustColour(ls::Color{ 255, 0, 0, 255 }, turned);
    CHECK(t.g == 255 && t.r == 0 && t.b == 0);
}

void testALayersColours() {
    Document doc;
    REQUIRE(doc.create("adjust", 8, 8));
    REQUIRE(ensurePalette(doc, doc.sprite()));
    const ls::ColorRole slot = addPaletteEntry(doc, doc.sprite(), ls::Color{ 20, 120, 220, 255 });
    PaintLayer layer;
    REQUIRE(createPaintLayer(doc, doc.sprite(), "body", ls::Color{ 200, 40, 40, 255 }, &layer));
    doc.beginAction("Paint");
    REQUIRE(paintPixels(doc, layer, { { 1, 1 } }));
    Ink through;
    through.colour = ls::Color{ 20, 120, 220, 255 };
    through.role = slot;
    InkStroke stroke;
    REQUIRE(beginInkStroke(doc, layer.layer, through, &stroke));
    REQUIRE(strokeInk(doc, stroke, { { 3, 3 } }));
    doc.endAction();
    const size_t elements = elementsOf(doc, layer.layer).size();

    ColourAdjust invert;
    invert.invert = true;
    const AdjustBase own = adjustBase(doc, { layer.layer }, false);
    REQUIRE(applyAdjust(doc, own, invert));
    CHECK(same(at(doc, 1, 1), ls::Color{ 55, 215, 215, 255 }));
    CHECK(same(at(doc, 3, 3), ls::Color{ 20, 120, 220, 255 }));   // the slot is the palette's
    CHECK(elementsOf(doc, layer.layer).size() == elements);        // still its elements

    // Back to nothing is back to exactly what was there.
    REQUIRE(applyAdjust(doc, own, ColourAdjust{}));
    CHECK(same(at(doc, 1, 1), ls::Color{ 200, 40, 40, 255 }));

    // With the slots, the slot moves too -- in the palette.
    const AdjustBase withSlots = adjustBase(doc, { layer.layer }, true);
    CHECK(withSlots.slots.size() == 1);
    REQUIRE(applyAdjust(doc, withSlots, invert));
    CHECK(same(at(doc, 3, 3), ls::Color{ 235, 135, 35, 255 }));
    ls::Color entry;
    CHECK(resolvePaletteRole(doc, slot, &entry) && entry.r == 235);
}

void testADithersEnds() {
    Document doc;
    REQUIRE(doc.create("dither", 8, 8));
    PaintLayer layer;
    REQUIRE(createPaintLayer(doc, doc.sprite(), "sky", ls::Color{ 0, 0, 0, 255 }, &layer));
    std::vector<ls::Vec2i> all;
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) { all.push_back({ x, y }); }
    }
    doc.beginAction("Fill");
    REQUIRE(paintPixels(doc, layer, all));
    doc.endAction();
    DitherSettings settings;
    settings.from = ls::Color{ 10, 20, 30, 255 };
    settings.to = ls::Color{ 240, 230, 220, 255 };
    doc.beginAction("Dither");
    REQUIRE(setLayerDithered(doc, layer, settings));
    doc.endAction();

    ColourAdjust invert;
    invert.invert = true;
    const AdjustBase base = adjustBase(doc, { layer.layer }, false);
    REQUIRE(applyAdjust(doc, base, invert));
    DitherSettings now;
    REQUIRE(readDitherSettings(doc, layer, &now));
    CHECK(same(now.from, ls::Color{ 245, 235, 225, 255 }));
    CHECK(same(now.to, ls::Color{ 15, 25, 35, 255 }));
}

} // namespace

int main() {
    testOneColour();
    testALayersColours();
    testADithersEnds();
    if (failures == 0) {
        std::printf("adjust: all passed\n");
        return 0;
    }
    std::printf("adjust: %d failure(s)\n", failures);
    return 1;
}
