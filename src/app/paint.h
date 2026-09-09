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

    // A layer and the operation that colours it. The region is separate,
    // because not every drawable layer has one: a stroked line names a polyline
    // directly and encloses no area at all. Anything that writes pixels needs
    // `drawable()`; anything that only recolours or transforms needs `valid()`.
    bool valid() const { return layer.valid() && fill.valid(); }
    bool drawable() const { return valid() && region.valid(); }
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

// Reconstructs the paint layers of a document that was just opened.
//
// A file carries operations, not the handles the interface was holding when it
// was written -- the reader mints fresh ids by design. So after an open, the
// editor has to look at what it has and work out which layers it can draw on:
// a layer whose first fill names a region is a paint layer, and that region is
// where the pencil accumulates.
//
// Layers it does not recognise are skipped rather than guessed at. A file may
// contain gradients, dithers, strokes and transforms that Fast has no tool for;
// those still compile and still display, they simply cannot be drawn on with a
// pencil. Skipping them is what lets Fast open a file made by a richer editor
// without either breaking it or pretending to understand it.
//
// Returns false only if the document has no sprite at all.
bool adoptPaintLayers(Document& doc, ls::SpriteId* outSprite,
                      std::vector<PaintLayer>* outLayers);

// The pixels between two points, so a fast drag does not leave gaps. Bresenham,
// inclusive of both ends.
std::vector<ls::Vec2i> linePixels(ls::Vec2i from, ls::Vec2i to);

} // namespace fast
