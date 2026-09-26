// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

// tilemap_tests.cpp — layers drawn as a grid of tiles.
//
// The promises: a tileset is never a frame and stays after the frames
// whatever is done to them; a tilemap layer draws its cells' tiles, turned
// as each cell says, and a pixel of the canvas maps back to the tile pixel it
// shows; an edit to a tile shows in every cell naming it; a duplicated frame
// gets its own cells; the canvas flipped, turned and resized takes the
// tilemaps with it, picture for picture; and scaling is refused, since tiles
// keep their size.

#include "app/animation.h"
#include "app/canvas_ops.h"
#include "app/document.h"
#include "app/ink.h"
#include "app/layers.h"
#include "app/paint.h"
#include "app/tilemap.h"

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

const ls::Color kRed   { 200, 30, 30, 255 };
const ls::Color kBlue  { 30, 30, 200, 255 };

ls::RasterBuffer picture(Document& doc, ls::SpriteId sprite) {
    auto size = doc.engine().getCanvasSize(doc.id());
    auto compiled = doc.engine().compileSprite(
        sprite, compileProfile(ls::CompileProfileType::Export, static_cast<uint32_t>(size.value.x),
                               static_cast<uint32_t>(size.value.y)));
    return compiled.ok() ? compiled.value.raster : ls::RasterBuffer{};
}

ls::Color at(Document& doc, ls::SpriteId sprite, int x, int y) {
    return ls::readPixel(picture(doc, sprite), x, y);
}

