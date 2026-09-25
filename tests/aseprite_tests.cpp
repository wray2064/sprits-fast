// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

// aseprite_tests.cpp — Aseprite's files, opened as work rather than pictures.
//
// Files are written here byte by byte from the published format description,
// so the test says exactly what is in them. The promises: raw, compressed and
// linked cels all land where they belong; an indexed sprite's indices become
// slots, so a slot edit recolours them; the layers keep their names, opacity,
// visibility and blend; groups are groups; frames keep their holds; tags
// become cycles in the right direction; and a damaged file is refused with a
// reason rather than read past its end.

#include "app/animation.h"
#include "app/document.h"
#include "app/element.h"
#include "app/import_aseprite.h"
#include "app/layers.h"
#include "app/palette.h"
#include "app/zlib.h"

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
using Bytes = std::vector<uint8_t>;

void u8(Bytes& b, uint32_t v) { b.push_back(static_cast<uint8_t>(v)); }
void u16(Bytes& b, uint32_t v) { u8(b, v & 0xFF); u8(b, (v >> 8) & 0xFF); }
void u32(Bytes& b, uint32_t v) { u16(b, v & 0xFFFF); u16(b, v >> 16); }
void text(Bytes& b, const std::string& s) {
    u16(b, static_cast<uint32_t>(s.size()));
    b.insert(b.end(), s.begin(), s.end());
}
void zeros(Bytes& b, size_t n) { b.insert(b.end(), n, 0); }

Bytes chunk(uint16_t type, const Bytes& body) {
    Bytes out;
    u32(out, static_cast<uint32_t>(body.size() + 6));
    u16(out, type);
    out.insert(out.end(), body.begin(), body.end());
    return out;
}

Bytes layerChunk(const std::string& name, uint16_t flags, uint16_t type, uint16_t level,
                 uint16_t blend, uint8_t opacity) {
    Bytes b;
    u16(b, flags); u16(b, type); u16(b, level); u16(b, 0); u16(b, 0);
    u16(b, blend); u8(b, opacity); zeros(b, 3); text(b, name);
    return chunk(0x2004, b);
}

Bytes celHead(int layer, int x, int y, uint16_t type, uint8_t opacity = 255) {
    Bytes b;
    u16(b, static_cast<uint32_t>(layer));
    u16(b, static_cast<uint32_t>(x) & 0xFFFF);
    u16(b, static_cast<uint32_t>(y) & 0xFFFF);
    u8(b, opacity); u16(b, type); u16(b, 0); zeros(b, 5);
    return b;
}

Bytes rawCel(int layer, int x, int y, uint16_t w, uint16_t h, const Bytes& pixels) {
    Bytes b = celHead(layer, x, y, 0);
    u16(b, w); u16(b, h);
    b.insert(b.end(), pixels.begin(), pixels.end());
    return chunk(0x2005, b);
}

Bytes packedCel(int layer, int x, int y, uint16_t w, uint16_t h, const Bytes& pixels) {
    Bytes b = celHead(layer, x, y, 2);
    u16(b, w); u16(b, h);
    Bytes packed;
    zlibDeflate(pixels, &packed);
    b.insert(b.end(), packed.begin(), packed.end());
    return chunk(0x2005, b);
}

Bytes linkedCel(int layer, uint16_t frame) {
    Bytes b = celHead(layer, 0, 0, 1);
    u16(b, frame);
    return chunk(0x2005, b);
}

Bytes paletteChunk(const std::vector<ls::Color>& colours) {
    Bytes b;
    u32(b, static_cast<uint32_t>(colours.size())); u32(b, 0);
    u32(b, static_cast<uint32_t>(colours.size() - 1)); zeros(b, 8);
    for (const ls::Color& c : colours) {
        u16(b, 0); u8(b, c.r); u8(b, c.g); u8(b, c.b); u8(b, c.a);
    }
    return chunk(0x2019, b);
}

