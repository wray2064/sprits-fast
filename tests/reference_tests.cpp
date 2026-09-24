// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

// reference_tests.cpp — the picture you draw from, and the folders beside it.
//
// A reference travels in the document, changes no pixel of the artwork, and
// undoes like anything else. A thumbnail can be read without opening the file.
// A library is a folder, listed.

#include "app/document.h"
#include "app/export_png.h"
#include "app/file_io.h"
#include "app/image_io.h"
#include "app/library.h"
#include "app/paint.h"
#include "app/reference.h"

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

constexpr uint32_t kCanvas = 16;

// A real PNG, made the way Fast makes them, so the decoder is fed something a
// person could actually have on disk rather than a fixture with a fixed blob.
std::vector<uint8_t> makePng(uint32_t width, uint32_t height, ls::Color colour) {
    ls::RasterBuffer raster = ls::makeRaster(width, height);
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            uint8_t* pixel = raster.row(y) + static_cast<size_t>(x) * 4u;
            pixel[0] = colour.r;
            pixel[1] = colour.g;
            pixel[2] = colour.b;
            pixel[3] = colour.a;
        }
    }
    std::vector<uint8_t> png;
    std::string error;
    encodeImageAsPng(raster, &png, &error);
    return png;
}

// CRC32 as PNG uses it, so a header can be rewritten and still be a valid
// file -- see the hostile-size check below.
uint32_t crc32Of(const uint8_t* data, size_t length) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ (0xEDB88320u & (~(crc & 1u) + 1u));
        }
    }
    return ~crc;
}

uint64_t hashOf(Document& doc) {
    ls::CompileProfile profile;
    profile.type = ls::CompileProfileType::Export;
    profile.outputWidth = kCanvas;
    profile.outputHeight = kCanvas;
    profile.palette = ls::PalettePolicy::Unconstrained;
    auto compiled = doc.engine().compileSprite(doc.sprite(), profile);
    uint64_t h = 1469598103934665603ull;
    if (compiled.ok()) {
        for (uint8_t byte : compiled.value.raster.pixels) {
            h ^= byte;
            h *= 1099511628211ull;
        }
    }
    return h;
}

bool build(Document& doc) {
    if (!doc.create("refs", kCanvas, kCanvas)) { return false; }
    PaintLayer layer;
    if (!createPaintLayer(doc, doc.sprite(), "Body", ls::Color{ 200, 90, 60, 255 }, &layer)) {
        return false;
    }
    doc.beginAction("draw");
    const bool ok = paintPixels(doc, layer, {{ 4, 4 }, { 5, 4 }, { 4, 5 }});
    doc.endAction();
    return ok;
}

// --- reading somebody else's image ----------------------------------------

void testTheDecoderIsBounded() {
    std::string error;
    ImageInfo info;
    ls::RasterBuffer raster;

    CHECK(!imageInfo({}, &info, &error) && !error.empty());
    error.clear();
    CHECK(!decodeImage({ 'n', 'o', 't', ' ', 'a', 'n', ' ', 'i', 'm', 'a', 'g', 'e' },
                       &raster, &error));
    CHECK(!error.empty());

    // A PNG whose header honestly claims 16000 x 16000 -- 256 megapixels, a
    // 1 GB decode -- is refused from the header alone, before a row is read.
    // The CRC is recomputed so the file is well formed: the point is that a
    // *valid* file is refused on size, not that a corrupt one fails to parse.
    std::vector<uint8_t> hostile = makePng(4, 4, ls::Color{ 1, 2, 3, 255 });
    REQUIRE(hostile.size() > 33);
    const uint8_t huge[8] = { 0, 0, 0x3E, 0x80, 0, 0, 0x3E, 0x80 };   // 16000, 16000
    for (size_t i = 0; i < 8; ++i) {
        hostile[16 + i] = huge[i];
    }
    // CRC32 over the chunk type and data: bytes 12..28, stored at 29..32.
    const uint32_t crc = crc32Of(&hostile[12], 17);
    hostile[29] = static_cast<uint8_t>(crc >> 24);
    hostile[30] = static_cast<uint8_t>(crc >> 16);
    hostile[31] = static_cast<uint8_t>(crc >> 8);
    hostile[32] = static_cast<uint8_t>(crc);
    error.clear();
    CHECK(!imageInfo(hostile, &info, &error));
    CHECK(error.find("larger") != std::string::npos);
    error.clear();
    CHECK(!decodeImage(hostile, &raster, &error));

    // A real one reads, and round-trips its size.
    const std::vector<uint8_t> good = makePng(7, 3, ls::Color{ 10, 20, 30, 255 });
    REQUIRE(imageInfo(good, &info, &error));
    CHECK(info.width == 7 && info.height == 3);
    REQUIRE(decodeImage(good, &raster, &error));
    CHECK(raster.width == 7 && raster.height == 3);
    CHECK(ls::readPixel(raster, 3, 1).g == 20);
}

