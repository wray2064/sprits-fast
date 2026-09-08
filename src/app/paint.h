// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors
#pragma once

// paint.h — what a pencil does when pixels are not the truth.
//
// In a conventional editor a pencil writes into a bitmap. Here there is no
// bitmap to write into: a layer is a stack of operations, and the picture is
// compiled from it.
//
// So a paint layer is a *region* -- the shape the user has drawn -- and a fill
// operation that colours it. The pencil accumulates pixels into that one region
// rather than adding an operation per stroke, which is why a thousand strokes
// stay a thousand pixels in one shape instead of a thousand operations to
// resolve. Changing the colour afterwards is one call, and does not touch the
// drawing at all.

#include "app/document.h"

#include <string>
#include <vector>

namespace fast {

// One drawable layer: the shape, and the rule that colours it.
struct PaintLayer {
    ls::LayerId     layer;
    ls::RegionId    region;
    ls::OperationId fill;

    bool valid() const { return layer.valid() && region.valid() && fill.valid(); }
};

// Adds a layer to `sprite` that can be drawn on. Brackets its own undo action.
bool createPaintLayer(Document& doc, ls::SpriteId sprite, const std::string& name,
                      ls::Color color, PaintLayer* out);

// Adds or removes pixels. These do *not* bracket an undo action: a stroke is
// many calls and one history entry, so the caller brackets the whole drag.
bool paintPixels(Document& doc, const PaintLayer& target,
                 const std::vector<ls::Vec2i>& pixels);
bool erasePixels(Document& doc, const PaintLayer& target,
                 const std::vector<ls::Vec2i>& pixels);

// The colour of a paint layer, changed without touching what was drawn. This is
// the difference the whole design is for.
bool setPaintColor(Document& doc, const PaintLayer& target, ls::Color color);
ls::Color paintColor(Document& doc, const PaintLayer& target);

// The pixels between two points, so a fast drag does not leave gaps. Bresenham,
// inclusive of both ends.
std::vector<ls::Vec2i> linePixels(ls::Vec2i from, ls::Vec2i to);

} // namespace fast
