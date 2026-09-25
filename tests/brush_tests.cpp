// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

// brush_tests.cpp — what a stroke lays down.

#include "app/brush.h"

#include <cstdio>
#include <set>
#include <vector>

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

bool has(const std::vector<ls::Vec2i>& pixels, int x, int y) {
    for (ls::Vec2i p : pixels) {
        if (p.x == x && p.y == y) { return true; }
    }
    return false;
}

std::set<std::pair<int, int>> asSet(const std::vector<ls::Vec2i>& pixels) {
    std::set<std::pair<int, int>> out;
    for (ls::Vec2i p : pixels) { out.insert({ p.x, p.y }); }
    return out;
}

void testStamps() {
    // Size 1 is the pixel; size 2 hangs right and down; size 3 is centred.
    CHECK(brushStamp({ 5, 5 }, 1, false).size() == 1);
    CHECK(has(brushStamp({ 5, 5 }, 1, true), 5, 5));
    const auto two = brushStamp({ 5, 5 }, 2, false);
    CHECK(two.size() == 4 && has(two, 5, 5) && has(two, 6, 6) && !has(two, 4, 4));
    const auto three = brushStamp({ 5, 5 }, 3, false);
    CHECK(three.size() == 9 && has(three, 4, 4) && has(three, 6, 6));
    // Round at 3 is a plus: the corners go.
    const auto plus = brushStamp({ 5, 5 }, 3, true);
    CHECK(plus.size() == 5 && has(plus, 5, 4) && !has(plus, 4, 4));
    // Round at 5 has no corners and is symmetric.
    const auto five = brushStamp({ 10, 10 }, 5, true);
    CHECK(!has(five, 8, 8) && has(five, 8, 10) && has(five, 12, 10) && has(five, 10, 8));
    CHECK(five.size() == 21);
    // Sizes are clamped.
    CHECK(brushStamp({ 0, 0 }, 0, false).size() == 1);
    CHECK(brushStamp({ 0, 0 }, 999, false).size() ==
          static_cast<size_t>(kMaxBrushSize) * static_cast<size_t>(kMaxBrushSize));
}

void testStrokesCoverTheLineOnce() {
    const auto thin = strokePixels({ 0, 0 }, { 4, 2 }, 1, false);
    CHECK(thin.size() == 5 && has(thin, 0, 0) && has(thin, 4, 2));
    const auto thick = strokePixels({ 0, 0 }, { 4, 0 }, 3, false);
    // Five centres under a 3-wide stamp: columns -1..5, three rows, none twice.
    CHECK(thick.size() == 21);
    CHECK(asSet(thick).size() == thick.size());
    CHECK(has(thick, -1, -1) && has(thick, 5, 1));
}

void testPressure() {
    BrushSettings brush;
    brush.size = 8;
    CHECK(pressuredSize(brush, 0.5f) == 8);          // off: pressure is ignored
    brush.pressureSize = true;
    CHECK(pressuredSize(brush, 1.f) == 8);
    CHECK(pressuredSize(brush, 0.5f) == 4);
    CHECK(pressuredSize(brush, 0.f) == 1);           // never nothing
    CHECK(pressuredSize(brush, 2.f) == 8);           // clamped
}

// The rule itself: an L loses its corner, a straight run keeps every pixel,
// and a staircase of Ls becomes a clean diagonal.
void testPixelPerfectDropsCorners() {
    PixelPerfect filter;
    std::vector<ls::Vec2i> painted;
    const auto feed = [&](std::vector<ls::Vec2i> path) {
        for (ls::Vec2i p : path) {
            for (ls::Vec2i out : filter.push(p)) { painted.push_back(out); }
        }
        for (ls::Vec2i out : filter.finish()) { painted.push_back(out); }
    };

    // Straight: nothing dropped.
    feed({ { 0, 0 }, { 1, 0 }, { 2, 0 }, { 3, 0 } });
    CHECK(painted.size() == 4);

    // One L: the corner goes.
    painted.clear();
    feed({ { 0, 0 }, { 1, 0 }, { 1, 1 } });
    CHECK(painted.size() == 2 && has(painted, 0, 0) && has(painted, 1, 1) && !has(painted, 1, 0));

    // A staircase drawn as the mouse reports it: right, down, right, down.
    // Every corner goes and a diagonal is left.
    painted.clear();
    feed({ { 0, 0 }, { 1, 0 }, { 1, 1 }, { 2, 1 }, { 2, 2 }, { 3, 2 }, { 3, 3 } });
    CHECK(painted.size() == 4);
    CHECK(has(painted, 0, 0) && has(painted, 1, 1) && has(painted, 2, 2) && has(painted, 3, 3));

    // Right, right, down, down: one corner, at the turn, and the runs either
    // side of it keep every pixel.
    painted.clear();
    feed({ { 0, 0 }, { 1, 0 }, { 2, 0 }, { 2, 1 }, { 2, 2 } });
    CHECK(painted.size() == 4 && !has(painted, 2, 0) && has(painted, 0, 0) && has(painted, 2, 2));

    // Repeats of the same sample do nothing.
    painted.clear();
    feed({ { 0, 0 }, { 0, 0 }, { 1, 0 }, { 1, 0 }, { 1, 1 } });
    CHECK(painted.size() == 2);

    // A single point paints on finish.
    painted.clear();
    feed({ { 7, 7 } });
    CHECK(painted.size() == 1 && has(painted, 7, 7));
}

} // namespace

void testSymmetryMirrorsAboutTheAxis() {
    Symmetry s;
    s.across = true;
    s.axisX = 32;                   // the middle of a 32-wide canvas
    std::vector<ls::Vec2i> out = mirrored({{ 0, 5 }, { 15, 6 }}, s);
    CHECK(out.size() == 4);
    const auto has = [&](int x, int y) {
        for (ls::Vec2i p : out) { if (p.x == x && p.y == y) { return true; } }
        return false;
    };
    CHECK(has(31, 5) && has(16, 6));

    // An odd width: the axis runs through the middle pixel, which is its own
    // mirror and appears once.
    s.axisX = 33;
    out = mirrored({{ 16, 0 }}, s);
    CHECK(out.size() == 1);

    // Both axes: four copies.
    s.axisX = 8;
    s.down = true;
    s.axisY = 8;
    out = mirrored({{ 1, 2 }}, s);
    CHECK(out.size() == 4 && has(6, 2) && has(1, 5) && has(6, 5));

    // Off: untouched.
    CHECK(mirrored({{ 1, 2 }}, Symmetry{}).size() == 1);
}

int main() {
    testSymmetryMirrorsAboutTheAxis();
    testStamps();
    testStrokesCoverTheLineOnce();
    testPressure();
    testPixelPerfectDropsCorners();
    if (failures == 0) {
        std::printf("fast_brush: all checks passed\n");
        return 0;
    }
    std::printf("fast_brush: %d check(s) failed\n", failures);
    return 1;
}