bool same(ls::Color a, ls::Color b) {
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

bool paintTile(Document& doc, ls::SpriteId tileset, uint32_t tile, ls::Color colour,
               const std::vector<ls::Vec2i>& pixels) {
    Ink ink;
    ink.colour = colour;
    InkStroke stroke;
    return beginInkStroke(doc, tileLayer(doc, tileset, tile), ink, &stroke) &&
           strokeInk(doc, stroke, pixels);
}

// A 16 x 8 document; a 4 x 4 tileset of two tiles -- tile 1 a red pixel in
// its top-left corner and a blue one beside it, tile 2 solid blue; a tilemap
// layer on the frame.
struct Scene {
    Document doc;
    ls::SpriteId frame;
    ls::SpriteId tileset;
    ls::LayerId layer;

    bool build() {
        if (!doc.create("tiles", 16, 8)) { return false; }
        frame = doc.sprite();
        doc.beginAction("Tiles");
        tileset = createTileset(doc, 4, 4);
        if (!tileset.valid() || addTile(doc, tileset) != 1 || addTile(doc, tileset) != 2) {
            return false;
        }
        std::vector<ls::Vec2i> all;
        for (int y = 0; y < 4; ++y) { for (int x = 0; x < 4; ++x) { all.push_back({ x, y }); } }
        if (!paintTile(doc, tileset, 1, kRed, {{ 0, 0 }}) ||
            !paintTile(doc, tileset, 1, kBlue, {{ 1, 0 }}) ||
            !paintTile(doc, tileset, 2, kBlue, all)) {
            return false;
        }
        layer = createTilemapLayer(doc, frame, tileset, "Tiles", -1);
        doc.endAction();
        doc.clearHistory();
        return layer.valid();
    }

    TilemapLayer map() {
        TilemapLayer read;
        readTilemapLayer(doc, layer, &read);
        return read;
    }

    bool set(int column, int row, uint32_t value) {
        TilemapLayer m = map();
        doc.beginAction("Place");
        const bool ok = setCell(doc, m, { column, row }, value);
        doc.endAction();
        return ok;
    }
};

void testATilesetIsNotAFrame() {
    Scene s;
    REQUIRE(s.build());
    CHECK(readFrames(s.doc).size() == 1);
    CHECK(isTileset(s.doc, s.tileset) && !isTileset(s.doc, s.frame));
    CHECK(tileCount(s.doc, s.tileset) == 2);
    // Frames added and moved: the tileset stays after them all.
    REQUIRE(addFrame(s.doc, 0) == 1);
    REQUIRE(moveFrame(s.doc, 1, 0));
    auto info = s.doc.engine().getDocumentInfo(s.doc.id());
    REQUIRE(info.ok() && info.value.sprites.size() == 3);
    CHECK(info.value.sprites.back() == s.tileset);
    CHECK(readFrames(s.doc).size() == 2);
}

void testCellsDrawTheirTilesTurned() {
    Scene s;
    REQUIRE(s.build());
    TilemapLayer m = s.map();
    CHECK(m.grid.columns == 4 && m.grid.rows == 2);     // the grid covers the canvas
    REQUIRE(s.set(0, 0, 1));
    REQUIRE(s.set(1, 0, 1 | ls::kTileFlipX));
    REQUIRE(s.set(2, 1, 1 | ls::kTileFlipD));
    CHECK(same(at(s.doc, s.frame, 0, 0), kRed) && same(at(s.doc, s.frame, 1, 0), kBlue));
    CHECK(same(at(s.doc, s.frame, 7, 0), kRed) && same(at(s.doc, s.frame, 6, 0), kBlue));
    CHECK(same(at(s.doc, s.frame, 8, 4), kRed) && same(at(s.doc, s.frame, 8, 5), kBlue));
    CHECK(at(s.doc, s.frame, 12, 0).a == 0);

    // Back from the canvas to the tile: the turns undone.
    m = s.map();
    uint32_t tile = 0;
    ls::Vec2i local;
    CHECK(tilePixel(m, { 6, 0 }, &tile, &local) && tile == 1 && local.x == 1 && local.y == 0);
    CHECK(tilePixel(m, { 8, 5 }, &tile, &local) && local.x == 1 && local.y == 0);
    CHECK(!tilePixel(m, { 12, 0 }, &tile, &local));

    // An edit to the tile shows in every cell naming it.
    REQUIRE(paintTile(s.doc, s.tileset, 1, kBlue, {{ 3, 3 }}));
    CHECK(same(at(s.doc, s.frame, 3, 3), kBlue) && same(at(s.doc, s.frame, 4, 3), kBlue));
    CHECK(cellsNaming(s.doc, s.tileset, 1) == 3);

    // One undo takes a placing back: the last two here.
    REQUIRE(s.doc.undo());
    REQUIRE(s.doc.undo());
    CHECK(at(s.doc, s.frame, 8, 4).a == 0 && at(s.doc, s.frame, 7, 0).a == 0);
    CHECK(same(at(s.doc, s.frame, 0, 0), kRed));
}

void testADuplicatedFrameHasItsOwnCells() {
    Scene s;
    REQUIRE(s.build());
    REQUIRE(s.set(0, 0, 1));
    REQUIRE(duplicateFrame(s.doc, 0) == 1);
    const std::vector<Frame> frames = readFrames(s.doc);
    REQUIRE(frames.size() == 2);
    const std::vector<ls::LayerId> layers = layerOrder(s.doc, frames[1].sprite);
    REQUIRE(!layers.empty());
    TilemapLayer copy;
    REQUIRE(readTilemapLayer(s.doc, layers.back(), &copy));
    CHECK(copy.map != s.map().map && copy.tileset == s.tileset);
    CHECK(same(at(s.doc, frames[1].sprite, 0, 0), kRed));
    s.doc.beginAction("Place");
    REQUIRE(setCell(s.doc, copy, { 0, 0 }, 2));
    s.doc.endAction();
    CHECK(same(at(s.doc, frames[1].sprite, 0, 0), kBlue));
    CHECK(same(at(s.doc, frames[0].sprite, 0, 0), kRed));
}

void testTheCanvasTakesTheTilemapsWithIt() {
    Scene s;
    REQUIRE(s.build());
    REQUIRE(s.set(0, 0, 1));
    REQUIRE(s.set(3, 1, 1 | ls::kTileFlipY));
    const ls::RasterBuffer before = picture(s.doc, s.frame);
    std::string error;

    // Flipped: the picture flipped, pixel for pixel.
    REQUIRE(flipCanvas(s.doc, true, &error));
    const ls::RasterBuffer flipped = picture(s.doc, s.frame);
    bool mirrored = true;
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 16; ++x) {
            mirrored = mirrored && same(ls::readPixel(flipped, x, y), ls::readPixel(before, 15 - x, y));
        }
    }
    CHECK(mirrored);
    REQUIRE(flipCanvas(s.doc, true, &error));
    CHECK(picture(s.doc, s.frame).pixels == before.pixels);

    // A quarter turn, and four of them back to the start.
    REQUIRE(rotateCanvas(s.doc, 1, &error));
    const ls::RasterBuffer turned = picture(s.doc, s.frame);
    REQUIRE(turned.width == 8 && turned.height == 16);
    bool rotated = true;
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 16; ++x) {
            // Clockwise: (x, y) goes to (height - 1 - y, x).
            rotated = rotated && same(ls::readPixel(turned, 7 - y, x), ls::readPixel(before, x, y));
        }
    }
    CHECK(rotated);
    REQUIRE(rotateCanvas(s.doc, 1, &error));
    REQUIRE(rotateCanvas(s.doc, 1, &error));
    REQUIRE(rotateCanvas(s.doc, 1, &error));
    CHECK(picture(s.doc, s.frame).pixels == before.pixels);

    // A bigger canvas, centred: the tiles move with the drawing and the grid
    // grows to cover it all.
    REQUIRE(resizeCanvas(s.doc, 24, 16, CanvasAnchor::Centre, &error));
    CHECK(same(at(s.doc, s.frame, 4, 4), kRed));
    const TilemapLayer grown = s.map();
    CHECK(grown.origin.x <= 0 && grown.origin.y <= 0);
    CHECK(grown.origin.x + static_cast<int>(grown.grid.columns * 4) >= 24);
    CHECK(grown.origin.y + static_cast<int>(grown.grid.rows * 4) >= 16);

    // Scaled: refused, and said why.
    error.clear();
    CHECK(!enlargeSprite(s.doc, 2, &error) && !error.empty());
}

void testTurnsCompose() {
    const int clockwise[4] = { 0, -1, 1, 0 };
    const int across[4] = { -1, 0, 0, 1 };
    uint32_t cell = 5;
    for (int i = 0; i < 4; ++i) {
        cell = turnedCell(cell, clockwise);
    }
    CHECK(cell == 5);
    CHECK(turnedCell(turnedCell(5, across), across) == 5);
    CHECK(turnedCell(5, clockwise) == (5u | ls::kTileFlipD | ls::kTileFlipX));
    CHECK(turnedCell(0, clockwise) == 0);
}

} // namespace

int main() {
    testATilesetIsNotAFrame();
    testCellsDrawTheirTilesTurned();
    testADuplicatedFrameHasItsOwnCells();
    testTheCanvasTakesTheTilemapsWithIt();
    testTurnsCompose();
    if (failures == 0) {
        std::printf("tilemap: all passed\n");
        return 0;
    }
    std::printf("tilemap: %d failure(s)\n", failures);
    return 1;
}
