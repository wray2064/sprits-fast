// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

// grid_snap_tests.cpp — landing on the grid.
//
// The nearest grid point is nearest on each axis, with the offset honoured
// and negative positions handled; a cell is found for pixels either side of
// zero; a snapped marquee has its corners on grid lines, is never empty, and
// is the anchor's cell for a drag that stays inside one; a snapped move puts
// the corner on a grid point and leaves an already-snapped move alone.

#include "app/grid_snap.h"

#include <cstdio>

static int failures = 0;

#define CHECK(...)                                                            \
    do {                                                                      \
        if (!(__VA_ARGS__)) {                                                 \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #__VA_ARGS__);\
            ++failures;                                                       \
        }                                                                     \
    } while (0)

namespace {

using namespace fast;

bool same(ls::Vec2i a, ls::Vec2i b) { return a.x == b.x && a.y == b.y; }

bool box(ls::Rect2i r, int x0, int y0, int x1, int y1) {
    return r.min.x == x0 && r.min.y == y0 && r.max.x == x1 && r.max.y == y1;
}

void testNearestPoint() {
    Grid grid;
    grid.width = 8;
    grid.height = 4;
    CHECK(same(nearestGridPoint({ 3.5f, 1.5f }, grid), { 0, 0 }));
    CHECK(same(nearestGridPoint({ 4.5f, 2.5f }, grid), { 8, 4 }));
    CHECK(same(nearestGridPoint({ 4.f, 2.f }, grid), { 0, 0 }));        // a tie: the lower
    CHECK(same(nearestGridPoint({ -3.5f, -1.5f }, grid), { 0, 0 }));
    CHECK(same(nearestGridPoint({ -4.5f, -2.5f }, grid), { -8, -4 }));
    grid.offsetX = 3;
    grid.offsetY = 1;
    CHECK(same(nearestGridPoint({ 10.5f, 4.5f }, grid), { 11, 5 }));
    CHECK(same(nearestGridPoint({ 5.5f, 0.2f }, grid), { 3, 1 }));
    Grid one;
    one.width = 1;
    one.height = 1;
    CHECK(same(nearestGridPoint({ 6.2f, 6.7f }, one), { 6, 7 }));
}

void testCells() {
    Grid grid;
    grid.width = 16;
    grid.height = 16;
    CHECK(box(cellAt({ 0, 0 }, grid), 0, 0, 16, 16));
    CHECK(box(cellAt({ 15, 31 }, grid), 0, 16, 16, 32));
    CHECK(box(cellAt({ -1, -16 }, grid), -16, -16, 0, 0));
    CHECK(box(cellAt({ -17, 16 }, grid), -32, 16, -16, 32));
    grid.offsetX = 4;
    CHECK(box(cellAt({ 3, 0 }, grid), -12, 0, 4, 16));
    CHECK(box(cellAt({ 4, 0 }, grid), 4, 0, 20, 16));
}

void testSnappedMarquee() {
    Grid grid;
    grid.width = 8;
    grid.height = 8;
    // From near one corner to near another two cells over.
    CHECK(box(snappedBox({ 1.5f, 1.5f }, { 14.5f, 22.5f }, grid), 0, 0, 16, 24));
    // Dragged up and left: still min to max.
    CHECK(box(snappedBox({ 14.5f, 22.5f }, { 1.5f, 1.5f }, grid), 0, 0, 16, 24));
    // Inside one cell: that cell.
    CHECK(box(snappedBox({ 9.5f, 9.5f }, { 10.5f, 10.5f }, grid), 8, 8, 16, 16));
    // Across on one axis only: the anchor's cell down the other.
    CHECK(box(snappedBox({ 9.5f, 9.5f }, { 30.5f, 10.5f }, grid), 8, 8, 32, 16));
}

void testSnappedMove() {
    Grid grid;
    grid.width = 10;
    grid.height = 10;
    // A corner at (3, 4) dragged by (9, 13) would land at (12, 17): the nearest
    // grid point is (10, 20), so the move becomes (7, 16).
    CHECK(same(snappedMove({ 3, 4 }, { 9, 13 }, grid), { 7, 16 }));
    // Already on the grid, and moved a whole cell: unchanged.
    CHECK(same(snappedMove({ 10, 10 }, { 10, -10 }, grid), { 10, -10 }));
    // A small nudge from a grid point stays put.
    CHECK(same(snappedMove({ 10, 10 }, { 2, -3 }, grid), { 0, 0 }));
}

} // namespace

int main() {
    testNearestPoint();
    testCells();
    testSnappedMarquee();
    testSnappedMove();
    if (failures == 0) {
        std::printf("grid_snap: all passed\n");
        return 0;
    }
    std::printf("grid_snap: %d failure(s)\n", failures);
    return 1;
}
