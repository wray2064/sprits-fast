// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

// webp_tests.cpp — the container around lossless WebP, and the hooks for
// checking it against another program's decoder.
//
// The RIFF sizes add up, a picture is one VP8L chunk whose header carries its
// size, an animation has its canvas, loop and one frame chunk per step with its
// hold, and nonsense is refused. That pixels come back exactly is
// webp_decode_tests.cpp. And with `--write DIR` this program writes the sample
// pictures as .webp beside their raw RGBA, and with `--read IN OUT` it decodes
// a file another program made: tools/webp_crosscheck.py uses both to compare
// against libwebp, both ways.

#include "app/webp.h"
#include "webp_samples.h"

#include <cstdio>
#include <cstring>
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

uint32_t le32(const std::vector<uint8_t>& b, size_t at) {
    return b[at] | (b[at + 1] << 8) | (b[at + 2] << 16) | (static_cast<uint32_t>(b[at + 3]) << 24);
}

uint32_t le24(const std::vector<uint8_t>& b, size_t at) {
    return b[at] | (b[at + 1] << 8) | (b[at + 2] << 16);
}

bool tag(const std::vector<uint8_t>& b, size_t at, const char* name) {
    return at + 4 <= b.size() && std::memcmp(b.data() + at, name, 4) == 0;
}

void testAPictureIsOneVp8lChunk() {
    for (int colours : { 1, 2, 3, 12, 200, 300 }) {
        std::vector<uint8_t> file;
        std::string error;
        REQUIRE(encodeWebp(picture(37, 21, colours), &file, &error));
        CHECK(tag(file, 0, "RIFF") && tag(file, 8, "WEBP") && tag(file, 12, "VP8L"));
        CHECK(le32(file, 4) + 8 == file.size());
        const uint32_t chunk = le32(file, 16);
        CHECK(20 + chunk + (chunk % 2) == file.size());
        CHECK(file[20] == 0x2F);
        // Width and height, less one, in the fourteen-bit fields that follow.
        const uint32_t bits = le32(file, 21);
        CHECK((bits & 0x3FFF) + 1 == 37);
        CHECK(((bits >> 14) & 0x3FFF) + 1 == 21);
        CHECK(((bits >> 28) & 1) == 1);          // alpha is used: colour 0 is clear
    }
}

void testAnAnimationHasItsFramesAndHolds() {
    std::vector<ls::RasterBuffer> frames{ picture(10, 6, 4), picture(10, 6, 5),
                                          picture(10, 6, 2) };
    std::vector<uint8_t> file;
    std::string error;
    REQUIRE(encodeAnimatedWebp(frames, { 100, 250, 40 }, true, &file, &error));
    CHECK(tag(file, 0, "RIFF") && tag(file, 8, "WEBP") && tag(file, 12, "VP8X"));
    CHECK(le32(file, 4) + 8 == file.size());
    CHECK((file[20] & 0x02) != 0 && (file[20] & 0x10) != 0);   // animated, with alpha
    CHECK(le24(file, 24) + 1 == 10 && le24(file, 27) + 1 == 6);
    CHECK(tag(file, 30, "ANIM"));
    CHECK(file[42] == 0 && file[43] == 0);                     // loops for ever
    size_t at = 44;
    const int holds[3] = { 100, 250, 40 };
    for (int i = 0; i < 3; ++i) {
        REQUIRE(tag(file, at, "ANMF"));
        const uint32_t size = le32(file, at + 4);
        CHECK(le24(file, at + 14) + 1 == 10 && le24(file, at + 17) + 1 == 6);
        CHECK(le24(file, at + 20) == static_cast<uint32_t>(holds[i]));
        CHECK(file[at + 23] == 0x02);                          // no blending
        CHECK(tag(file, at + 24, "VP8L"));
        at += 8 + size + (size % 2);
    }
    CHECK(at == file.size());

    std::vector<uint8_t> once;
    REQUIRE(encodeAnimatedWebp(frames, { 100, 250, 40 }, false, &once, &error));
    CHECK(once[42] == 1);
}