void testDownscaleKeepsWholePixels() {
    ls::RasterBuffer raster = ls::makeRaster(8, 4);
    for (uint32_t x = 0; x < 8; ++x) {
        uint8_t* pixel = raster.row(0) + static_cast<size_t>(x) * 4u;
        pixel[0] = static_cast<uint8_t>(x * 30);
        pixel[3] = 255;
    }
    const ls::RasterBuffer small = downscaleNearest(raster, 4);
    CHECK(small.width == 4 && small.height == 2);
    // Nearest: every value came from a pixel, none is an average of two.
    for (uint32_t x = 0; x < small.width; ++x) {
        const uint8_t value = ls::readPixel(small, static_cast<int32_t>(x), 0).r;
        CHECK(value % 30 == 0);
    }
    // Already small enough is handed back untouched.
    CHECK(downscaleNearest(raster, 64).width == 8);
}

// --- a reference in the document ------------------------------------------

void testAReferenceTravelsAndChangesNoPixel() {
    Document doc;
    REQUIRE(build(doc));
    const uint64_t artwork = hashOf(doc);
    CHECK(readReferences(doc).empty());

    Reference reference;
    std::string error;
    REQUIRE(addReference(doc, "pose.png", makePng(32, 24, ls::Color{ 9, 9, 9, 255 }),
                         &reference, &error));
    CHECK(reference.name == "pose");
    CHECK(reference.width == 32 && reference.height == 24);
    CHECK(referenceBytes(doc, reference) != nullptr);

    // Centred and fitted: a 32x24 image on a 16x16 canvas is scaled to fit.
    CHECK(reference.scale < 1.f);
    CHECK(reference.x >= 0.f && reference.y >= 0.f);

    // The artwork is untouched. That is the whole promise of a reference.
    CHECK(hashOf(doc) == artwork);

    const std::vector<Reference> listed = readReferences(doc);
    REQUIRE(listed.size() == 1);
    CHECK(listed[0].id == reference.id && listed[0].name == "pose");

    // Placement is editable, and nothing else moves with it.
    Reference moved = listed[0];
    moved.x = 3.5f;
    moved.opacity = 0.25f;
    moved.behind = false;
    REQUIRE(updateReference(doc, moved));
    const std::vector<Reference> after = readReferences(doc);
    REQUIRE(after.size() == 1);
    CHECK(after[0].x == 3.5f && after[0].opacity == 0.25f && !after[0].behind);
    CHECK(hashOf(doc) == artwork);

    // It survives a save, image and placement together.
    std::string path = "fast_reference_test.lsprite";
    REQUIRE(doc.save(path, &error));
    Document again;
    REQUIRE(again.open(path, &error));
    deleteFile(path);
    const std::vector<Reference> reopened = readReferences(again);
    REQUIRE(reopened.size() == 1);
    CHECK(reopened[0].name == "pose" && reopened[0].x == 3.5f);
    CHECK(!reopened[0].behind && reopened[0].opacity == 0.25f);
    const std::vector<uint8_t>* bytes = referenceBytes(again, reopened[0]);
    REQUIRE(bytes != nullptr);
    ls::RasterBuffer picture;
    REQUIRE(decodeImage(*bytes, &picture, &error));
    CHECK(picture.width == 32 && picture.height == 24);
}

// The bit that needed the history to carry Fast's own package entries: the
// engine's snapshot knows nothing about them, so without that an undone
// import left the image behind.
void testImportingAReferenceUndoes() {
    Document doc;
    REQUIRE(build(doc));
    Reference reference;
    std::string error;
    REQUIRE(addReference(doc, "a.png", makePng(8, 8, ls::Color{ 1, 1, 1, 255 }),
                         &reference, &error));
    CHECK(readReferences(doc).size() == 1);

    REQUIRE(doc.undo());
    CHECK(readReferences(doc).empty());
    CHECK(doc.companion(reference.entryName()) == nullptr);

    REQUIRE(doc.redo());
    CHECK(readReferences(doc).size() == 1);
    CHECK(doc.companion(reference.entryName()) != nullptr);

    // Removing is one step too.
    REQUIRE(removeReference(doc, readReferences(doc)[0]));
    CHECK(readReferences(doc).empty());
    REQUIRE(doc.undo());
    CHECK(readReferences(doc).size() == 1);
}

void testTheLimitsHold() {
    Document doc;
    REQUIRE(build(doc));
    std::string error;

    // Sixteen is the ceiling, and the seventeenth is refused with a reason.
    for (size_t i = 0; i < kMaxReferences; ++i) {
        Reference made;
        REQUIRE(addReference(doc, "r" + std::to_string(i) + ".png",
                             makePng(4, 4, ls::Color{ 2, 2, 2, 255 }), &made, &error));
    }
    Reference extra;
    error.clear();
    CHECK(!addReference(doc, "one-too-many.png", makePng(4, 4, ls::Color{ 3, 3, 3, 255 }),
                        &extra, &error));
    CHECK(!error.empty());
    CHECK(readReferences(doc).size() == kMaxReferences);

    // Not an image at all is refused before anything is stored.
    Document other;
    REQUIRE(build(other));
    Reference bad;
    error.clear();
    CHECK(!addReference(other, "notes.txt", { 'h', 'e', 'l', 'l', 'o' }, &bad, &error));
    CHECK(other.companionNames().empty());
}

