// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

// anim_export_tests.cpp — animations out as one file.
//
// The promises are checked by reading the files back with a decoder this
// project did not write: a GIF decodes to exactly the frames that went in,
// transparency included, through LZW streams long enough to fill and clear the
// code table; frames that between them have too many colours for one palette
// get one each and still decode exactly; a frame with too many for any palette
// is reduced and says so; an APNG is well-formed chunk by chunk and its
// default image is the first frame; ping-pong plays there and back without
// repeating the ends; and a document exports what its frames compile to.

#include "app/animation.h"
#include "app/document.h"
#include "app/export_anim.h"
#include "app/file_io.h"
#include "app/image_io.h"
#include "app/ink.h"
#include "app/paint.h"

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

// Equal as a GIF can hold them: transparent is transparent, the rest exact.
bool sameAsGif(const ls::RasterBuffer& a, const ls::RasterBuffer& b) {
    if (a.width != b.width || a.height != b.height) {
        return false;
    }
    for (uint32_t y = 0; y < a.height; ++y) {
        for (uint32_t x = 0; x < a.width; ++x) {
            const uint8_t* p = a.row(y) + static_cast<size_t>(x) * 4u;
            const uint8_t* q = b.row(y) + static_cast<size_t>(x) * 4u;
            if ((p[3] < 128) != (q[3] < 128)) {
                return false;
            }
            if (p[3] >= 128 && (p[0] != q[0] || p[1] != q[1] || p[2] != q[2])) {
                return false;
            }
        }
    }
    return true;
}

uint32_t lcg(uint32_t& state) {
    state = state * 1664525u + 1013904223u;
    return state >> 8;
}

ls::RasterBuffer noise(uint32_t size, uint32_t colours, uint32_t seed, uint8_t base) {
    ls::RasterBuffer raster = ls::makeRaster(size, size);
    uint32_t state = seed;
    for (uint32_t y = 0; y < size; ++y) {
        for (uint32_t x = 0; x < size; ++x) {
            const uint32_t pick = lcg(state) % (colours + 1);
            if (pick == colours) {
                continue;                              // a transparent pixel
            }
            put(raster, x, y, ls::Color{ static_cast<uint8_t>(pick), base,
                                         static_cast<uint8_t>(pick * 7), 255 });
        }
    }
    return raster;
}

void testGifRoundTripsExactly() {
    std::vector<ls::RasterBuffer> frames = { noise(12, 5, 1, 10), noise(12, 5, 2, 10) };
    std::vector<uint8_t> gif;
    AnimationReport report;
    std::string error;
    REQUIRE(encodeGif(frames, { 100, 250 }, true, &gif, &report, &error));
    CHECK(report.frames == 2 && !report.reducedColours);
    CHECK(countGifFrames(gif) == 2);

    std::vector<ls::RasterBuffer> back;
    std::vector<int> holds;
    REQUIRE(decodeFrames(gif, &back, &holds, &error));
    REQUIRE(back.size() == 2);
    CHECK(sameAsGif(back[0], frames[0]));
    CHECK(sameAsGif(back[1], frames[1]));
    CHECK(holds[0] == 100 && holds[1] == 250);
}

void testLongStreamsFillAndClearTheTable() {
    // 200 colours of noise at 160 x 160 is far past 4096 codes, so the
    // encoder has to widen to twelve bits and clear -- where LZW encoders go
    // wrong, if they do.
    std::vector<ls::RasterBuffer> frames = { noise(160, 200, 7, 3) };
    std::vector<uint8_t> gif;
    std::string error;
    REQUIRE(encodeGif(frames, { 100 }, false, &gif, nullptr, &error));
    std::vector<ls::RasterBuffer> back;
    std::vector<int> holds;
    REQUIRE(decodeFrames(gif, &back, &holds, &error));
    REQUIRE(back.size() == 1);
    CHECK(sameAsGif(back[0], frames[0]));

    // And a flat picture, the other extreme: long runs, few codes.
    ls::RasterBuffer flat = ls::makeRaster(64, 64);
    for (uint32_t y = 0; y < 64; ++y) {
        for (uint32_t x = 0; x < 64; ++x) {
            put(flat, x, y, ls::Color{ 40, 80, 120, 255 });
        }
    }
    REQUIRE(encodeGif({ flat }, { 100 }, false, &gif, nullptr, &error));
    REQUIRE(decodeFrames(gif, &back, &holds, &error));
    CHECK(sameAsGif(back[0], flat));
}

void testTooManyColoursForOnePaletteGetOneEach() {
    // 200 colours in each frame, none shared: 400 between them.
    std::vector<ls::RasterBuffer> frames = { noise(40, 200, 3, 10), noise(40, 200, 4, 90) };
    std::vector<uint8_t> gif;
    AnimationReport report;
    std::string error;
    REQUIRE(encodeGif(frames, { 100, 100 }, true, &gif, &report, &error));
    CHECK(!report.reducedColours);
    std::vector<ls::RasterBuffer> back;
    std::vector<int> holds;
    REQUIRE(decodeFrames(gif, &back, &holds, &error));
    REQUIRE(back.size() == 2);
    CHECK(sameAsGif(back[0], frames[0]));
    CHECK(sameAsGif(back[1], frames[1]));
}

