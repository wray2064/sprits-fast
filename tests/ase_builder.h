// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors
#pragma once

// ase_builder.h — Aseprite files, written byte by byte from the published
// format description, so that a test (and a fuzzer's seed corpus) says exactly
// what is in them. Shared by aseprite_tests.cpp and tests/fuzz.

#include "app/zlib.h"

#include <livesprite/livesprite.h>

#include <cstdint>
#include <string>
#include <vector>

namespace asebuild {

using Bytes = std::vector<uint8_t>;

inline void u8(Bytes& b, uint32_t v) { b.push_back(static_cast<uint8_t>(v)); }
inline void u16(Bytes& b, uint32_t v) { u8(b, v & 0xFF); u8(b, (v >> 8) & 0xFF); }
inline void u32(Bytes& b, uint32_t v) { u16(b, v & 0xFFFF); u16(b, v >> 16); }
inline void text(Bytes& b, const std::string& s) {
    u16(b, static_cast<uint32_t>(s.size()));
    b.insert(b.end(), s.begin(), s.end());
}
inline void zeros(Bytes& b, size_t n) { b.insert(b.end(), n, 0); }

inline Bytes chunk(uint16_t type, const Bytes& body) {
    Bytes out;
    u32(out, static_cast<uint32_t>(body.size() + 6));
    u16(out, type);
    out.insert(out.end(), body.begin(), body.end());
    return out;
}

inline Bytes layerChunk(const std::string& name, uint16_t flags, uint16_t type, uint16_t level,
                        uint16_t blend, uint8_t opacity) {
    Bytes b;
    u16(b, flags); u16(b, type); u16(b, level); u16(b, 0); u16(b, 0);
    u16(b, blend); u8(b, opacity); zeros(b, 3); text(b, name);
    return chunk(0x2004, b);
}

inline Bytes celHead(int layer, int x, int y, uint16_t type, uint8_t opacity = 255) {
    Bytes b;
    u16(b, static_cast<uint32_t>(layer));
    u16(b, static_cast<uint32_t>(x) & 0xFFFF);
    u16(b, static_cast<uint32_t>(y) & 0xFFFF);
    u8(b, opacity); u16(b, type); u16(b, 0); zeros(b, 5);
    return b;
}

inline Bytes rawCel(int layer, int x, int y, uint16_t w, uint16_t h, const Bytes& pixels) {
    Bytes b = celHead(layer, x, y, 0);
    u16(b, w); u16(b, h);
    b.insert(b.end(), pixels.begin(), pixels.end());
    return chunk(0x2005, b);
}

inline Bytes packedCel(int layer, int x, int y, uint16_t w, uint16_t h, const Bytes& pixels) {
    Bytes b = celHead(layer, x, y, 2);
    u16(b, w); u16(b, h);
    Bytes packed;
    fast::zlibDeflate(pixels, &packed);
    b.insert(b.end(), packed.begin(), packed.end());
    return chunk(0x2005, b);
}

inline Bytes linkedCel(int layer, uint16_t frame) {
    Bytes b = celHead(layer, 0, 0, 1);
    u16(b, frame);
    return chunk(0x2005, b);
}

inline Bytes paletteChunk(const std::vector<ls::Color>& colours) {
    Bytes b;
    u32(b, static_cast<uint32_t>(colours.size())); u32(b, 0);
    u32(b, static_cast<uint32_t>(colours.size() - 1)); zeros(b, 8);
    for (const ls::Color& c : colours) {
        u16(b, 0); u8(b, c.r); u8(b, c.g); u8(b, c.b); u8(b, c.a);
    }
    return chunk(0x2019, b);
}

inline Bytes tagsChunk(int from, int to, int direction, const std::string& name) {
    Bytes b;
    u16(b, 1); zeros(b, 8);
    u16(b, static_cast<uint32_t>(from)); u16(b, static_cast<uint32_t>(to));
    u8(b, static_cast<uint32_t>(direction)); u16(b, 0); zeros(b, 6 + 3 + 1);
    text(b, name);
    return chunk(0x2018, b);
}

inline Bytes frame(uint16_t ms, const std::vector<Bytes>& chunks) {
    Bytes body;
    for (const Bytes& c : chunks) { body.insert(body.end(), c.begin(), c.end()); }
    Bytes out;
    u32(out, static_cast<uint32_t>(body.size() + 16));
    u16(out, 0xF1FA);
    u16(out, static_cast<uint32_t>(chunks.size()));
    u16(out, ms); zeros(out, 2);
    u32(out, static_cast<uint32_t>(chunks.size()));
    out.insert(out.end(), body.begin(), body.end());
    return out;
}

inline Bytes file(uint16_t w, uint16_t h, uint16_t depth, const std::vector<Bytes>& frames) {
    Bytes body;
    for (const Bytes& f : frames) { body.insert(body.end(), f.begin(), f.end()); }
    Bytes out;
    u32(out, static_cast<uint32_t>(body.size() + 128));
    u16(out, 0xA5E0);
    u16(out, static_cast<uint32_t>(frames.size()));
    u16(out, w); u16(out, h); u16(out, depth);
    u32(out, 1);                        // layer opacity is valid
    u16(out, 100); u32(out, 0); u32(out, 0);
    u8(out, 0);                         // transparent index 0
    zeros(out, 3);
    u16(out, 0);
    u8(out, 1); u8(out, 1);
    u16(out, 0); u16(out, 0); u16(out, 16); u16(out, 16);
    zeros(out, 84);
    out.insert(out.end(), body.begin(), body.end());
    return out;
}

// An indexed 4x4 sprite, three frames: a raw cel, a compressed cel, a linked
// cel; a tag over frames 1..2 ping-pong; two layers, the upper at half.
inline Bytes indexedSprite() {
    const std::vector<ls::Color> palette = {
        { 0, 0, 0, 255 }, { 200, 30, 30, 255 }, { 30, 200, 30, 255 }, { 200, 30, 30, 255 } };
    // Frame 0: layer 0 has index 1 at (0,0) and index 3 (same colour as 1)
    // at (1,0); layer 1 has index 2 at (3,3), placed with a cel offset.
    Bytes base = { 1, 3, 0, 0 };           // a 4x1 cel at (0,0)
    Bytes top = { 2 };                     // a 1x1 cel at (3,3)
    Bytes moved = { 0, 2 };                // frame 1's base: 2x1 at (2,1), index 2 at (3,1)
    return file(4, 4, 8, {
        frame(80, { layerChunk("base", 1, 0, 0, 0, 255), layerChunk("top", 1, 0, 0, 0, 128),
                    paletteChunk(palette), tagsChunk(1, 2, 2, "bounce"),
                    rawCel(0, 0, 0, 4, 1, base), rawCel(1, 3, 3, 1, 1, top) }),
        frame(120, { packedCel(0, 2, 1, 2, 1, moved) }),
        frame(60, { linkedCel(0, 1) }),
    });
}

// A 2x2 RGBA sprite of one layer, two frames, one raw and one compressed cel.
inline Bytes rgbaSprite() {
    const Bytes red = { 255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 128 };
    return file(2, 2, 32, {
        frame(100, { layerChunk("layer", 1, 0, 0, 0, 255), rawCel(0, 0, 0, 2, 2, red) }),
        frame(100, { packedCel(0, 0, 0, 2, 2, red) }),
    });
}

} // namespace asebuild
