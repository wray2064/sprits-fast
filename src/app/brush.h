// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors
#pragma once

// brush.h — what a freehand stroke lays down.
//
// A pencil was one pixel wide and every sample was painted as it came. That is
// the right default for pixel art and the wrong ceiling: a brush has a size, a
// shape, and -- at one pixel -- the rule every pixel-art tool has and no
// general painter does, that a stroke's corners are dropped so a diagonal reads
// as a line rather than a staircase with doubled steps. And a stylus reports
// pressure, which can drive the size.
//
// The engine sees none of this. A stroke is still pixels added to a region;
// what changes is which pixels, decided here from the path the pointer took.

#include <livesprite/livesprite.h>

#include <vector>

namespace fast {

constexpr int kMaxBrushSize = 32;

struct BrushSettings {
    int  size = 1;               // pixels across
    bool round = false;          // round rather than square, which only shows from 3 up
    bool pixelPerfect = true;    // at size 1: drop the corner of every L
    bool pressureSize = false;   // a pen's pressure scales the size, 1 up to `size`
    int  stabiliser = 0;         // the string's length in pixels; 0 is off
};

// The lazy mouse: the line follows a point dragged along behind the pointer
// on a string, so a hand's tremor never reaches the canvas and a curve comes
// out smooth. Nothing moves until the pointer is further than the string's
// length away; then the point is pulled straight toward it.
class Stabiliser {
public:
    void      reset(ls::Vec2f at) { at_ = at; }
    ls::Vec2f follow(ls::Vec2f pointer, float length);
    ls::Vec2f at() const { return at_; }
private:
    ls::Vec2f at_ { 0.f, 0.f };
};

// The pixels one stamp of the brush covers, centred on `at`. Even sizes sit
// with their centre on the pixel's top-left corner, so a 2-wide brush is the
// pixel and its right and lower neighbours rather than a smear either side.
std::vector<ls::Vec2i> brushStamp(ls::Vec2i at, int size, bool round);

// The pixels a stroke segment covers: the brush stamped along the line from
// `from` to `to`, each pixel once.
std::vector<ls::Vec2i> strokePixels(ls::Vec2i from, ls::Vec2i to, int size, bool round);

// Symmetry: drawing on one side draws on the other as well.
//
// An axis is given doubled, as the sum of the two pixel edges it lies between
// -- so a 32-wide canvas's middle is 32 (between pixels 15 and 16) and a
// 33-wide canvas's is 33 (through the middle of pixel 16). Doubling keeps the
// arithmetic in whole numbers for both, and a pixel at x lands at
// axis - 1 - x either way.
struct Symmetry {
    bool across = false;    // mirrored left to right, about a vertical axis
    bool down = false;      // mirrored top to bottom, about a horizontal one
    int  axisX = 0;         // doubled, as above
    int  axisY = 0;
    bool active() const { return across || down; }
};

// The pixels with their mirror images added: two copies for one axis, four
// for both. Each pixel appears once.
std::vector<ls::Vec2i> mirrored(const std::vector<ls::Vec2i>& pixels, const Symmetry& symmetry);

// A spray: `count` pixels scattered in a disc of `radius` about `at`, from a
// seeded generator, so the same seed sprays the same pixels -- a stroke
// replayed is the same stroke.
std::vector<ls::Vec2i> sprayPixels(ls::Vec2i at, int radius, int count, uint32_t seed);

// The size a pen at `pressure` (0..1) draws at, when pressure sets the size.
int pressuredSize(const BrushSettings& brush, float pressure);

// The pixel-perfect rule, as a filter fed the stroke's path one point at a
// time. It holds each point back until the next arrives, because whether a
// point is the corner of an L cannot be known until the path has moved on:
//
//     . x .        the middle x is a corner -- its neighbours touch
//     x x .   ->   diagonally without it -- and is dropped, so the
//     . . .        stroke reads as a clean diagonal step.
//
// push() returns the points that are now certain; finish() releases the last.
class PixelPerfect {
public:
    // Points now safe to paint. Duplicates of the previous point are ignored.
    std::vector<ls::Vec2i> push(ls::Vec2i point);
    std::vector<ls::Vec2i> finish();
    void reset();

private:
    bool      havePainted_ = false;
    ls::Vec2i lastPainted_ { 0, 0 };
    bool      havePending_ = false;
    ls::Vec2i pending_ { 0, 0 };
};

} // namespace fast