void testNonsenseIsRefused() {
    std::vector<uint8_t> file;
    std::string error;
    CHECK(!encodeWebp(ls::RasterBuffer{}, &file, &error) && !error.empty());
    CHECK(!encodeAnimatedWebp({}, {}, true, &file, &error));
    CHECK(!encodeAnimatedWebp({ picture(4, 4, 2), picture(5, 4, 2) }, { 10, 10 }, true, &file,
                              &error));
    CHECK(!encodeAnimatedWebp({ picture(4, 4, 2) }, { 10, 10 }, true, &file, &error));
}

void writeSamples(const std::string& dir) {
    uint32_t seed = 12345;
    int n = 0;
    for (const auto& kind : kinds()) {
        const ls::RasterBuffer image = makeSample(kind, seed);
        std::vector<uint8_t> file;
        std::string error;
        if (!encodeWebp(image, &file, &error)) {
            std::printf("could not encode sample %d: %s\n", n, error.c_str());
            continue;
        }
        const std::string stem = dir + "/sample" + std::to_string(n++);
        if (FILE* f = std::fopen((stem + ".webp").c_str(), "wb")) {
            std::fwrite(file.data(), 1, file.size(), f);
            std::fclose(f);
        }
        if (FILE* f = std::fopen((stem + ".rgba").c_str(), "wb")) {
            std::fprintf(f, "%u %u\n", kind.w, kind.h);
            std::fwrite(image.pixels.data(), 1, image.pixels.size(), f);
            std::fclose(f);
        }
    }
    // And an animation of three of the same size.
    std::vector<ls::RasterBuffer> frames{ picture(40, 30, 3), picture(40, 30, 7),
                                          picture(40, 30, 1) };
    std::vector<uint8_t> file;
    std::string error;
    if (encodeAnimatedWebp(frames, { 120, 80, 200 }, true, &file, &error)) {
        if (FILE* f = std::fopen((dir + "/anim.webp").c_str(), "wb")) {
            std::fwrite(file.data(), 1, file.size(), f);
            std::fclose(f);
        }
        for (size_t i = 0; i < frames.size(); ++i) {
            if (FILE* f = std::fopen((dir + "/anim" + std::to_string(i) + ".rgba").c_str(), "wb")) {
                std::fprintf(f, "%u %u\n", frames[i].width, frames[i].height);
                std::fwrite(frames[i].pixels.data(), 1, frames[i].pixels.size(), f);
                std::fclose(f);
            }
        }
    }
    std::printf("wrote %d samples and an animation to %s\n", n, dir.c_str());
}

// With --read IN.webp OUT.rgba, decodes a file another program made and writes
// "W H\n" and the raw RGBA, for the same cross-check the other way round.
int readOne(const char* in, const char* out) {
    std::vector<uint8_t> bytes;
    if (FILE* f = std::fopen(in, "rb")) {
        uint8_t buffer[65536];
        size_t got = 0;
        while ((got = std::fread(buffer, 1, sizeof buffer, f)) > 0) {
            bytes.insert(bytes.end(), buffer, buffer + got);
        }
        std::fclose(f);
    } else {
        std::printf("cannot open %s\n", in);
        return 2;
    }
    ls::RasterBuffer image;
    std::string error;
    if (!decodeWebp(bytes, &image, &error)) {
        std::printf("%s: %s\n", in, error.c_str());
        return 1;
    }
    if (FILE* f = std::fopen(out, "wb")) {
        std::fprintf(f, "%u %u\n", image.width, image.height);
        std::fwrite(image.pixels.data(), 1, image.pixels.size(), f);
        std::fclose(f);
    }
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    if (argc == 3 && std::string(argv[1]) == "--write") {
        writeSamples(argv[2]);
        return 0;
    }
    if (argc == 4 && std::string(argv[1]) == "--read") {
        return readOne(argv[2], argv[3]);
    }
    testAPictureIsOneVp8lChunk();
    testAnAnimationHasItsFramesAndHolds();
    testNonsenseIsRefused();
    if (failures == 0) {
        std::printf("webp: all passed\n");
        return 0;
    }
    std::printf("webp: %d failure(s)\n", failures);
    return 1;
}
