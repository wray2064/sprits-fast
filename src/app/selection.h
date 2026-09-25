// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors
#pragma once

// selection.h — which pixels the next edit is about.
//
// A selection is a mask over the canvas: a set of pixels, kept as the same
// interval runs the engine keeps regions in, so the boolean operations a
// marquee needs -- add, subtract, intersect, invert -- are the engine's own and
// already tested. It is not part of the document: like the zoom, it is where the
// person is looking rather than something they made, so it is not saved and
// selecting is not an undo step. What is done *to* the selected pixels is.
//
// Everything here is plain arithmetic over interval sets, with no engine state,
// so the whole of it can be tested without a document.

#include "app/document.h"
#include "app/bucket.h"

#include <vector>

namespace fast {

// How a new shape combines with the selection already there. Shift adds, Alt
// subtracts and both intersect -- the modifiers every editor since Photoshop
// has used, so a hand that knows one knows this.
enum class SelectMode { Replace, Add, Subtract, Intersect };

struct Selection {
    ls::IntervalSet mask;        // canvas pixels, normalized
    ls::IntervalSet previous;    // what Deselect dropped, for Reselect

    bool empty() const { return mask.empty(); }
    bool contains(ls::Vec2i pixel) const;
    ls::Rect2i bounds() const;
};

// The pixels inside a rectangle with corners `a` and `b`, both inclusive --
// a drag from one pixel to another selects both.
ls::IntervalSet rectangleMask(ls::Vec2i a, ls::Vec2i b);

// The ellipse inscribed in that rectangle, as pixels. Symmetric in both axes,
// so a selection made with it and flipped is the same selection.
ls::IntervalSet ellipseMask(ls::Vec2i a, ls::Vec2i b);

// A freehand or polygonal lasso: the pixels the closed outline encloses, and
// the outline itself, so a thin lasso still selects what it was drawn over.
ls::IntervalSet lassoMask(const std::vector<ls::Vec2i>& points);

// The magic wand: what a paint bucket at `seed` would fill, with the same
// settings -- contiguous or global, tolerance, diagonals -- because it is the
// same question asked for a different reason.
ls::IntervalSet wandMask(Document& doc, ls::SpriteId sprite, ls::Vec2i seed,
                         const BucketSettings& settings);

// Combines `shape` into `current`.
ls::IntervalSet combine(const ls::IntervalSet& current, const ls::IntervalSet& shape,
                        SelectMode mode);

// Keeps a mask on the canvas. Everything that builds one clips through this,
// so a marquee dragged off the edge selects to the edge.
ls::IntervalSet clipToCanvas(const ls::IntervalSet& mask, uint32_t width, uint32_t height);

// The same pixels, moved by a whole number of pixels.
ls::IntervalSet translated(const ls::IntervalSet& mask, ls::Vec2i by);

// Mirrored left-right or top-bottom within `within`, and turned by quarter
// turns about its centre. Exact, because the grid maps onto itself: a pixel
// goes to a pixel, never between two.
ls::IntervalSet flippedHorizontally(const ls::IntervalSet& mask, ls::Rect2i within);
ls::IntervalSet flippedVertically(const ls::IntervalSet& mask, ls::Rect2i within);
ls::IntervalSet rotatedQuarter(const ls::IntervalSet& mask, ls::Rect2i within, bool clockwise);

// Every pixel of a mask, row by row.
std::vector<ls::Vec2i> pixelsOf(const ls::IntervalSet& mask);

// The edges of a mask, as unit segments between pixel corners, for drawing
// marching ants: horizontal edges where a row's coverage differs from the
// row above, and vertical ones at the ends of each run.
struct MaskEdge {
    ls::Vec2i from;
    ls::Vec2i to;
};
std::vector<MaskEdge> maskOutline(const ls::IntervalSet& mask);

} // namespace fast
