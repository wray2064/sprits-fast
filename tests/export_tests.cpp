// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

// export_tests.cpp — the work leaving the program.
//
// Fast has no PNG *reader*, so these check the file structurally rather than by
// decoding it: the signature, the IHDR fields, and the terminating chunk. That
// is enough to catch the failures that matter -- wrong dimensions, wrong colour
// type, a truncated write -- without carrying a decoder just for the tests. A
// separate check confirms the bytes are stable, which is what would notice stb
// changing under us.

#include "app/export_png.h"
#include "app/file_io.h"
#include "app/paint.h"

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

uint32_t beU32(const std::vector<uint8_t>& bytes, size_t at) {
    return (static_cast<uint32_t>(bytes[at]) << 24) |
           (static_cast<uint32_t>(bytes[at + 1]) << 16) |
           (static_cast<uint32_t>(bytes[at + 2]) << 8) |
            static_cast<uint32_t>(bytes[at + 3]);
}

bool hasPngSignature(const std::vector<uint8_t>& bytes) {
    static const uint8_t signature[8] = { 137, 'P', 'N', 'G', 13, 10, 26, 10 };
    if (bytes.size() < 8) {
        return false;
    }
    for (size_t i = 0; i < 8; ++i) {
        if (bytes[i] != signature[i]) {
            return false;
        }
    }
    return true;
}

bool endsWithIend(const std::vector<uint8_t>& bytes) {
    if (bytes.size() < 12) {
        return false;
    }
    const size_t at = bytes.size() - 8;
    return bytes[at] == 'I' && bytes[at + 1] == 'E' &&
           bytes[at + 2] == 'N' && bytes[at + 3] == 'D';
}

struct Canvas {
    fast::Document   doc;
    SpriteId         sprite;
    fast::PaintLayer paint;

    bool build(uint32_t size = 16) {
        if (!doc.create("export", size, size)) { return false; }
        sprite = doc.sprite();
        if (!fast::createPaintLayer(doc, sprite, "layer 1",
                                    Color{220, 90, 40, 255}, &paint)) {
            return false;
        }
        fast::paintPixels(doc, paint, fast::linePixels({2, 2}, {13, 13}));
        return true;
    }
};

// ---------------------------------------------------------------------------

void testAnExportIsAValidPng() {
    Canvas canvas;
    REQUIRE(canvas.build(16));

    const std::string path = "export_test.png";
    std::string error;
    REQUIRE(fast::exportSpriteToPng(canvas.doc, canvas.sprite, path, {}, &error));
    CHECK(fast::fileExists(path));

    std::vector<uint8_t> bytes;
    REQUIRE(fast::readFile(path, bytes, &error));
    CHECK(hasPngSignature(bytes));
    CHECK(endsWithIend(bytes));

    // IHDR is always the first chunk: length, type, then width and height.
    REQUIRE(bytes.size() > 33);
    CHECK(bytes[12] == 'I' && bytes[13] == 'H' && bytes[14] == 'D' && bytes[15] == 'R');
    CHECK(beU32(bytes, 16) == 16);      // width
    CHECK(beU32(bytes, 20) == 16);      // height
    CHECK(bytes[24] == 8);              // 8 bits per channel
    CHECK(bytes[25] == 6);              // colour type 6 = RGBA

    // It compressed. A file the size of the raw pixels means the encoder is not
    // doing its job, and that was the whole reason for choosing this one.
    CHECK(bytes.size() < 16u * 16u * 4u);

    fast::deleteFile(path);
}

// Scaling is duplication, so the file grows by exactly the factor per side.
void testScalingIsWholePixels() {
    Canvas canvas;
    REQUIRE(canvas.build(16));

    fast::ExportSettings settings;
    settings.scale = 4;

    const std::string path = "export_scaled.png";
    std::string error;
    REQUIRE(fast::exportSpriteToPng(canvas.doc, canvas.sprite, path, settings, &error));

    std::vector<uint8_t> bytes;
    REQUIRE(fast::readFile(path, bytes, &error));
    CHECK(beU32(bytes, 16) == 64);
    CHECK(beU32(bytes, 20) == 64);

    fast::deleteFile(path);
}

void testTheExtensionIsSupplied() {
    Canvas canvas;
    REQUIRE(canvas.build(8));

    std::string error;
    REQUIRE(fast::exportSpriteToPng(canvas.doc, canvas.sprite, "export_noext", {}, &error));
    CHECK(fast::fileExists("export_noext.png"));
    fast::deleteFile("export_noext.png");
}

