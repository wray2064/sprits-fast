// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors
#pragma once

// grid_snap.h — putting things on the grid.
//
// With snapping on, what is placed by dragging lands on the tile grid: a
// marquee covers whole cells, a shape's corners sit on grid lines, and a
// moved selection's top-left corner steps from grid point to grid point.
// Grid lines are pixel boundaries -- x = offsetX + k * width -- so a corner
// "on the grid" is between pixels, never on one.

#include <livesprite/livesprite.h>

#include <vector>

namespace fast {

struct Grid {
    int width = 16;
    int height = 16;
    int offsetX = 0;
    int offsetY = 0;
    // Guides: lines between columns (x) and rows (y) that snap as well as the
    // grid's own, whichever is nearer.
    std::vector<int32_t> linesX;
    std::vector<int32_t> linesY;
};

// The grid point nearest a position in canvas coordinates (a pixel's centre
// is x + 0.5). Ties go to the lower line.
ls::Vec2i nearestGridPoint(ls::Vec2f at, const Grid& grid);

// The cell a pixel is in, as pixels [min, max).
ls::Rect2i cellAt(ls::Vec2i pixel, const Grid& grid);

// A marquee dragged from `anchor` to `pointer`, snapped: its corners on the
// grid points nearest each end. Never empty -- where both ends snap to the
// same line, the box takes the cell the anchor is in on that axis, so a
// click-and-drag inside one cell selects that cell.
ls::Rect2i snappedBox(ls::Vec2f anchor, ls::Vec2f pointer, const Grid& grid);

// A move of something whose top-left corner is `topLeft`, by `delta`,
// changed so the corner lands on the grid point nearest where it would have.
ls::Vec2i snappedMove(ls::Vec2i topLeft, ls::Vec2i delta, const Grid& grid);

} // namespace fast
