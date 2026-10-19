// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors
#pragma once

// vector_icon.h — an icon as strokes, read from an SVG.
//
// The interface's icons are Lucide's (ISC licence, assets/icons/lucide/): each a
// 24 x 24 drawing made only of strokes -- paths, circles, rectangles, lines.
// They are read here into lists of points and drawn by the window with the
// colour and width it likes, so an icon is as crisp at 16 pixels as at 64 and
// takes whatever colour the theme says, with no picture involved. That is the
// same thing the rest of the program holds to: the shape is the truth, and
// pixels are only what it is drawn as.
//
// Only what those icons use is understood: <path> with M L H V C S Q T A Z in
// both cases, <circle>, <ellipse>, <rect> (with a corner radius), <line>,
// <polyline> and <polygon>. Anything else in a file is skipped, and a file is
// bounded, because an icon set is somebody else's file.

#include <string>
#include <utility>
#include <vector>

namespace fast {

struct IconStroke {
    std::vector<std::pair<float, float>> points;   // in the icon's own units
    bool closed = false;
};

struct VectorIcon {
    float width = 24.f;      // the viewBox
    float height = 24.f;
    std::vector<IconStroke> strokes;
};

constexpr size_t kMaxIconBytes = 16 * 1024;
constexpr size_t kMaxIconStrokes = 128;
constexpr size_t kMaxStrokePoints = 4096;

// Reads `svg`. False, with a reason, when it is too large or has no drawable
// shape; a shape it does not understand is skipped, not an error.
bool parseVectorIcon(const std::string& svg, VectorIcon* out, std::string* error);

// The bounding box of every point, for a test and for centring.
void iconBounds(const VectorIcon& icon, float* minX, float* minY, float* maxX, float* maxY);

} // namespace fast
