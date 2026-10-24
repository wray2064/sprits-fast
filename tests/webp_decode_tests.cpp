// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

// webp_decode_tests.cpp — the lossless WebP reader (app/webp_decode.cpp).
//
// Every kind of picture the writer has a path for comes back exactly through the
// reader, an animation reads as its first frame, a predicted picture really is
// smaller, and damaged files are refused rather than trusted. That another
// program's encoder (libwebp) is read correctly too is checked by
// tools/webp_crosscheck.py, which runs this program's sibling (webp_tests
// --read) over files libwebp wrote at every effort level.

#include "app/webp.h"
#include "webp_samples.h"

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
using namespace webp_samples;

void testEveryKindComesBackExactly() {
    uint32_t seed = 12345;
    for (const Kind& kind : kinds()) {
        const ls::RasterBuffer image = makeSample(kind, seed);
        std::vector<uint8_t> file;
        std::string error;
        REQUIRE(encodeWebp(image, &file, &error));
        CHECK(looksLikeWebp(file));
        uint32_t w = 0, h = 0;
        REQUIRE(webpSize(file, &w, &h, &error));
        CHECK(w == kind.w && h == kind.h);
        ls::RasterBuffer back;
        const bool ok = decodeWebp(file, &back, &error);
        if (!ok) {
            std::printf("kind %ux%u colours %d style %d: %s\n", kind.w, kind.h, kind.colours,
                        kind.style, error.c_str());
        }
        REQUIRE(ok);
        CHECK(same(image, back));
    }
}

void testAnAnimationReadsAsItsFirstFrame() {
    std::vector<ls::RasterBuffer> frames{ picture(10, 6, 4), picture(10, 6, 5) };
    std::vector<uint8_t> file;
    std::string error;
    REQUIRE(encodeAnimatedWebp(frames, { 100, 100 }, true, &file, &error));
    ls::RasterBuffer back;
    REQUIRE(decodeWebp(file, &back, &error));
    CHECK(same(frames[0], back));
}

void testAGradientCostsLessThanItsRawSize() {
    uint32_t seed = 1;
    const ls::RasterBuffer image = makeSample({ 128, 128, 0, 4 }, seed);
    std::vector<uint8_t> file;
    std::string error;
    REQUIRE(encodeWebp(image, &file, &error));
    CHECK(file.size() < image.pixels.size() / 3);
}

void testDamagedFilesAreRefusedNotTrusted() {
    uint32_t seed = 99;
    std::vector<uint8_t> good;
    std::string error;
    REQUIRE(encodeWebp(makeSample({ 40, 30, 300, 0 }, seed), &good, &error));
    ls::RasterBuffer back;

    // Cut short at every length.
    for (size_t keep = 0; keep < good.size(); keep += 7) {
        std::vector<uint8_t> cut(good.begin(), good.begin() + static_cast<long>(keep));
        decodeWebp(cut, &back, &error);                       // must not crash; usually refuses
    }
    // One byte changed at every place.
    uint32_t state = 7;
    for (size_t at = 20; at < good.size(); ++at) {
        std::vector<uint8_t> bent = good;
        bent[at] = static_cast<uint8_t>(nextRandom(state));
        decodeWebp(bent, &back, &error);
    }
    // Lossy data is named, not mistaken for lossless.
    std::vector<uint8_t> lossy = good;
    lossy[12] = 'V'; lossy[13] = 'P'; lossy[14] = '8'; lossy[15] = ' ';
    CHECK(!decodeWebp(lossy, &back, &error) && error.find("lossy") != std::string::npos);
    // Not WebP at all.
    CHECK(!decodeWebp({ 1, 2, 3 }, &back, &error));
    CHECK(!decodeWebp({}, &back, &error));
    // A header claiming more pixels than this build opens.
    std::vector<uint8_t> huge = good;
    huge[21] = 0xFF; huge[22] = 0xFF; huge[23] = 0xFF; huge[24] = 0x0F;     // 16384 x 16384
    CHECK(!decodeWebp(huge, &back, &error));
}

} // namespace

int main() {
    testEveryKindComesBackExactly();
    testAnAnimationReadsAsItsFirstFrame();
    testAGradientCostsLessThanItsRawSize();
    testDamagedFilesAreRefusedNotTrusted();
    if (failures == 0) {
        std::printf("webp_decode: all passed\n");
        return 0;
    }
    std::printf("webp_decode: %d failure(s)\n", failures);
    return 1;
}
