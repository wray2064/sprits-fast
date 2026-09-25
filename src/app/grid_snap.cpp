// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

#include "app/grid_snap.h"

#include <algorithm>
#include <cmath>

namespace fast {

namespace {

// The grid line nearest `at` on one axis: offset + k * step.
int32_t nearestLine(float at, int step, int offset) {
    const int s = std::max(step, 1);
    const float exact = (at - static_cast<float>(offset)) / static_cast<float>(s);
    const float below = std::floor(exact);
    // Exactly half way goes to the lower line.
    const float k = exact - below <= 0.5f ? below : below + 1.f;
    return offset + static_cast<int32_t>(k) * s;
}

// The start of the cell containing pixel `p` on one axis.
int32_t cellStart(int32_t p, int step, int offset) {
    const int s = std::max(step, 1);
    const int32_t rel = p - offset;
    const int32_t k = rel >= 0 ? rel / s : -((-rel + s - 1) / s);
    return offset + k * s;
}

} // namespace

ls::Vec2i nearestGridPoint(ls::Vec2f at, const Grid& grid) {
    return { nearestLine(at.x, grid.width, grid.offsetX),
             nearestLine(at.y, grid.height, grid.offsetY) };
}

ls::Rect2i cellAt(ls::Vec2i pixel, const Grid& grid) {
    const int32_t x = cellStart(pixel.x, grid.width, grid.offsetX);
    const int32_t y = cellStart(pixel.y, grid.height, grid.offsetY);
    return { { x, y }, { x + std::max(grid.width, 1), y + std::max(grid.height, 1) } };
}

ls::Rect2i snappedBox(ls::Vec2f anchor, ls::Vec2f pointer, const Grid& grid) {
    const ls::Vec2i a = nearestGridPoint(anchor, grid);
    const ls::Vec2i b = nearestGridPoint(pointer, grid);
    const ls::Rect2i cell = cellAt({ static_cast<int32_t>(std::floor(anchor.x)),
                                     static_cast<int32_t>(std::floor(anchor.y)) }, grid);
    ls::Rect2i box;
    if (a.x == b.x) {
        box.min.x = cell.min.x;
        box.max.x = cell.max.x;
    } else {
        box.min.x = std::min(a.x, b.x);
        box.max.x = std::max(a.x, b.x);
    }
    if (a.y == b.y) {
        box.min.y = cell.min.y;
        box.max.y = cell.max.y;
    } else {
        box.min.y = std::min(a.y, b.y);
        box.max.y = std::max(a.y, b.y);
    }
    return box;
}

ls::Vec2i snappedMove(ls::Vec2i topLeft, ls::Vec2i delta, const Grid& grid) {
    const ls::Vec2i landed = nearestGridPoint(
        { static_cast<float>(topLeft.x + delta.x), static_cast<float>(topLeft.y + delta.y) },
        grid);
    return { landed.x - topLeft.x, landed.y - topLeft.y };
}

} // namespace fast
