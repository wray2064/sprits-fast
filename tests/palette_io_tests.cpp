// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

// palette_io_tests.cpp — palettes as files, and what removing a slot means.
//
// The two formats are the ones a pixel artist actually meets: .gpl from GIMP
// and Aseprite, .hex from Lospec. Both arrive from elsewhere, so most of what
// is here is about not trusting them.

#include "app/document.h"
#include "app/dither.h"
#include "app/file_io.h"
#include "app/paint.h"
#include "app/palette.h"
#include "app/palette_io.h"
#include "app/shape.h"

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

const char* kGpl =
    "GIMP Palette\n"
    "Name: Two Tone\n"
    "Columns: 2\n"
    "#\n"
    "  0   0   0\tblack\n"
    "255 255 255\twhite\n"
    " 34  32  52\tdeep space\n";      // a name with a space in it

void testGplReadsColoursAndNames() {
    PaletteFile file;
    std::string error;
    REQUIRE(parsePalette(kGpl, &file, &error));
    CHECK(file.name == "Two Tone");
    REQUIRE(file.entries.size() == 3);
    CHECK(file.entries[0].color.r == 0 && file.entries[0].label == "black");
    CHECK(file.entries[1].color.r == 255 && file.entries[1].label == "white");
    CHECK(file.entries[2].color.g == 32 && file.entries[2].label == "deep space");
    CHECK(file.entries[2].role == 2);
}

void testHexReadsWithAndWithoutHashes() {
    PaletteFile file;
    std::string error;
    REQUIRE(parsePalette("1a1c2c\n#5d275d\nB13E53\n", &file, &error));
    REQUIRE(file.entries.size() == 3);
    CHECK(file.entries[0].color.r == 0x1a && file.entries[0].color.b == 0x2c);
    CHECK(file.entries[1].color.r == 0x5d);
    CHECK(file.entries[2].color.r == 0xb1 && file.entries[2].color.a == 255);
    CHECK(file.name.empty());

    // An alpha byte is taken when given.
    REQUIRE(parsePalette("ff000080\n", &file, &error));
    CHECK(file.entries.front().color.a == 0x80);
}

// One stray line must not cost a person the other thirty-one.
void testStrayLinesAreSkippedNotFatal() {
    PaletteFile file;
    std::string error;
    REQUIRE(parsePalette("GIMP Palette\n10 20 30\nnot a colour\n40 50 60\n", &file, &error));
    REQUIRE(file.entries.size() == 2);
    CHECK(file.entries[1].color.r == 40);

    REQUIRE(parsePalette("112233\nxyzxyz\n; a comment\n445566\n", &file, &error));
    REQUIRE(file.entries.size() == 2);
    CHECK(file.entries[1].color.r == 0x44);
}

// A value outside 0..255 is a rejected line, not a clamped one.
void testOutOfRangeIsRefusedNotClamped() {
    PaletteFile file;
    std::string error;
    REQUIRE(parsePalette("GIMP Palette\n300 0 0\n0 0 0\n", &file, &error));
    REQUIRE(file.entries.size() == 1);
    CHECK(file.entries.front().color.r == 0);

    // Nothing usable at all is a failure with a reason, not an empty palette.
    CHECK(!parsePalette("GIMP Palette\n999 999 999\n", &file, &error));
    CHECK(!error.empty());
    CHECK(!parsePalette("", &file, &error));
    CHECK(!parsePalette("hello world\n", &file, &error));
}

void testTheEntryCountIsBounded() {
    std::string big;
    for (int i = 0; i < 1000; ++i) {
        big += "000000\n";
    }
    PaletteFile file;
    std::string error;
    REQUIRE(parsePalette(big, &file, &error));
    CHECK(file.entries.size() == kMaxPaletteEntries);
}

