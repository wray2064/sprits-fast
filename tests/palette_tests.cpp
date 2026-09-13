// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

// palette_tests.cpp — colours as names.
//
// The claim worth checking is the one that makes a palette worth having on this
// engine rather than any other: changing an entry recolours every layer using
// it, from the drawing, without touching a pixel of it.

#include "app/animation.h"
#include "app/dither.h"
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
        sprite = doc.sprite();
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


// The gap that was there: a dithered layer sat outside the palette entirely,
// because its ramp held literal colours. Now each end of the ramp can follow a
// slot, and a palette change reaches the dither like everything else.
void testADitheredLayerFollowsThePalette() {
    Canvas canvas;
    REQUIRE(canvas.build());

    fast::PaintLayer layer = canvas.addLayer("shade", Color{10, 10, 10, 255}, 4);
    // A solid block, so the dither has room to show both of its colours.
    for (int32_t row = 5; row < 12; ++row) {
        fast::paintPixels(canvas.doc, layer, fast::linePixels({2, row}, {13, row}));
    }

    REQUIRE(fast::setPaletteEntry(canvas.doc, 3, Color{30, 40, 90, 255}));
    REQUIRE(fast::setPaletteEntry(canvas.doc, 4, Color{250, 200, 120, 255}));

    fast::DitherSettings dither;
    dither.pattern = ls::DitherPatternKind::Bayer4;
    dither.modulation = ls::DitherModulation::Constant;
    dither.density = 0.5f;
    dither.fromRole = 3;
    dither.toRole = 4;
    dither.from = Color{30, 40, 90, 255};
    dither.to = Color{250, 200, 120, 255};
    REQUIRE(fast::setLayerDithered(canvas.doc, layer, dither));

    // It reads back as roles, not as whatever colour they resolved to.
    fast::DitherSettings read;
    REQUIRE(fast::readDitherSettings(canvas.doc, layer, &read));
    CHECK(read.fromRole == 3);
    CHECK(read.toRole == 4);

    const std::vector<uint8_t> day = canvas.pixels();
    REQUIRE(!day.empty());

    // Change both slots. Nothing touches the layer.
    REQUIRE(fast::setPaletteEntry(canvas.doc, 3, Color{60, 20, 20, 255}));
    REQUIRE(fast::setPaletteEntry(canvas.doc, 4, Color{200, 90, 90, 255}));
    const std::vector<uint8_t> night = canvas.pixels();

    CHECK(day != night);
    // And the new colours are what is there, with the old ones gone.
    bool sawNewDark = false, sawNewLight = false, sawOld = false;
    for (int32_t y = 5; y < 12; ++y) {
        for (int32_t x = 2; x < 14; ++x) {
            const Color c = canvas.at(x, y);
            if (c.r == 60 && c.g == 20)   { sawNewDark = true; }
            if (c.r == 200 && c.g == 90)  { sawNewLight = true; }
            if (c.r == 30 || c.r == 250)  { sawOld = true; }
        }
    }
    CHECK(sawNewDark && sawNewLight);
    CHECK(!sawOld);

    // Detaching one end makes it a value again: it stops following.
    read.toRole = ls::kColorRoleNone;
    REQUIRE(fast::applyDitherSettings(canvas.doc, layer, read));
    REQUIRE(fast::setPaletteEntry(canvas.doc, 4, Color{1, 2, 3, 255}));
    bool sawDetached = false;
    for (int32_t y = 5; y < 12; ++y) {
        for (int32_t x = 2; x < 14; ++x) {
            if (canvas.at(x, y).r == 1) { sawDetached = true; }
        }
    }
    CHECK(!sawDetached);
}

} // namespace

// --- several palettes -------------------------------------------------------

