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
#include "app/tilemap.h"
#include "app/zlib.h"
#include "ase_builder.h"

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
using namespace asebuild;


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

// A tilemap: a tileset of 4x4 tiles (tile 0 empty, tile 1 a red corner and a
// blue row) and a 2x2 grid naming tile 1 plainly and flipped across. It comes
// in as a tilemap layer over a tileset, each cell its tile.
void testTilemapsComeInAsTilemaps() {
    const ls::Color red { 220, 30, 30, 255 };
    const ls::Color blue { 30, 30, 220, 255 };
    Bytes tiles(4 * 8 * 4, 0);                        // two 4x4 RGBA tiles, stacked
    const auto put = [&tiles](int x, int y, ls::Color c) {
        uint8_t* p = tiles.data() + (static_cast<size_t>(y) * 4 + x) * 4;
        p[0] = c.r; p[1] = c.g; p[2] = c.b; p[3] = c.a;
    };
    put(0, 4, red);                                   // tile 1 starts at row 4
    for (int x = 0; x < 4; ++x) { put(x, 6, blue); }
    Bytes packedTiles;
    zlibDeflate(tiles, &packedTiles);
    Bytes set;
    u32(set, 0); u32(set, 2 | 4); u32(set, 2);        // id, tiles inside and 0 empty, count
    u16(set, 4); u16(set, 4); u16(set, 1); zeros(set, 14);
    text(set, "ground");
    u32(set, static_cast<uint32_t>(packedTiles.size()));
    set.insert(set.end(), packedTiles.begin(), packedTiles.end());

    Bytes layer;
    u16(layer, 1); u16(layer, 2); u16(layer, 0); u16(layer, 0); u16(layer, 0);
    u16(layer, 0); u8(layer, 255); zeros(layer, 3); text(layer, "map");
    u32(layer, 0);                                    // its tileset

    Bytes cells;
    for (uint32_t v : { 1u, 0u, 0u, 1u | 0x20000000u }) { u32(cells, v); }
    Bytes packedCells;
    zlibDeflate(cells, &packedCells);
    Bytes cel = celHead(0, 0, 0, 3);
    u16(cel, 2); u16(cel, 2); u16(cel, 32);
    u32(cel, 0x1fffffff); u32(cel, 0x20000000); u32(cel, 0x40000000); u32(cel, 0x80000000);
    zeros(cel, 10);
    cel.insert(cel.end(), packedCells.begin(), packedCells.end());

    const Bytes bytes = file(8, 8, 32, { frame(100, {
        chunk(0x2023, set), chunk(0x2004, layer), chunk(0x2005, cel) }) });
    AseFile ase;
    std::string error;
    REQUIRE(parseAseprite(bytes, &ase, &error));
    REQUIRE(ase.tilesets.size() == 1 && ase.layers.size() == 1 && ase.layers[0].tileset == 0);
    Document doc;
    AsepriteReport report;
    REQUIRE(documentFromAseprite(doc, ase, "tiles", &report, &error));
    CHECK(!report.skippedTilemaps);
    auto info = doc.engine().getSpriteInfo(doc.sprite());
    REQUIRE(info.ok() && info.value.layers.size() == 1);
    CHECK(isTilemapLayer(doc, info.value.layers[0]));
    CHECK(same(pixel(doc, doc.sprite(), 0, 0), red));       // cell (0,0): tile 1
    CHECK(same(pixel(doc, doc.sprite(), 2, 2), blue));
    CHECK(pixel(doc, doc.sprite(), 4, 0).a == 0);           // cell (1,0): empty
    CHECK(same(pixel(doc, doc.sprite(), 7, 4), red));       // cell (1,1): flipped across
    CHECK(pixel(doc, doc.sprite(), 4, 4).a == 0);
    CHECK(same(pixel(doc, doc.sprite(), 5, 6), blue));
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
    testTilemapsComeInAsTilemaps();
    testDamageIsRefused();
    if (failures == 0) {
        std::printf("aseprite: all passed\n");
        return 0;
    }
    std::printf("aseprite: %d failure(s)\n", failures);
    return 1;
}