Bytes tagsChunk(int from, int to, int direction, const std::string& name) {
    Bytes b;
    u16(b, 1); zeros(b, 8);
    u16(b, static_cast<uint32_t>(from)); u16(b, static_cast<uint32_t>(to));
    u8(b, static_cast<uint32_t>(direction)); u16(b, 0); zeros(b, 6 + 3 + 1);
    text(b, name);
    return chunk(0x2018, b);
}

Bytes frame(uint16_t ms, const std::vector<Bytes>& chunks) {
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

Bytes file(uint16_t w, uint16_t h, uint16_t depth, const std::vector<Bytes>& frames) {
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

ls::Color pixel(Document& doc, ls::SpriteId sprite, int x, int y) {
    auto size = doc.engine().getCanvasSize(doc.id());
    auto compiled = doc.engine().compileSprite(
        sprite, compileProfile(ls::CompileProfileType::Export, static_cast<uint32_t>(size.value.x),
                               static_cast<uint32_t>(size.value.y)));
    return compiled.ok() ? ls::readPixel(compiled.value.raster, x, y) : ls::Color{ 0, 0, 0, 0 };
}

bool same(ls::Color a, ls::Color b) {
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

// An indexed 4x4 sprite, three frames: a raw cel, a compressed cel, a linked
// cel; a tag over frames 1..2 ping-pong; two layers, the upper at half.
Bytes indexedSprite() {
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

void testIndexedCelsLandAndBecomeSlots() {
    AseFile ase;
    std::string error;
    REQUIRE(parseAseprite(indexedSprite(), &ase, &error));
    CHECK(ase.frames.size() == 3 && ase.layers.size() == 2 && ase.palette.size() == 4);

    Document doc;
    AsepriteReport report;
    REQUIRE(documentFromAseprite(doc, ase, "walk", &report, &error));
    CHECK(report.indexed && report.layers == 2 && report.tags == 1);
    CHECK(!doc.modified() && !doc.canUndo());          // opening is not editing
    const std::vector<Frame> frames = readFrames(doc);
    REQUIRE(frames.size() == 3);

    // Raw cel.
    CHECK(same(pixel(doc, frames[0].sprite, 0, 0), ls::Color{ 200, 30, 30, 255 }));
    // Index 0 is the transparent index, and on a layer that is not the
    // background it draws nothing.
    CHECK(pixel(doc, frames[0].sprite, 2, 0).a == 0);
    CHECK(pixel(doc, frames[0].sprite, 3, 1).a == 0);
    // Compressed cel, at its offset.
    CHECK(same(pixel(doc, frames[1].sprite, 3, 1), ls::Color{ 30, 200, 30, 255 }));
    CHECK(pixel(doc, frames[1].sprite, 0, 0).a == 0);
    // Linked cel: frame 2 shows frame 1's.
    CHECK(same(pixel(doc, frames[2].sprite, 3, 1), ls::Color{ 30, 200, 30, 255 }));

    // Holds.
    CHECK(frames[0].durationMs == 80 && frames[1].durationMs == 120 && frames[2].durationMs == 60);

    // Indices 1 and 3 share a colour but stay two slots: recolour slot 3 and
    // only the pixel painted with index 3 changes.
    doc.beginAction("slot");
    REQUIRE(setPaletteEntry(doc, 3, ls::Color{ 0, 0, 255, 255 }));
    doc.endAction();
    CHECK(same(pixel(doc, frames[0].sprite, 1, 0), ls::Color{ 0, 0, 255, 255 }));
    CHECK(same(pixel(doc, frames[0].sprite, 0, 0), ls::Color{ 200, 30, 30, 255 }));

    // The upper layer at half opacity.
    auto layers = doc.engine().getSpriteInfo(frames[0].sprite);
    REQUIRE(layers.ok() && layers.value.layers.size() == 2);
    LayerProps props;
    REQUIRE(readLayerProps(doc, layers.value.layers[1], &props));
    CHECK(props.name == "top" && props.opacity > 0.49f && props.opacity < 0.51f);

    // The tag, ping-pong over frames 1..2.
    const std::vector<Cycle> cycles = readCycles(doc, 3);
    REQUIRE(cycles.size() == 1);
    CHECK(cycles[0].name == "bounce" && cycles[0].loop == LoopMode::PingPong);
    CHECK((cycles[0].frames == std::vector<int>{ 1, 2 }));
}

void testRgbaPixelsMatchSlotsWhereTheyCan() {
    const std::vector<ls::Color> palette = { { 10, 20, 30, 255 } };
    Bytes pixels = { 10, 20, 30, 255,   99, 98, 97, 255 };     // a slot colour, then not
    Bytes bytes = file(2, 1, 32, {
        frame(100, { layerChunk("Layer 1", 1, 0, 0, 0, 255), paletteChunk(palette),
                     rawCel(0, 0, 0, 2, 1, pixels) }) });
    AseFile ase;
    std::string error;
    REQUIRE(parseAseprite(bytes, &ase, &error));
    Document doc;
    REQUIRE(documentFromAseprite(doc, ase, "rgba", nullptr, &error));
    CHECK(same(pixel(doc, doc.sprite(), 1, 0), ls::Color{ 99, 98, 97, 255 }));
    doc.beginAction("slot");
    REQUIRE(setPaletteEntry(doc, 0, ls::Color{ 1, 1, 1, 255 }));
    doc.endAction();
    CHECK(same(pixel(doc, doc.sprite(), 0, 0), ls::Color{ 1, 1, 1, 255 }));
    CHECK(same(pixel(doc, doc.sprite(), 1, 0), ls::Color{ 99, 98, 97, 255 }));
}

void testGroupsAndHiddenLayers() {
    Bytes one = { 255, 0, 0, 255 };
    Bytes bytes = file(1, 1, 32, {
        frame(100, { layerChunk("body", 1, 1, 0, 0, 255),          // a group
                     layerChunk("arm", 1, 0, 1, 1, 255),           // in it, multiply
                     layerChunk("ghost", 0, 0, 0, 0, 255),         // hidden
                     rawCel(1, 0, 0, 1, 1, one), rawCel(2, 0, 0, 1, 1, one) }) });
    AseFile ase;
    std::string error;
    REQUIRE(parseAseprite(bytes, &ase, &error));
    Document doc;
    AsepriteReport report;
    REQUIRE(documentFromAseprite(doc, ase, "groups", &report, &error));
    auto info = doc.engine().getSpriteInfo(doc.sprite());
    REQUIRE(info.ok() && info.value.layers.size() == 2);        // the group is not a layer
    LayerProps arm, ghost;
    REQUIRE(readLayerProps(doc, info.value.layers[0], &arm));
    REQUIRE(readLayerProps(doc, info.value.layers[1], &ghost));
    CHECK(arm.group.valid() && arm.blend == ls::BlendMode::Multiply);
    CHECK(!ghost.visible);
}

void testDamageIsRefused() {
    Bytes good = indexedSprite();
    AseFile ase;
    std::string error;
    Bytes cut(good.begin(), good.begin() + static_cast<long long>(good.size() / 2));
    CHECK(!parseAseprite(cut, &ase, &error) && !error.empty());
    CHECK(!parseAseprite({ 1, 2, 3 }, &ase, &error));
    Bytes wrongMagic = good;
    wrongMagic[4] = 0;
    CHECK(!parseAseprite(wrongMagic, &ase, &error));
    // A compressed cel whose stream does not decode to its size.
    Bytes badCel = file(4, 4, 8, { frame(100, { layerChunk("a", 1, 0, 0, 0, 255),
        chunk(0x2005, [] { Bytes b = celHead(0, 0, 0, 2); u16(b, 4); u16(b, 4);
                           b.push_back(0x78); b.push_back(0x9C); b.push_back(1); return b; }()) }) });
    CHECK(!parseAseprite(badCel, &ase, &error));
}

} // namespace

int main() {
    testIndexedCelsLandAndBecomeSlots();
    testRgbaPixelsMatchSlotsWhereTheyCan();
    testGroupsAndHiddenLayers();
    testDamageIsRefused();
    if (failures == 0) {
        std::printf("aseprite: all passed\n");
        return 0;
    }
    std::printf("aseprite: %d failure(s)\n", failures);
    return 1;
}