// The swap: a second palette, and switching the document to it recolours
// every frame that has no palette of its own -- in one step, from the
// drawing. This is the feature the engine exists for.
void testSwitchingTheDocumentPaletteRecoloursEveryFrame() {
    Canvas canvas;
    REQUIRE(canvas.build());
    fast::PaintLayer layer = canvas.addLayer("body", Color{1, 1, 1, 255}, 5);
    REQUIRE(fast::setLayerRole(canvas.doc, layer, 3));
    REQUIRE(fast::setPaletteEntry(canvas.doc, 3, Color{200, 40, 40, 255}));
    REQUIRE(fast::duplicateFrame(canvas.doc, 0) == 1);
    const std::vector<fast::Frame> frames = fast::readFrames(canvas.doc);
    REQUIRE(frames.size() == 2);

    const PaletteId day = fast::documentPalette(canvas.doc);
    REQUIRE(day.valid());
    CHECK(fast::listPalettes(canvas.doc).size() == 1);

    // A copy to edit, so night starts as day and differs by one slot.
    const PaletteId night = fast::addPalette(canvas.doc, "night", day);
    REQUIRE(night.valid());
    CHECK(fast::listPalettes(canvas.doc).size() == 2);
    CHECK(fast::listPalettes(canvas.doc)[1].name == "night");
    CHECK(fast::paletteEntries(canvas.doc, night).size() ==
          fast::paletteEntries(canvas.doc, day).size());
    REQUIRE(fast::setPaletteEntry(canvas.doc, night, 3, Color{40, 40, 200, 255}));

    // Nothing changed yet: the document still uses day.
    CHECK(canvas.at(5, 5).r == 200);
    CHECK(fast::documentPalette(canvas.doc) == day);

    REQUIRE(fast::usePalette(canvas.doc, night));
    CHECK(fast::documentPalette(canvas.doc) == night);
    for (const fast::Frame& frame : frames) {
        canvas.sprite = frame.sprite;
        CHECK(canvas.at(5, 5).b == 200);
        CHECK(fast::paletteFor(canvas.doc, frame.sprite) == night);
        CHECK(!fast::frameBinding(canvas.doc, frame.sprite).valid());
    }

    // The quick swap steps through the list and wraps.
    CHECK(fast::nextPalette(canvas.doc, night) == day);
    CHECK(fast::nextPalette(canvas.doc, day) == night);

    // Renaming reaches the list.
    REQUIRE(fast::renamePalette(canvas.doc, night, "dusk"));
    CHECK(fast::listPalettes(canvas.doc)[1].name == "dusk");
}

// A frame with a palette of its own ignores the swap; releasing it makes it
// follow again. This is a flash frame, and it is colour cycling.
void testAFrameMayKeepAPaletteOfItsOwn() {
    Canvas canvas;
    REQUIRE(canvas.build());
    fast::PaintLayer layer = canvas.addLayer("body", Color{1, 1, 1, 255}, 5);
    REQUIRE(fast::setLayerRole(canvas.doc, layer, 3));
    REQUIRE(fast::setPaletteEntry(canvas.doc, 3, Color{200, 40, 40, 255}));
    REQUIRE(fast::duplicateFrame(canvas.doc, 0) == 1);
    const std::vector<fast::Frame> frames = fast::readFrames(canvas.doc);

    const PaletteId day = fast::documentPalette(canvas.doc);
    const PaletteId flash = fast::addPalette(canvas.doc, "flash", day);
    REQUIRE(flash.valid());
    REQUIRE(fast::setPaletteEntry(canvas.doc, flash, 3, Color{255, 255, 255, 255}));

    REQUIRE(fast::bindFrame(canvas.doc, frames[1].sprite, flash));
    CHECK(fast::frameBinding(canvas.doc, frames[1].sprite) == flash);
    CHECK(fast::paletteFor(canvas.doc, frames[1].sprite) == flash);
    canvas.sprite = frames[0].sprite;
    CHECK(canvas.at(5, 5).r == 200);
    canvas.sprite = frames[1].sprite;
    CHECK(canvas.at(5, 5).r == 255 && canvas.at(5, 5).g == 255);

    // Editing "the palette this frame uses" edits flash, not day.
    REQUIRE(fast::setPaletteEntry(canvas.doc, fast::paletteFor(canvas.doc, frames[1].sprite),
                                  3, Color{0, 255, 0, 255}));
    CHECK(canvas.at(5, 5).g == 255 && canvas.at(5, 5).r == 0);
    canvas.sprite = frames[0].sprite;
    CHECK(canvas.at(5, 5).r == 200);

    // A document swap leaves the bound frame alone.
    const PaletteId night = fast::addPalette(canvas.doc, "night", day);
    REQUIRE(fast::setPaletteEntry(canvas.doc, night, 3, Color{40, 40, 200, 255}));
    REQUIRE(fast::usePalette(canvas.doc, night));
    CHECK(canvas.at(5, 5).b == 200);
    canvas.sprite = frames[1].sprite;
    CHECK(canvas.at(5, 5).g == 255);

    // Released, it follows the document -- which is night now.
    REQUIRE(fast::bindFrame(canvas.doc, frames[1].sprite, PaletteId{}));
    CHECK(!fast::frameBinding(canvas.doc, frames[1].sprite).valid());
    CHECK(canvas.at(5, 5).b == 200);
}

