// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

// palette_tests.cpp — colours as names.
//
// The claim worth checking is the one that makes a palette worth having on this
// engine rather than any other: changing an entry recolours every layer using
// it, from the drawing, without touching a pixel of it.

#include "app/palette.h"
#include "app/transform.h"

#include <cstdio>
#include <string>

static int failures = 0;

#define CHECK(...)                                                            \
    do {                                                                      \
        if (!(__VA_ARGS__)) {                                                 \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #__VA_ARGS__);\
            ++failures;                                                       \
        }                                                                     \
    } while (0)

#define REQUIRE(...) do { if (!(__VA_ARGS__)) { CHECK(__VA_ARGS__); return; } } while (0)

using namespace ls;

namespace {

constexpr uint32_t kSize = 16;

CompileProfile profile() {
    CompileProfile p;
    p.type = CompileProfileType::Export;
    p.outputWidth = kSize;
    p.outputHeight = kSize;
    p.palette = PalettePolicy::Unconstrained;
    return p;
}

struct Canvas {
    fast::Document doc;
    SpriteId       sprite;

    bool build() {
        if (!doc.create("palette", kSize, kSize)) { return false; }
        sprite = doc.engine().createSprite(doc.id()).value;
        return fast::ensurePalette(doc, sprite);
    }

    fast::PaintLayer addLayer(const std::string& name, Color colour, int32_t row) {
        fast::PaintLayer layer;
        fast::createPaintLayer(doc, sprite, name, colour, &layer);
        fast::paintPixels(doc, layer, fast::linePixels({2, row}, {13, row}));
        return layer;
    }

    Color at(int32_t x, int32_t y) {
        auto compiled = doc.engine().compileSprite(sprite, profile());
        if (compiled.fail()) { return Color{0, 0, 0, 0}; }
        return readPixel(compiled.value.raster, x, y);
    }

    std::vector<uint8_t> pixels() {
        auto compiled = doc.engine().compileSprite(sprite, profile());
        return compiled.ok() ? compiled.value.raster.pixels : std::vector<uint8_t>();
    }