void testTooManyForAnyPaletteIsReducedAndSaid() {
    std::vector<ls::RasterBuffer> frames = { noise(40, 250, 5, 10) };
    // Add colours past 255 in the one frame.
    for (uint32_t x = 0; x < 40; ++x) {
        put(frames[0], x, 0, ls::Color{ static_cast<uint8_t>(x), 200, 33, 255 });
        put(frames[0], x, 1, ls::Color{ static_cast<uint8_t>(x), 201, 33, 255 });
    }
    std::vector<uint8_t> gif;
    AnimationReport report;
    std::string error;
    REQUIRE(encodeGif(frames, { 100 }, false, &gif, &report, &error));
    CHECK(report.reducedColours);
    std::vector<ls::RasterBuffer> back;
    std::vector<int> holds;
    REQUIRE(decodeFrames(gif, &back, &holds, &error));
    CHECK(back.size() == 1 && back[0].width == 40);
}

uint32_t be32(const std::vector<uint8_t>& b, size_t at) {
    return static_cast<uint32_t>(b[at]) << 24 | static_cast<uint32_t>(b[at + 1]) << 16 |
           static_cast<uint32_t>(b[at + 2]) << 8 | b[at + 3];
}

void testApngIsWellFormed() {
    std::vector<ls::RasterBuffer> frames = { noise(10, 6, 11, 50), noise(10, 6, 12, 50),
                                             noise(10, 6, 13, 50) };
    // Partial alpha, which only APNG of the two can carry.
    put(frames[0], 0, 0, ls::Color{ 255, 0, 0, 100 });
    std::vector<uint8_t> png;
    std::string error;
    REQUIRE(encodeApng(frames, { 100, 200, 300 }, true, &png, &error));

    // Walk the chunks: acTL says three frames, there are three fcTLs, the
    // sequence numbers run 0.. without a gap, and every CRC is right --
    // checked by stb decoding the default image, which verifies nothing about
    // fdAT, so the counts are checked here.
    size_t at = 8;
    int fctl = 0;
    int fdat = 0;
    uint32_t expected = 0;
    bool sequenced = true;
    uint32_t declared = 0;
    while (at + 12 <= png.size()) {
        const uint32_t length = be32(png, at);
        const std::string type(png.begin() + static_cast<long long>(at + 4),
                               png.begin() + static_cast<long long>(at + 8));
        if (type == "acTL") { declared = be32(png, at + 8); }
        if (type == "fcTL" || type == "fdAT") {
            sequenced = sequenced && be32(png, at + 8) == expected++;
            (type == "fcTL" ? fctl : fdat)++;
        }
        at += 12 + length;
    }
    CHECK(at == png.size());
    CHECK(declared == 3 && fctl == 3 && fdat == 2 && sequenced);

    ls::RasterBuffer first;
    REQUIRE(decodeImage(png, &first, &error));
    const uint8_t* p = first.row(0);
    CHECK(p[0] == 255 && p[3] == 100);          // the partial alpha survives
}

void testPingPongPlaysThereAndBack() {
    Cycle cycle;
    cycle.frames = { 0, 1, 2, 3 };
    cycle.loop = LoopMode::PingPong;
    CHECK((stepsToPlay(cycle) == std::vector<int>{ 0, 1, 2, 3, 2, 1 }));
    cycle.loop = LoopMode::Loop;
    CHECK(stepsToPlay(cycle).size() == 4);
}

void testADocumentExportsWhatItCompilesTo() {
    Document doc;
    REQUIRE(doc.create("anim", 8, 8));
    PaintLayer layer;
    REQUIRE(createPaintLayer(doc, doc.sprite(), "Layer 1", ls::Color{ 200, 0, 0, 255 }, &layer));
    doc.beginAction("Pencil");
    InkStroke stroke;
    Ink red;
    red.colour = ls::Color{ 200, 0, 0, 255 };
    REQUIRE(beginInkStroke(doc, layer.layer, red, &stroke));
    REQUIRE(strokeInk(doc, stroke, {{ 1, 1 }}));
    doc.endAction();
    REQUIRE(duplicateFrame(doc, 0) == 1);
    REQUIRE(setFrameDuration(doc, 1, 300));

    const std::vector<Frame> frames = readFrames(doc);
    Cycle cycle = everyFrame(static_cast<int>(frames.size()));
    const std::string path = "anim_export_test.gif";
    AnimationSettings settings;
    settings.scale = 2;
    AnimationReport report;
    std::string error;
    REQUIRE(exportAnimation(doc, frames, cycle, path, settings, &report, &error));
    std::vector<uint8_t> bytes;
    REQUIRE(readFile(path, bytes, &error));
    std::vector<ls::RasterBuffer> back;
    std::vector<int> holds;
    REQUIRE(decodeFrames(bytes, &back, &holds, &error));
    REQUIRE(back.size() == 2);
    CHECK(back[0].width == 16);                     // at 2x
    CHECK(back[0].row(2)[2 * 4] == 200);            // the pixel at (1,1), doubled
    CHECK(holds[1] == 300);
    deleteFile(path);

    // A sequence: one numbered file per step.
    settings.format = AnimationFormat::PngSequence;
    REQUIRE(exportAnimation(doc, frames, cycle, "anim_seq.png", settings, &report, &error));
    CHECK(fileExists("anim_seq-01.png") && fileExists("anim_seq-02.png"));
    deleteFile("anim_seq-01.png");
    deleteFile("anim_seq-02.png");
}

} // namespace

int main() {
    testGifRoundTripsExactly();
    testLongStreamsFillAndClearTheTable();
    testTooManyColoursForOnePaletteGetOneEach();
    testTooManyForAnyPaletteIsReducedAndSaid();
    testApngIsWellFormed();
    testPingPongPlaysThereAndBack();
    testADocumentExportsWhatItCompilesTo();
    if (failures == 0) {
        std::printf("anim export: all passed\n");
        return 0;
    }
    std::printf("anim export: %d failure(s)\n", failures);
    return 1;
}
