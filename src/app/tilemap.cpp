// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

#include "app/tilemap.h"

#include "app/animation.h"
#include "app/layers.h"
#include "app/paint.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <utility>

namespace fast {

namespace {

constexpr const char* kTilesetKey = "fast.tileset";   // "16x16": the tile size

bool parseSize(const std::string& text, uint32_t* w, uint32_t* h) {
    unsigned a = 0;
    unsigned b = 0;
    char x = 0;
    // Written by us as WxH; anything else is not a tileset of ours.
    size_t at = 0;
    while (at < text.size() && text[at] >= '0' && text[at] <= '9') {
        a = a * 10u + static_cast<unsigned>(text[at] - '0');
        ++at;
    }
    if (at >= text.size()) {
        return false;
    }
    x = text[at++];
    const size_t start = at;
    while (at < text.size() && text[at] >= '0' && text[at] <= '9') {
        b = b * 10u + static_cast<unsigned>(text[at] - '0');
        ++at;
    }
    if (x != 'x' || at == start || at != text.size() || a == 0 || b == 0 || a > 1024 || b > 1024) {
        return false;
    }
    *w = a;
    *h = b;
    return true;
}

// A cell's turns as a matrix, row by row: the diagonal first, then
// left-right, then top-bottom -- Y . X . D.
void matrixOf(uint32_t cell, int m[4]) {
    const int sx = (cell & ls::kTileFlipX) != 0 ? -1 : 1;
    const int sy = (cell & ls::kTileFlipY) != 0 ? -1 : 1;
    if ((cell & ls::kTileFlipD) != 0) {
        m[0] = 0;  m[1] = sx;
        m[2] = sy; m[3] = 0;
    } else {
        m[0] = sx; m[1] = 0;
        m[2] = 0;  m[3] = sy;
    }
}

uint32_t bitsOf(const int m[4]) {
    uint32_t bits = 0;
    if (m[1] == 0) {                      // no diagonal
        if (m[0] < 0) { bits |= ls::kTileFlipX; }
        if (m[3] < 0) { bits |= ls::kTileFlipY; }
    } else {                              // M . D is the diagonal part
        bits |= ls::kTileFlipD;
        if (m[1] < 0) { bits |= ls::kTileFlipX; }
        if (m[2] < 0) { bits |= ls::kTileFlipY; }
    }
    return bits;
}

} // namespace

// ---------------------------------------------------------------- tilesets --

bool isTileset(Document& doc, ls::SpriteId sprite) {
    auto value = doc.engine().getMetadata(sprite.value, kTilesetKey);
    uint32_t w = 0;
    uint32_t h = 0;
    return value.ok() && parseSize(value.value, &w, &h);
}

std::vector<ls::SpriteId> tilesetsOf(Document& doc) {
    std::vector<ls::SpriteId> out;
    auto info = doc.engine().getDocumentInfo(doc.id());
    if (info.fail()) {
        return out;
    }
    for (ls::SpriteId sprite : info.value.sprites) {
        if (isTileset(doc, sprite)) {
            out.push_back(sprite);
        }
    }
    return out;
}

ls::SpriteId createTileset(Document& doc, uint32_t width, uint32_t height) {
    if (width == 0 || height == 0 || width > 1024 || height > 1024) {
        return ls::SpriteId{};
    }
    auto made = doc.engine().createSprite(doc.id());
    if (made.fail()) {
        return ls::SpriteId{};
    }
    char size[32];
    std::snprintf(size, sizeof(size), "%ux%u", width, height);
    doc.engine().setMetadata(made.value.value, kTilesetKey, size);
    return made.value;
}

bool tileSizeOf(Document& doc, ls::SpriteId tileset, uint32_t* width, uint32_t* height) {
    auto value = doc.engine().getMetadata(tileset.value, kTilesetKey);
    return value.ok() && parseSize(value.value, width, height);
}

size_t tileCount(Document& doc, ls::SpriteId tileset) {
    return layerOrder(doc, tileset).size();
}

ls::LayerId tileLayer(Document& doc, ls::SpriteId tileset, uint32_t tile) {
    const std::vector<ls::LayerId> tiles = layerOrder(doc, tileset);
    if (tile == 0 || tile > tiles.size()) {
        return ls::LayerId{};
    }
    return tiles[tile - 1];
}

uint32_t addTile(Document& doc, ls::SpriteId tileset) {
    const size_t next = tileCount(doc, tileset) + 1;
    PaintLayer made;
    if (!createPaintLayer(doc, tileset, "Tile " + std::to_string(next),
                          ls::Color{ 200, 200, 200, 255 }, &made)) {
        return 0;
    }
    return static_cast<uint32_t>(tileCount(doc, tileset));
}

uint32_t duplicateTile(Document& doc, ls::SpriteId tileset, uint32_t tile) {
    const ls::LayerId source = tileLayer(doc, tileset, tile);
    if (!source.valid()) {
        return 0;
    }
    auto copy = doc.engine().cloneLayer(source, tileset, -1);
    if (copy.fail()) {
        return 0;
    }
    doc.engine().setLayerName(copy.value,
                              "Tile " + std::to_string(tileCount(doc, tileset)));
    return static_cast<uint32_t>(tileCount(doc, tileset));
}

// ----------------------------------------------------------- tilemap layers --

bool readTilemapLayer(Document& doc, ls::LayerId layer, TilemapLayer* out) {
    ls::LSContext& engine = doc.engine();
    auto operations = engine.getLayerOperations(layer);
    if (operations.fail()) {
        return false;
    }
    for (const ls::OperationInfo& op : operations.value) {
        if (op.type != "DrawTilemapOp") {
            continue;
        }
        auto map = engine.getOperationParameter(op.id, "tilemap");
        auto set = engine.getOperationParameter(op.id, "tileset");
        auto origin = engine.getOperationParameter(op.id, "origin");
        const uint64_t* mapId = map.ok() ? std::get_if<uint64_t>(&map.value) : nullptr;
        const uint64_t* setId = set.ok() ? std::get_if<uint64_t>(&set.value) : nullptr;
        if (mapId == nullptr || setId == nullptr) {
            return false;
        }
        auto grid = engine.getTilemap(ls::TilemapId{ *mapId });
        if (grid.fail()) {
            return false;
        }
        TilemapLayer read;
        read.layer = layer;
        read.draw = op.id;
        read.map = ls::TilemapId{ *mapId };
        read.tileset = ls::SpriteId{ *setId };
        if (const ls::Vec2f* o = origin.ok() ? std::get_if<ls::Vec2f>(&origin.value) : nullptr) {
            read.origin = { static_cast<int32_t>(std::lround(o->x)),
                            static_cast<int32_t>(std::lround(o->y)) };
        }
        read.grid = std::move(grid.value);
        *out = std::move(read);
        return true;
    }
    return false;
}

bool isTilemapLayer(Document& doc, ls::LayerId layer) {
    TilemapLayer read;
    return readTilemapLayer(doc, layer, &read);
}

namespace {

bool drawTilemapOn(Document& doc, ls::LayerId layer, ls::SpriteId tileset, ls::Vec2i origin,
                   const ls::TilemapDesc& grid) {
    ls::LSContext& engine = doc.engine();
    auto map = engine.createTilemap(doc.id(), grid);
    if (map.fail()) {
        return false;
    }
    ls::DrawTilemapOp draw;
    draw.tilemap = map.value;
    draw.tileset = tileset;
    draw.origin = { static_cast<float>(origin.x), static_cast<float>(origin.y) };
    if (engine.addOperation(layer, draw).fail()) {
        engine.deleteTilemap(map.value);
        return false;
    }
    return true;
}

} // namespace

ls::LayerId createTilemapLayer(Document& doc, ls::SpriteId sprite, ls::SpriteId tileset,
                               const std::string& name, int atIndex) {
    uint32_t tw = 0;
    uint32_t th = 0;
    auto size = doc.engine().getCanvasSize(doc.id());
    if (!tileSizeOf(doc, tileset, &tw, &th) || size.fail()) {
        return ls::LayerId{};
    }
    ls::TilemapDesc grid;
    grid.tileWidth = tw;
    grid.tileHeight = th;
    grid.columns = (static_cast<uint32_t>(size.value.x) + tw - 1) / tw;
    grid.rows = (static_cast<uint32_t>(size.value.y) + th - 1) / th;
    grid.cells.assign(static_cast<size_t>(grid.columns) * grid.rows, 0u);
    ls::LayerDesc desc;
    desc.name = name;
    auto layer = doc.engine().createLayer(sprite, desc);
    if (layer.fail()) {
        return ls::LayerId{};
    }
    if (!drawTilemapOn(doc, layer.value, tileset, { 0, 0 }, grid)) {
        doc.engine().deleteLayer(layer.value);
        return ls::LayerId{};
    }
    if (atIndex >= 0) {
        moveLayer(doc, layer.value, atIndex);
    }
    return layer.value;
}

bool makeTilemapLike(Document& doc, ls::LayerId layer, const TilemapLayer& like) {
    ls::TilemapDesc grid = like.grid;
    std::fill(grid.cells.begin(), grid.cells.end(), 0u);
    return drawTilemapOn(doc, layer, like.tileset, like.origin, grid);
}

bool cellAt(const TilemapLayer& map, ls::Vec2i pixel, ls::Vec2i* cell) {
    const int32_t tw = static_cast<int32_t>(map.grid.tileWidth);
    const int32_t th = static_cast<int32_t>(map.grid.tileHeight);
    const int32_t x = pixel.x - map.origin.x;
    const int32_t y = pixel.y - map.origin.y;
    if (x < 0 || y < 0 || tw <= 0 || th <= 0) {
        return false;
    }
    const int32_t c = x / tw;
    const int32_t r = y / th;
    if (c >= static_cast<int32_t>(map.grid.columns) || r >= static_cast<int32_t>(map.grid.rows)) {
        return false;
    }
    *cell = { c, r };
    return true;
}

uint32_t cellValue(const TilemapLayer& map, ls::Vec2i cell) {
    if (cell.x < 0 || cell.y < 0 || cell.x >= static_cast<int32_t>(map.grid.columns) ||
        cell.y >= static_cast<int32_t>(map.grid.rows)) {
        return 0;
    }
    return map.grid.cells[static_cast<size_t>(cell.y) * map.grid.columns +
                          static_cast<size_t>(cell.x)];
}

bool setCell(Document& doc, TilemapLayer& map, ls::Vec2i cell, uint32_t value) {
    if (cell.x < 0 || cell.y < 0 || cell.x >= static_cast<int32_t>(map.grid.columns) ||
        cell.y >= static_cast<int32_t>(map.grid.rows)) {
        return false;
    }
    if (doc.engine().setTilemapCell(map.map, static_cast<uint32_t>(cell.x),
                                    static_cast<uint32_t>(cell.y), value).fail()) {
        return false;
    }
    map.grid.cells[static_cast<size_t>(cell.y) * map.grid.columns + static_cast<size_t>(cell.x)] =
        value;
    return true;
}

bool tilePixel(const TilemapLayer& map, ls::Vec2i pixel, uint32_t* tile, ls::Vec2i* local) {
    ls::Vec2i cell;
    if (!cellAt(map, pixel, &cell)) {
        return false;
    }
    const uint32_t value = cellValue(map, cell);
    if ((value & ls::kTileIndexMask) == 0) {
        return false;
    }
    const int32_t tw = static_cast<int32_t>(map.grid.tileWidth);
    const int32_t th = static_cast<int32_t>(map.grid.tileHeight);
    const int32_t u = pixel.x - map.origin.x - cell.x * tw;
    const int32_t v = pixel.y - map.origin.y - cell.y * th;
    // As the engine draws it, backwards: the flips undone, then the diagonal.
    int32_t su = (value & ls::kTileFlipX) != 0 ? tw - 1 - u : u;
    int32_t sv = (value & ls::kTileFlipY) != 0 ? th - 1 - v : v;
    if ((value & ls::kTileFlipD) != 0 && tw == th) {
        std::swap(su, sv);
    }
    *tile = value & ls::kTileIndexMask;
    *local = { su, sv };
    return true;
}

uint32_t turnedCell(uint32_t cell, const int m[4]) {
    const uint32_t tile = cell & ls::kTileIndexMask;
    if (tile == 0) {
        return 0;
    }
    int t[4];
    matrixOf(cell, t);
    const int r[4] = { m[0] * t[0] + m[1] * t[2], m[0] * t[1] + m[1] * t[3],
                       m[2] * t[0] + m[3] * t[2], m[2] * t[1] + m[3] * t[3] };
    return tile | bitsOf(r);
}

size_t cellsNaming(Document& doc, ls::SpriteId tileset, uint32_t tile) {
    size_t count = 0;
    for (const Frame& frame : readFrames(doc)) {
        for (ls::LayerId layer : layerOrder(doc, frame.sprite)) {
            TilemapLayer map;
            if (!readTilemapLayer(doc, layer, &map) || map.tileset != tileset) {
                continue;
            }
            for (uint32_t cell : map.grid.cells) {
                count += (cell & ls::kTileIndexMask) == tile ? 1u : 0u;
            }
        }
    }
    return count;
}

} // namespace fast
