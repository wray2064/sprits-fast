// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

// tile_tools.h — a tilemap layer in the window.
//
// Two ways to work on one, as in Aseprite. Drawing pixels: the pencil, spray
// and eraser draw into the tiles under them -- every cell naming a tile shows
// the stroke -- and an empty cell drawn into gets a new tile of its own.
// Placing tiles: the pencil puts the chosen tile, turned as chosen, into the
// cells it passes over, the eraser empties them, the picker takes a cell's
// tile, the bucket fills the cells alike round the one clicked.

#pragma once

#include "ui/editor.h"

#include <imgui.h>

#include <vector>

namespace fast {

class CanvasView;

// The active layer as a tilemap, when it is one.
bool activeTilemap(Editor& editor, TilemapLayer* out);

// The pointer on a tilemap layer: placing tiles, or telling a tool that
// cannot draw there why. True when it took the pointer this frame.
bool handleTilemapStroke(Editor& editor, CanvasView& canvas, bool overCanvas, ls::Vec2i pixel);

// Drawing pixels: a stroke's pixels into the tiles under them, and the
// stroke's end.
void strokeTiles(Editor& editor, const std::vector<ls::Vec2i>& run);
void endTileStroke(Editor& editor);

// The grid over the canvas, and the tile a placing would put down.
void drawTilemapOverlay(Editor& editor, CanvasView& canvas, ImDrawList* draw, ImVec2 origin,
                        float zoom);

// The Element panel's section for a tilemap layer: the mode, the tiles, the
// turn of the tile to place.
void drawTilesSection(Editor& editor, CanvasView& canvas);

// Sprite > New tilemap layer.
void drawTilemapDialog(Editor& editor, CanvasView& canvas);

} // namespace fast