void testAHostileListIsNotTrusted() {
    std::vector<Reference> out;
    // Not ours at all.
    CHECK(!decodeReferences("something else entirely", &out));
    // Ours, with a line naming a path rather than an id: refused, so nothing
    // can name an entry outside "fast/ref/".
    CHECK(decodeReferences("lsfast-references 1\n../../etc/passwd|x|4|4|0|0|1|1|111\n", &out));
    CHECK(out.empty());
    // Ours, with absurd numbers: clamped, not refused, because losing the
    // picture over a silly position would be worse than moving it.
    CHECK(decodeReferences("lsfast-references 1\n1|p|4|4|1e30|-1e30|900|7|111\n", &out));
    REQUIRE(out.size() == 1);
    CHECK(out[0].x <= 100000.f && out[0].y >= -100000.f);
    CHECK(out[0].scale <= 64.f && out[0].opacity <= 1.f);
    // A round trip of a real list keeps every field.
    std::vector<Reference> written;
    Reference one;
    one.id = "3"; one.name = "a|b\nc"; one.width = 5; one.height = 6;
    one.x = -2.5f; one.y = 7.25f; one.scale = 2.f; one.opacity = 0.75f;
    one.visible = false; one.behind = false; one.locked = true;
    written.push_back(one);
    REQUIRE(decodeReferences(encodeReferences(written), &out));
    REQUIRE(out.size() == 1);
    CHECK(out[0].name == "a|b\nc");          // the separators survive the escape
    CHECK(out[0].x == -2.5f && out[0].scale == 2.f);
    CHECK(!out[0].visible && !out[0].behind && out[0].locked);
}

// --- the libraries ---------------------------------------------------------

void testAThumbnailIsReadWithoutOpeningTheDocument() {
    Document doc;
    REQUIRE(build(doc));
    CHECK(!readThumbnail("no-such-file.lsprite", nullptr));

    std::string error;
    const std::string path = "fast_library_test.lsprite";
    REQUIRE(updateThumbnail(doc));
    REQUIRE(doc.save(path, &error));

    std::vector<uint8_t> png;
    REQUIRE(readThumbnail(path, &png));
    ls::RasterBuffer picture;
    REQUIRE(decodeImage(png, &picture, &error));
    CHECK(picture.width == kCanvas && picture.height == kCanvas);   // already small
    CHECK(ls::readPixel(picture, 4, 4).r == 200);                   // what was drawn

    // A document saved without one lists with no thumbnail rather than failing.
    Document plain;
    REQUIRE(build(plain));
    const std::string plainPath = "fast_library_plain.lsprite";
    REQUIRE(plain.save(plainPath, &error));
    std::vector<uint8_t> none;
    CHECK(!readThumbnail(plainPath, &none));

    // And the listing finds both.
    const std::vector<LibraryDocument> found = listDocuments(".");
    bool sawOne = false;
    for (const LibraryDocument& document : found) {
        sawOne = sawOne || document.name == "fast_library_test";
        CHECK(hasExtension(document.path, ".lsprite"));
    }
    CHECK(sawOne);

    deleteFile(path);
    deleteFile(plainPath);
}

void testFolderHelpers() {
    CHECK(joinPath("a", "b") == "a/b" || joinPath("a", "b") == "a\\b");
    CHECK(joinPath("", "b") == "b");
    CHECK(joinPath("a/", "b") == "a/b");
    CHECK(parentDirectory("a/b/c") == "a/b");
    CHECK(parentDirectory("c").empty());
    CHECK(parentDirectory("a/b/") == "a");
    CHECK(looksLikeImageName("pose.PNG") && looksLikeImageName("a.jpeg"));
    CHECK(!looksLikeImageName("a.lsprite") && !looksLikeImageName("png"));
    CHECK(listDirectory("no-such-folder-here").empty());
    CHECK(!directoryExists("no-such-folder-here"));
}

} // namespace

int main() {
    testTheDecoderIsBounded();
    testDownscaleKeepsWholePixels();
    testAReferenceTravelsAndChangesNoPixel();
    testImportingAReferenceUndoes();
    testTheLimitsHold();
    testAHostileListIsNotTrusted();
    testAThumbnailIsReadWithoutOpeningTheDocument();
    testFolderHelpers();

    if (failures == 0) {
        std::printf("fast_reference: all checks passed\n");
        return 0;
    }
    std::printf("fast_reference: %d check(s) failed\n", failures);
    return 1;
}