// Deleting: never the last; the document moves off a deleted one; a frame
// bound to a deleted one follows the document.
void testDeletingAPalette() {
    Canvas canvas;
    REQUIRE(canvas.build());
    const PaletteId day = fast::documentPalette(canvas.doc);
    CHECK(!fast::deletePalette(canvas.doc, day));           // the last one stays

    const PaletteId night = fast::addPalette(canvas.doc, "night", PaletteId{});
    REQUIRE(night.valid());
    CHECK(fast::paletteEntries(canvas.doc, night).size() == 16);   // the starter set
    REQUIRE(fast::bindFrame(canvas.doc, canvas.sprite, night));
    REQUIRE(fast::usePalette(canvas.doc, night));

    REQUIRE(fast::deletePalette(canvas.doc, night));
    CHECK(fast::listPalettes(canvas.doc).size() == 1);
    CHECK(fast::documentPalette(canvas.doc) == day);
    CHECK(!fast::frameBinding(canvas.doc, canvas.sprite).valid());
    CHECK(fast::paletteFor(canvas.doc, canvas.sprite) == day);
    CHECK(!fast::deletePalette(canvas.doc, day));
}

// Files written before this bound the first frame to the palette by name and
// never bound the document. Opened now, the frame has to follow a swap like
// every other, so ensurePalette moves the binding to where it belongs.
void testAnOlderFileFollowsASwap() {
    Canvas canvas;
    REQUIRE(canvas.build());
    const PaletteId day = fast::documentPalette(canvas.doc);
    fast::PaintLayer layer = canvas.addLayer("body", Color{1, 1, 1, 255}, 5);
    REQUIRE(fast::setLayerRole(canvas.doc, layer, 3));

    // The old shape: the sprite bound by name, the document not at all.
    REQUIRE(canvas.doc.engine().bindSpritePalette(canvas.sprite, day).ok());
    CHECK(fast::frameBinding(canvas.doc, canvas.sprite) == day);

    REQUIRE(fast::ensurePalette(canvas.doc, canvas.sprite));
    CHECK(!fast::frameBinding(canvas.doc, canvas.sprite).valid());
    CHECK(fast::documentPalette(canvas.doc) == day);

    const PaletteId night = fast::addPalette(canvas.doc, "night", day);
    REQUIRE(fast::setPaletteEntry(canvas.doc, night, 3, Color{40, 40, 200, 255}));
    REQUIRE(fast::usePalette(canvas.doc, night));
    CHECK(canvas.at(5, 5).b == 200);
}

int main() {
    testADocumentGetsAStarterPalette();
    testSwitchingTheDocumentPaletteRecoloursEveryFrame();
    testAFrameMayKeepAPaletteOfItsOwn();
    testDeletingAPalette();
    testAnOlderFileFollowsASwap();
    testChangingAnEntryRecoloursEveryLayerUsingIt();
    testRecolouringCostsNothing();
    testDetachingKeepsWhatIsOnScreen();
    testAddingAnEntry();
    testThePaletteSurvivesAReload();
    testALayerWithoutARoleStillPaints();
    testADitheredLayerFollowsThePalette();

    if (failures == 0) {
        std::printf("fast_palette: all checks passed\n");
        return 0;
    }
    std::printf("fast_palette: %d check(s) failed\n", failures);
    return 1;
}
