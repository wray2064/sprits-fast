// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

// palette_tools_tests.cpp — arranging and building palettes.
//
// The promises: sorting and dragging reorder the swatches without changing a
// compiled pixel; a ramp between two slots is evenly spaced and sits between
// them; adjusting a palette's hue recolours what is painted through it and
// dragging back to zero restores it exactly; every literal colour can become
// a slot, after which the palette recolours the whole sprite; .pal and .act
// round-trip; a palette reads off an image; and loading a file shows it in the
// file's order.

#include "app/document.h"
#include "app/element.h"
#include "app/image_io.h"
#include "app/ink.h"
#include "app/paint.h"
#include "app/palette.h"
#include "app/palette_io.h"
#include "app/palette_tools.h"

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

bool same(ls::Color a, ls::Color b) {
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

ls::RasterBuffer picture(Document& doc) {
    auto compiled = doc.engine().compileSprite(
        doc.sprite(), compileProfile(ls::CompileProfileType::Export, 8, 8));
    return compiled.ok() ? compiled.value.raster : ls::RasterBuffer{};
}

// A document whose palette is black, white, red, with a pixel through each.
struct Scene {
    Document      doc;
    PaintLayer    layer;
    ls::PaletteId palette;

    bool build() {
        if (!doc.create("palette", 8, 8) || !ensurePalette(doc, doc.sprite())) {
            return false;
        }
        palette = paletteFor(doc, doc.sprite());
        for (const PaletteEntry& entry : paletteEntries(doc, palette)) {
            removePaletteEntry(doc, palette, entry.role);
        }
        setPaletteEntry(doc, palette, 0, { 0, 0, 0, 255 });
        setPaletteEntry(doc, palette, 1, { 255, 255, 255, 255 });
        setPaletteEntry(doc, palette, 2, { 255, 0, 0, 255 });
        if (!createPaintLayer(doc, doc.sprite(), "Layer 1", { 0, 0, 0, 255 }, &layer)) {
            return false;
        }
        for (ls::ColorRole role : { 0u, 1u, 2u }) {
            Ink ink;
            ink.role = role;
            InkStroke stroke;
            doc.beginAction("Pencil");
            if (!beginInkStroke(doc, layer.layer, ink, &stroke) ||
                !strokeInk(doc, stroke, {{ static_cast<int32_t>(role), 0 }})) {
                return false;
            }
            doc.endAction();
        }
        return true;
    }

    std::vector<ls::ColorRole> order() {
        std::vector<ls::ColorRole> out;
        for (const PaletteEntry& entry : paletteEntries(doc, palette)) {
            out.push_back(entry.role);
        }
        return out;
    }
};

void testSortingChangesNoPixel() {
    Scene s;
    REQUIRE(s.build());
    const std::vector<uint8_t> before = picture(s.doc).pixels;
    REQUIRE(sortPalette(s.doc, s.palette, PaletteSort::Reverse));
    CHECK((s.order() == std::vector<ls::ColorRole>{ 2, 1, 0 }));
    REQUIRE(sortPalette(s.doc, s.palette, PaletteSort::Lightness));
    CHECK(s.order().front() == 0 && s.order().back() == 1);      // black ... white
    REQUIRE(moveSlot(s.doc, s.palette, 1, 0));
    CHECK(s.order().front() == 1);
    CHECK(picture(s.doc).pixels == before);
}

void testARampSitsBetweenItsEnds() {
    Scene s;
    REQUIRE(s.build());
    const std::vector<ls::ColorRole> made = addRampBetween(s.doc, s.palette, 0, 1, 3);
    REQUIRE(made.size() == 3);
    const std::vector<PaletteEntry> entries = paletteEntries(s.doc, s.palette);
    REQUIRE(entries.size() == 6);
    // Black, three greys lightening, white, then red where it was.
    CHECK(entries[0].role == 0 && entries[4].role == 1 && entries[5].role == 2);
    CHECK(entries[1].color.r == 64 && entries[2].color.r == 128 && entries[3].color.r == 191);
    // New roles are past every role in use, not reused ones.
    for (ls::ColorRole role : made) {
        CHECK(role > 2);
    }
}

void testAdjustingIsAPaletteEditAndReturnsExactly() {
    Scene s;
    REQUIRE(s.build());
    const std::vector<PaletteEntry> base = paletteEntries(s.doc, s.palette);
    const std::vector<uint8_t> before = picture(s.doc).pixels;
    REQUIRE(adjustPalette(s.doc, s.palette, base, 120.f, 0.f, 0.f));
    const ls::Color red = ls::readPixel(picture(s.doc), 2, 0);
    CHECK(red.g == 255 && red.r == 0);                  // red turned a third: green
    REQUIRE(adjustPalette(s.doc, s.palette, base, 0.f, 0.f, 0.f));
    CHECK(picture(s.doc).pixels == before);
    REQUIRE(adjustPalette(s.doc, s.palette, base, 0.f, 0.f, 0.5f));
    CHECK(ls::readPixel(picture(s.doc), 0, 0).r == 128);    // black halfway to white
}

void testLiteralColoursBecomeSlots() {
    Document doc;
    REQUIRE(doc.create("literal", 8, 8) && ensurePalette(doc, doc.sprite()));
    const ls::PaletteId palette = paletteFor(doc, doc.sprite());
    PaletteEntry existing = paletteEntries(doc, palette).front();
    PaintLayer layer;
    REQUIRE(createPaintLayer(doc, doc.sprite(), "Layer 1", { 1, 2, 3, 255 }, &layer));
    for (ls::Color colour : { existing.color, ls::Color{ 12, 200, 34, 255 } }) {
        Ink ink;
        ink.colour = colour;
        InkStroke stroke;
        doc.beginAction("Pencil");
        REQUIRE(beginInkStroke(doc, layer.layer, ink, &stroke));
        REQUIRE(strokeInk(doc, stroke, {{ colour.g == 200 ? 1 : 0, 0 }}));
        doc.endAction();
    }
    const size_t slotsBefore = paletteEntries(doc, palette).size();
    const int converted = slotsFromColours(doc, palette);
    CHECK(converted >= 2);
    // The colour already in the palette reused its slot; the new one got one.
    CHECK(paletteEntries(doc, palette).size() == slotsBefore + 1);
    Ink ink;
    ink.colour = { 12, 200, 34, 255 };
    CHECK(elementsWithInk(doc, layer.layer, ink).empty());      // no literal left
}

void testFormatsRoundTrip() {
    std::vector<PaletteEntry> entries = {
        { 0, { 1, 2, 3, 255 }, "" }, { 1, { 250, 128, 7, 255 }, "" }, { 2, { 0, 90, 255, 255 }, "" } };
    PaletteFile read;
    std::string error;
    REQUIRE(parsePalette(toJasc(entries), &read, &error));
    REQUIRE(read.entries.size() == 3);
    CHECK(same(read.entries[1].color, entries[1].color));

    const std::vector<uint8_t> act = toAct(entries);
    CHECK(act.size() == 772);
    REQUIRE(parseAct(act, &read, &error));
    REQUIRE(read.entries.size() == 3);
    CHECK(same(read.entries[2].color, entries[2].color));
    CHECK(!parseAct(std::vector<uint8_t>(10, 0), &read, &error));
    // 768 bytes and no count: the whole table.
    std::vector<uint8_t> bare(act.begin(), act.begin() + 768);
    REQUIRE(parseAct(bare, &read, &error));
    CHECK(read.entries.size() == 256);
}

void testAPaletteReadsOffAnImage() {
    ls::RasterBuffer image = ls::makeRaster(3, 1);
    const ls::Color a { 10, 20, 30, 255 };
    const ls::Color b { 40, 50, 60, 255 };
    ls::writePixel(image, 0, 0, a);
    ls::writePixel(image, 1, 0, b);
    ls::writePixel(image, 2, 0, a);
    std::vector<uint8_t> png;
    std::string error;
    REQUIRE(encodeImageAsPng(image, &png, &error));
    PaletteFile read;
    REQUIRE(paletteFromImage(png, &read, &error));
    REQUIRE(read.entries.size() == 2);
    CHECK(same(read.entries[0].color, a) && same(read.entries[1].color, b));
}

void testLoadingShowsTheFilesOrder() {
    Scene s;
    REQUIRE(s.build());
    REQUIRE(sortPalette(s.doc, s.palette, PaletteSort::Reverse));
    PaletteFile file;
    std::string error;
    REQUIRE(parsePalette("ff0000\n00ff00\n0000ff\n", &file, &error));
    int dropped = 0;
    REQUIRE(applyPaletteFile(s.doc, s.doc.sprite(), file, &dropped));
    CHECK((s.order() == std::vector<ls::ColorRole>{ 0, 1, 2 }));
}

void testPresetsAreWellFormed() {
    const std::vector<PalettePreset>& presets = palettePresets();
    CHECK(presets.size() >= 5);
    for (const PalettePreset& preset : presets) {
        CHECK(!preset.name.empty());
        CHECK(preset.colours.size() >= 2 && preset.colours.size() <= kMaxPaletteEntries);
    }
}

void testHslRoundTrips() {
    for (int r = 0; r < 256; r += 51) {
        for (int g = 0; g < 256; g += 51) {
            for (int b = 0; b < 256; b += 51) {
                const ls::Color c { static_cast<uint8_t>(r), static_cast<uint8_t>(g),
                                    static_cast<uint8_t>(b), 255 };
                float h, s, l;
                rgbToHsl(c, &h, &s, &l);
                CHECK(same(hslToRgb(h, s, l, 255), c));
            }
        }
    }
}

} // namespace

int main() {
    testSortingChangesNoPixel();
    testARampSitsBetweenItsEnds();
    testAdjustingIsAPaletteEditAndReturnsExactly();
    testLiteralColoursBecomeSlots();
    testFormatsRoundTrip();
    testAPaletteReadsOffAnImage();
    testLoadingShowsTheFilesOrder();
    testPresetsAreWellFormed();
    testHslRoundTrips();
    if (failures == 0) {
        std::printf("palette tools: all passed\n");
        return 0;
    }
    std::printf("palette tools: %d failure(s)\n", failures);
    return 1;
}
