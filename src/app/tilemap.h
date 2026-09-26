// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

// tilemap.h — layers drawn as a grid of tiles.
//
// A tileset is a sprite of the document that is not a frame. Its layers are
// its tiles: tile 1 is its first layer, drawn from its own top-left corner,
// tileWidth x tileHeight pixels. A tilemap layer is a layer whose one
// operation is the engine's DrawTilemapOp: a grid of cells over the canvas,
// each naming a tile, turned or not. Each frame's tilemap layer has a grid of
// its own; the tiles are shared, so an edit to a tile's pixels shows in every
// cell of every frame that names it -- the point of tiles.

#pragma once

#include "app/document.h"

#include <livesprite/livesprite.h>

#include <cstdint>
#include <string>
#include <vector>

namespace fast {

struct TilemapLayer {
    ls::LayerId     layer;
    ls::OperationId draw;
    ls::TilemapId   map;
    ls::SpriteId    tileset;
    ls::Vec2i       origin { 0, 0 };
    ls::TilemapDesc grid;
};

// ---------------------------------------------------------------- tilesets --

bool isTileset(Document& doc, ls::SpriteId sprite);
std::vector<ls::SpriteId> tilesetsOf(Document& doc);

// A new tileset with tiles `width` x `height` and no tiles yet, after every
// frame. Does not bracket an action.
ls::SpriteId createTileset(Document& doc, uint32_t width, uint32_t height);
bool tileSizeOf(Document& doc, ls::SpriteId tileset, uint32_t* width, uint32_t* height);

size_t tileCount(Document& doc, ls::SpriteId tileset);
// Tile `tile`'s layer, counting from 1; invalid when there is no such tile.
ls::LayerId tileLayer(Document& doc, ls::SpriteId tileset, uint32_t tile);
// A new empty tile at the end; its number, or 0. Does not bracket an action.
uint32_t addTile(Document& doc, ls::SpriteId tileset);
// A new tile holding what `tile` holds. Its number, or 0.
uint32_t duplicateTile(Document& doc, ls::SpriteId tileset, uint32_t tile);

// ----------------------------------------------------------- tilemap layers --

bool readTilemapLayer(Document& doc, ls::LayerId layer, TilemapLayer* out);
bool isTilemapLayer(Document& doc, ls::LayerId layer);

// A tilemap layer on `sprite` at `atIndex` (-1 on top), named `name`, drawing
// from `tileset`: a grid of empty cells covering the canvas. Does not bracket
// an action.
ls::LayerId createTilemapLayer(Document& doc, ls::SpriteId sprite, ls::SpriteId tileset,
                               const std::string& name, int atIndex);

// Makes an empty layer a tilemap layer like `like`: the same tileset, grid
// and place, its own cells all empty -- a new frame's cel of a tilemap track.
bool makeTilemapLike(Document& doc, ls::LayerId layer, const TilemapLayer& like);

// The cell a canvas pixel is in; false outside the grid.
bool cellAt(const TilemapLayer& map, ls::Vec2i pixel, ls::Vec2i* cell);
uint32_t cellValue(const TilemapLayer& map, ls::Vec2i cell);
// One cell set; the grid held in `map` is kept up to date. Does not bracket.
bool setCell(Document& doc, TilemapLayer& map, ls::Vec2i cell, uint32_t value);

// Where a canvas pixel falls in the tile its cell names -- the tile's number
// and the pixel in the tile's own space, the cell's turns undone. False for
// an empty cell or outside the grid.
bool tilePixel(const TilemapLayer& map, ls::Vec2i pixel, uint32_t* tile, ls::Vec2i* local);

// A cell's turns, composed with one more: the linear part of a flip or a
// quarter turn as a 2 x 2 matrix of -1, 0 and 1, row by row. The tile number
// is kept.
uint32_t turnedCell(uint32_t cell, const int m[4]);

// How many cells of every tilemap layer, in every frame, name `tile`.
size_t cellsNaming(Document& doc, ls::SpriteId tileset, uint32_t tile);

} // namespace fast