    int opaque() {
        auto compiled = doc.engine().compileSprite(sprite, profile());
        if (compiled.fail()) { return -1; }
        int count = 0;
        for (uint32_t y = 0; y < kSize; ++y) {
            for (uint32_t x = 0; x < kSize; ++x) {
                count += readPixel(compiled.value.raster, static_cast<int32_t>(x),
                                   static_cast<int32_t>(y)).a != 0;
            }
        }
        return count;
    }
};

// ---------------------------------------------------------------------------

void testADocumentGetsAStarterPalette() {
    Canvas canvas;
    REQUIRE(canvas.build());

    const std::vector<fast::PaletteEntry> entries = fast::paletteEntries(canvas.doc);
    CHECK(entries.size() == 16);
    CHECK(entries.front().role == 0);

    // Calling again must not build a second palette or reset the first.
    REQUIRE(fast::setPaletteEntry(canvas.doc, 0, Color{1, 2, 3, 255}));
    REQUIRE(fast::ensurePalette(canvas.doc, canvas.sprite));
    CHECK(fast::paletteEntries(canvas.doc).size() == 16);
    CHECK(fast::paletteEntries(canvas.doc).front().color.r == 1);
}

// The claim: one call, and every layer using that role changes.
void testChangingAnEntryRecoloursEveryLayerUsingIt() {
    Canvas canvas;
    REQUIRE(canvas.build());

    fast::PaintLayer first = canvas.addLayer("a", Color{10, 10, 10, 255}, 4);
    fast::PaintLayer second = canvas.addLayer("b", Color{20, 20, 20, 255}, 8);
    fast::PaintLayer other = canvas.addLayer("c", Color{30, 30, 30, 255}, 12);

    REQUIRE(fast::setLayerRole(canvas.doc, first, 8));
    REQUIRE(fast::setLayerRole(canvas.doc, second, 8));
    // `other` keeps its own colour.

    REQUIRE(fast::setPaletteEntry(canvas.doc, 8, Color{200, 60, 40, 255}));

    const int drawn = canvas.opaque();
    CHECK(canvas.at(5, 4).r == 200);
    CHECK(canvas.at(5, 8).r == 200);
    CHECK(canvas.at(5, 12).r == 30);        // untouched, as it should be

    // One more change, and the drawing is still exactly the same size: this
    // recolours, it does not repaint.
    REQUIRE(fast::setPaletteEntry(canvas.doc, 8, Color{40, 90, 200, 255}));
    CHECK(canvas.at(5, 4).b == 200);
    CHECK(canvas.at(5, 8).b == 200);
    CHECK(canvas.opaque() == drawn);
}

// A hundred palette edits must leave the document exactly as large as one.
void testRecolouringCostsNothing() {
    Canvas canvas;
    REQUIRE(canvas.build());
    fast::PaintLayer layer = canvas.addLayer("a", Color{10, 10, 10, 255}, 4);
    REQUIRE(fast::setLayerRole(canvas.doc, layer, 3));

    const size_t operations =
        canvas.doc.engine().getLayerOperations(layer.layer).value.size();

    for (int i = 0; i < 100; ++i) {
        fast::setPaletteEntry(canvas.doc, 3,
                              Color{static_cast<uint8_t>(i * 2), 80, 120, 255});
    }

    CHECK(canvas.doc.engine().getLayerOperations(layer.layer).value.size()
          == operations);
    CHECK(fast::paletteEntries(canvas.doc).size() == 16);

    // Setting it back gives the same picture as before the hundred changes.
    REQUIRE(fast::setPaletteEntry(canvas.doc, 3, Color{7, 80, 120, 255}));
    const std::vector<uint8_t> once = canvas.pixels();
    for (int i = 0; i < 20; ++i) {
        fast::setPaletteEntry(canvas.doc, 3, Color{static_cast<uint8_t>(i), 1, 2, 255});
    }
    REQUIRE(fast::setPaletteEntry(canvas.doc, 3, Color{7, 80, 120, 255}));
    CHECK(canvas.pixels() == once);
}

void testDetachingKeepsWhatIsOnScreen() {
    Canvas canvas;
    REQUIRE(canvas.build());
    fast::PaintLayer layer = canvas.addLayer("a", Color{10, 10, 10, 255}, 4);

    REQUIRE(fast::setLayerRole(canvas.doc, layer, 8));
    REQUIRE(fast::setPaletteEntry(canvas.doc, 8, Color{200, 60, 40, 255}));
    CHECK(canvas.at(5, 4).r == 200);
    CHECK(fast::layerRole(canvas.doc, layer) == 8);

    const Color shown = fast::effectiveLayerColor(canvas.doc, canvas.sprite, layer);
    CHECK(shown.r == 200);

    // Detaching leaves it with the colour it was showing, not the one it had
    // before the role was applied. Anything else is a visible jump the user did
    // not ask for.
    REQUIRE(fast::setPaintColor(canvas.doc, layer, shown));
    REQUIRE(fast::setLayerRole(canvas.doc, layer, kColorRoleNone));
    CHECK(canvas.at(5, 4).r == 200);
    CHECK(fast::layerRole(canvas.doc, layer) == kColorRoleNone);

    // And the palette no longer reaches it.
    REQUIRE(fast::setPaletteEntry(canvas.doc, 8, Color{20, 200, 20, 255}));
    CHECK(canvas.at(5, 4).r == 200);
}

void testAddingAnEntry() {
    Canvas canvas;
    REQUIRE(canvas.build());

    const ColorRole role = fast::addPaletteEntry(canvas.doc, canvas.sprite,
                                                 Color{123, 45, 67, 255});
    CHECK(role != kColorRoleNone);
    CHECK(role == 16);                      // after the sixteen starters
    CHECK(fast::paletteEntries(canvas.doc).size() == 17);

    const ColorRole second = fast::addPaletteEntry(canvas.doc, canvas.sprite,
                                                   Color{1, 2, 3, 255});
    CHECK(second == 17);
    CHECK(second != role);
}

// A palette is document state, so it has to survive being written to a file.
void testThePaletteSurvivesAReload() {
    Canvas canvas;
    REQUIRE(canvas.build());
    fast::PaintLayer layer = canvas.addLayer("a", Color{10, 10, 10, 255}, 4);
    REQUIRE(fast::setLayerRole(canvas.doc, layer, 8));
    REQUIRE(fast::setPaletteEntry(canvas.doc, 8, Color{200, 60, 40, 255}));
    const std::vector<uint8_t> before = canvas.pixels();

    std::string error;
    const std::string path = "palette_test.lsprite";
    REQUIRE(canvas.doc.save(path, &error));

    fast::Document reopened;
    REQUIRE(reopened.open(path, &error));

    SpriteId sprite;
    std::vector<fast::PaintLayer> layers;
    REQUIRE(fast::adoptPaintLayers(reopened, &sprite, &layers));
    REQUIRE(layers.size() == 1);

    auto compiled = reopened.engine().compileSprite(sprite, profile());
    REQUIRE(compiled.ok());
    CHECK(compiled.value.raster.pixels == before);

    CHECK(fast::paletteEntries(reopened).size() == 16);
    CHECK(fast::layerRole(reopened, layers.front()) == 8);

    // Still live: the reloaded palette still drives the reloaded layer.
    REQUIRE(fast::setPaletteEntry(reopened, 8, Color{20, 200, 20, 255}));
    auto after = reopened.engine().compileSprite(sprite, profile());
    REQUIRE(after.ok());
    CHECK(readPixel(after.value.raster, 5, 4).g == 200);
}

// Files written before palettes existed carry no role and must keep working.
void testALayerWithoutARoleStillPaints() {
    Canvas canvas;
    REQUIRE(canvas.build());
    fast::PaintLayer layer = canvas.addLayer("a", Color{170, 90, 30, 255}, 4);

    CHECK(fast::layerRole(canvas.doc, layer) == kColorRoleNone);
    CHECK(canvas.at(5, 4).r == 170);
    CHECK(fast::effectiveLayerColor(canvas.doc, canvas.sprite, layer).r == 170);

    // A palette change must not reach a layer that never asked for one.
    REQUIRE(fast::setPaletteEntry(canvas.doc, 0, Color{5, 5, 5, 255}));
    CHECK(canvas.at(5, 4).r == 170);
}

} // namespace

int main() {
    testADocumentGetsAStarterPalette();
    testChangingAnEntryRecoloursEveryLayerUsingIt();
    testRecolouringCostsNothing();
    testDetachingKeepsWhatIsOnScreen();
    testAddingAnEntry();
    testThePaletteSurvivesAReload();
    testALayerWithoutARoleStillPaints();

    if (failures == 0) {
        std::printf("fast_palette: all checks passed\n");
        return 0;
    }
    std::printf("fast_palette: %d check(s) failed\n", failures);
    return 1;
}