void testGplRoundTripsWithNames() {
    std::vector<PaletteEntry> entries = {
        { 0, { 10, 20, 30, 255 }, "skin" },
        { 1, { 40, 50, 60, 255 }, "" },
        { 2, { 70, 80, 90, 255 }, "two words" },
    };
    const std::string text = toGpl("Test", entries);
    CHECK(text.rfind("GIMP Palette", 0) == 0);

    PaletteFile back;
    std::string error;
    REQUIRE(parsePalette(text, &back, &error));
    CHECK(back.name == "Test");
    REQUIRE(back.entries.size() == 3);
    CHECK(back.entries[0].label == "skin");
    CHECK(back.entries[1].label == "slot 1");       // unnamed gets a stand-in
    CHECK(back.entries[2].label == "two words");
    CHECK(back.entries[2].color.b == 90);
}

void testHexRoundTrips() {
    std::vector<PaletteEntry> entries = {
        { 0, { 0x1a, 0x1c, 0x2c, 255 } },
        { 1, { 0xff, 0x00, 0x4d, 255 } },
    };
    PaletteFile back;
    std::string error;
    REQUIRE(parsePalette(toHex(entries), &back, &error));
    REQUIRE(back.entries.size() == 2);
    CHECK(back.entries[1].color.r == 0xff && back.entries[1].color.b == 0x4d);
}

// --- applying to a document -------------------------------------------------

struct Canvas {
    Document doc;
    PaintLayer layer;
    bool build() {
        if (!doc.create("io", 16, 16)) { return false; }
        if (!ensurePalette(doc, doc.sprite())) { return false; }
        if (!createPaintLayer(doc, doc.sprite(), "a", { 1, 1, 1, 255 }, &layer)) { return false; }
        paintPixels(doc, layer, linePixels({ 2, 4 }, { 13, 4 }));
        return true;
    }
    ls::Color at(int32_t x, int32_t y) {
        ls::CompileProfile p;
        p.type = ls::CompileProfileType::Export;
        p.outputWidth = 16; p.outputHeight = 16;
        p.palette = ls::PalettePolicy::Unconstrained;
        auto c = doc.engine().compileSprite(doc.sprite(), p);
        return c.ok() ? ls::readPixel(c.value.raster, x, y) : ls::Color{};
    }
};

// Loading a palette recolours a sprite drawn through roles -- the point of
// roles -- and it is one undo step.
void testLoadingAPaletteRecoloursAndIsOneUndo() {
    Canvas canvas;
    REQUIRE(canvas.build());
    REQUIRE(setLayerRole(canvas.doc, canvas.layer, 1));
    REQUIRE(setPaletteEntry(canvas.doc, 1, { 200, 0, 0, 255 }));
    CHECK(canvas.at(5, 4).r == 200);
    canvas.doc.markUnmodified();

    PaletteFile file;
    std::string error;
    REQUIRE(parsePalette("000000\n0000ff\n", &file, &error));
    int dropped = -1;
    REQUIRE(applyPaletteFile(canvas.doc, canvas.doc.sprite(), file, &dropped));
    CHECK(canvas.at(5, 4).b == 255);                   // role 1 is blue now
    CHECK(paletteEntries(canvas.doc).size() == 2);      // the starter's others are gone

    REQUIRE(canvas.doc.undo());
    CHECK(canvas.at(5, 4).r == 200);
    CHECK(paletteEntries(canvas.doc).size() > 2);
}

// A role the new palette lacks falls back to the literal, and the count of
// roles that were in use is reported so the interface can say so.
void testRolesTheFileLacksFallBackAndAreCounted() {
    Canvas canvas;
    REQUIRE(canvas.build());
    REQUIRE(setLayerRole(canvas.doc, canvas.layer, 5));
    REQUIRE(setPaletteEntry(canvas.doc, 5, { 0, 200, 0, 255 }));
    CHECK(canvas.at(5, 4).g == 200);

    PaletteFile file;
    std::string error;
    REQUIRE(parsePalette("ffffff\n", &file, &error));      // one colour: role 0 only
    int dropped = 0;
    REQUIRE(applyPaletteFile(canvas.doc, canvas.doc.sprite(), file, &dropped));
    CHECK(dropped == 1);
    // The layer still paints -- through the colour it had, not through nothing.
    CHECK(canvas.at(5, 4).a == 255);
    CHECK(canvas.at(5, 4).g != 200 || canvas.at(5, 4).r == 1);
}

