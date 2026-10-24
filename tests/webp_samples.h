// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors
#pragma once

// webp_samples.h — pictures for the WebP tests, one for every way the encoder
// has of writing one: 1, 2, 4, 16 and 256 colours, more than 256, runs, repeated
// rows, noise, translucency, and a smooth gradient (the predictor's case). The
// writer's tests and the reader's share them, and `--write` hands them to
// libwebp (tools/webp_crosscheck.py).

#include <livesprite/livesprite.h>

#include <cstdint>
#include <vector>

namespace webp_samples {

inline uint32_t nextRandom(uint32_t& state) {
    state = state * 1664525u + 1013904223u;
    return state >> 8;
}

// A picture of `colours` colours cycling, with some translucent.
inline ls::RasterBuffer picture(uint32_t w, uint32_t h, int colours) {
    ls::RasterBuffer image = ls::makeRaster(w, h);
    for (uint32_t y = 0; y < h; ++y) {
        for (uint32_t x = 0; x < w; ++x) {
            const int c = static_cast<int>((x / 3 + y * 7) % static_cast<uint32_t>(colours));
            ls::writePixel(image, static_cast<int32_t>(x), static_cast<int32_t>(y),
                           ls::Color{ static_cast<uint8_t>(c * 37), static_cast<uint8_t>(c * 11),
                                      static_cast<uint8_t>(255 - c), c == 0 ? uint8_t(0) : uint8_t(255) });
        }
    }
    return image;
}

struct Kind { uint32_t w, h; int colours; int style; };

inline const std::vector<Kind>& kinds() {
    static const std::vector<Kind> all = {
        { 1, 1, 1, 0 }, { 7, 3, 2, 0 }, { 33, 17, 4, 1 }, { 64, 64, 16, 2 },
        { 100, 37, 256, 0 }, { 50, 50, 257, 0 }, { 128, 96, 5000, 3 }, { 300, 5, 3, 1 },
        { 17, 200, 9, 2 }, { 256, 256, 40, 1 }, { 5000, 2, 6, 1 }, { 70, 45, 4000, 4 },
    };
    return all;
}

inline ls::RasterBuffer makeSample(const Kind& kind, uint32_t& seed) {
    std::vector<ls::Color> palette;
    for (int c = 0; c < kind.colours; ++c) {
        const uint32_t r = nextRandom(seed);
        palette.push_back({ uint8_t(r), uint8_t(r >> 8), uint8_t(r >> 16),
                            uint8_t((c % 5 == 0) ? (r >> 3) & 0xFF : 255) });
    }
    ls::RasterBuffer image = ls::makeRaster(kind.w, kind.h);
    for (uint32_t y = 0; y < kind.h; ++y) {
        for (uint32_t x = 0; x < kind.w; ++x) {
            size_t c = 0;
            switch (kind.style) {
                case 1: c = (x / 5 + y / 3) % palette.size(); break;       // runs
                case 2: c = (x * 7) % palette.size(); break;               // repeated rows
                case 3: c = (x + y * kind.w) % palette.size(); break;      // many colours
                case 4:                                                    // a smooth gradient
                    ls::writePixel(image, int32_t(x), int32_t(y),
                                   ls::Color{ uint8_t(x * 3 + y), uint8_t(y * 5 + x / 2),
                                              uint8_t(200 - x - y), 255 });
                    continue;
                default: c = nextRandom(seed) % palette.size(); break;    // noise
            }
            ls::writePixel(image, int32_t(x), int32_t(y), palette[c]);
        }
    }
    return image;
}

inline bool same(const ls::RasterBuffer& a, const ls::RasterBuffer& b) {
    return a.width == b.width && a.height == b.height && a.pixels == b.pixels;
}

} // namespace webp_samples