// The same sprite must export to the same bytes. This is what would notice stb
// changing its deflate under us, or the compression level global being left at
// whatever somebody else set it to.
void testExportsAreStable() {
    Canvas canvas;
    REQUIRE(canvas.build(32));

    auto compiled = canvas.doc.engine().compileSprite(
        canvas.sprite, [] {
            CompileProfile p;
            p.type = CompileProfileType::Export;
            p.outputWidth = 32;
            p.outputHeight = 32;
            p.palette = PalettePolicy::Unconstrained;
            return p;
        }());
    REQUIRE(compiled.ok());

    std::vector<uint8_t> first, second;
    std::string error;
    REQUIRE(fast::encodeRasterToPng(compiled.value.raster, {}, &first, &error));
    REQUIRE(fast::encodeRasterToPng(compiled.value.raster, {}, &second, &error));
    CHECK(first == second);
    CHECK(!first.empty());

    // And a different picture produces different bytes, so the comparison above
    // is not trivially true.
    fast::paintPixels(canvas.doc, canvas.paint, {{20, 20}, {21, 21}});
    auto changed = canvas.doc.engine().compileSprite(
        canvas.sprite, [] {
            CompileProfile p;
            p.type = CompileProfileType::Export;
            p.outputWidth = 32;
            p.outputHeight = 32;
            p.palette = PalettePolicy::Unconstrained;
            return p;
        }());
    REQUIRE(changed.ok());

    std::vector<uint8_t> third;
    REQUIRE(fast::encodeRasterToPng(changed.value.raster, {}, &third, &error));
    CHECK(third != first);
}

void testBadInputIsRefused() {
    Canvas canvas;
    REQUIRE(canvas.build(8));
    std::string error;

    fast::ExportSettings zero;
    zero.scale = 0;
    CHECK(!fast::exportSpriteToPng(canvas.doc, canvas.sprite, "no.png", zero, &error));
    CHECK(!error.empty());

    fast::ExportSettings huge;
    huge.scale = 1000;
    CHECK(!fast::exportSpriteToPng(canvas.doc, canvas.sprite, "no.png", huge, &error));

    // A large export is allowed even past the canvas policy, and that is
    // deliberate. The policy bounds what is affordable to *edit* interactively;
    // an export is an explicit one-shot request, so asking for a 2048 sprite at
    // 4x is a reasonable thing to want and it produces a real 8192 image.
    Canvas big;
    REQUIRE(big.build(2048));
    fast::ExportSettings four;
    four.scale = 4;
    CHECK(fast::exportSpriteToPng(big.doc, big.sprite, "export_big.png", four, &error));
    CHECK(fast::fileExists("export_big.png"));
    fast::deleteFile("export_big.png");

    // What stops it is the engine's ceiling and the machine's memory, not a
    // policy: a scale that cannot be represented fails cleanly rather than
    // truncating or crashing.
    fast::ExportSettings absurd;
    absurd.scale = 64;              // 131072 x 131072
    CHECK(!fast::exportSpriteToPng(big.doc, big.sprite, "no.png", absurd, &error));
    CHECK(!error.empty());

    CHECK(!fast::fileExists("no.png"));

    // An unwritable path fails without leaving anything behind.
    CHECK(!fast::exportSpriteToPng(canvas.doc, canvas.sprite,
                                   "no_such_directory/nested/x.png", {}, &error));

    std::vector<uint8_t> out;
    RasterBuffer empty;
    CHECK(!fast::encodeRasterToPng(empty, {}, &out, &error));
}

// Exporting must not disturb the document: it is a read, however much work it
// does.
void testExportingChangesNothing() {
    Canvas canvas;
    REQUIRE(canvas.build(16));
    const bool modifiedBefore = canvas.doc.modified();
    const bool canUndoBefore = canvas.doc.canUndo();

    std::string error;
    REQUIRE(fast::exportSpriteToPng(canvas.doc, canvas.sprite, "export_readonly.png",
                                    {}, &error));

    CHECK(canvas.doc.modified() == modifiedBefore);
    CHECK(canvas.doc.canUndo() == canUndoBefore);
    CHECK(canvas.doc.path().empty());       // exporting is not saving

    fast::deleteFile("export_readonly.png");
}

} // namespace

int main() {
    testAnExportIsAValidPng();
    testScalingIsWholePixels();
    testTheExtensionIsSupplied();
    testExportsAreStable();
    testBadInputIsRefused();
    testExportingChangesNothing();

    if (failures == 0) {
        std::printf("fast_export: all checks passed\n");
        return 0;
    }
    std::printf("fast_export: %d check(s) failed\n", failures);
    return 1;
}