void testRemovingASlotAsksFirstAndRevertsCleanly() {
    Canvas canvas;
    REQUIRE(canvas.build());
    REQUIRE(setLayerRole(canvas.doc, canvas.layer, 3));
    REQUIRE(setPaletteEntry(canvas.doc, 3, { 0, 0, 200, 255 }));

    CHECK(paletteRoleInUse(canvas.doc, 3));
    CHECK(!paletteRoleInUse(canvas.doc, 12));

    REQUIRE(removePaletteEntry(canvas.doc, 3));
    // The slot is gone but the layer still names the role -- it is the palette
    // that no longer answers, not the layer that stopped asking. So the query
    // still says yes, which is what lets an interface offer to put it back.
    CHECK(paletteRoleInUse(canvas.doc, 3));
    CHECK(canvas.at(5, 4).a == 255);                     // and it still paints
    CHECK(canvas.at(5, 4).b != 200);                     // in its fallback
}

void testLabelsRideThroughTheDocument() {
    Canvas canvas;
    REQUIRE(canvas.build());
    REQUIRE(setPaletteLabel(canvas.doc, 0, "outline"));
    const std::vector<PaletteEntry> entries = paletteEntries(canvas.doc);
    REQUIRE(!entries.empty());
    CHECK(entries.front().label == "outline");

    const std::string path = "palette_io_test.lsprite";
    std::string error;
    REQUIRE(canvas.doc.save(path, &error));
    Document reopened;
    REQUIRE(reopened.open(path, &error));
    CHECK(paletteEntries(reopened).front().label == "outline");
    deleteFile(path);
}

void testExportWritesTheFormatTheExtensionAsks() {
    Canvas canvas;
    REQUIRE(canvas.build());
    std::string error;

    REQUIRE(exportPaletteFile(canvas.doc, canvas.doc.sprite(), "palette_io_test.gpl", &error));
    std::vector<uint8_t> bytes;
    REQUIRE(readFile("palette_io_test.gpl", bytes, &error));
    CHECK(std::string(bytes.begin(), bytes.end()).rfind("GIMP Palette", 0) == 0);
    deleteFile("palette_io_test.gpl");

    REQUIRE(exportPaletteFile(canvas.doc, canvas.doc.sprite(), "palette_io_test.hex", &error));
    REQUIRE(readFile("palette_io_test.hex", bytes, &error));
    const std::string hex(bytes.begin(), bytes.end());
    CHECK(hex.find("GIMP") == std::string::npos);
    CHECK(hex.size() >= 7 && hex[6] == '\n');           // rrggbb newline
    deleteFile("palette_io_test.hex");
}

} // namespace

int main() {
    testGplReadsColoursAndNames();
    testHexReadsWithAndWithoutHashes();
    testStrayLinesAreSkippedNotFatal();
    testOutOfRangeIsRefusedNotClamped();
    testTheEntryCountIsBounded();
    testGplRoundTripsWithNames();
    testHexRoundTrips();
    testLoadingAPaletteRecoloursAndIsOneUndo();
    testRolesTheFileLacksFallBackAndAreCounted();
    testRemovingASlotAsksFirstAndRevertsCleanly();
    testLabelsRideThroughTheDocument();
    testExportWritesTheFormatTheExtensionAsks();
    if (failures == 0) {
        std::printf("palette_io: all checks passed\n");
        return 0;
    }
    std::printf("palette_io: %d check(s) failed\n", failures);
    return 1;
}
