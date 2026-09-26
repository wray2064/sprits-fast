// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors
#pragma once

// paint.h — what a pencil does when pixels are not the truth.
//
// In a conventional editor a pencil writes into a bitmap. Here there is no
// bitmap to write into: a layer is a stack of operations, and the picture is
// compiled from it -- and nothing in it is pixels until then.
//
// So a paint layer is a *region* and a fill operation that colours it, and a
// freehand region is made of *strokes* (the engine's StrokesDesc): the paths
// the pencil took and the brushes it took them with, the areas laid down
// whole, and the marks that erased them. Where they were drawn they draw
// exactly the pixels drawn; turned or scaled, they are drawn again where they
// land, so a turned pencil line is a pencil line. Changing the colour
// afterwards is one call, and does not touch the drawing at all.

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

// A brush, as a freehand mark remembers it.
struct PenBrush {
    int  size = 1;
    bool round = false;
    bool pixelPerfect = true;     // at size 1: the corners of an L dropped
};

// What a region is made of. Pixels are what documents from before freehand
// marks were kept as strokes hold -- still drawn, and still edited, as pixels.
enum class RegionMade { Missing, Pixels, Strokes, Area, Face, Shape };
RegionMade regionMadeOf(Document& doc, ls::RegionId region, ls::GeometryId* geometry = nullptr);

// A freehand region with nothing drawn in it yet. Does not bracket an action.
ls::RegionId createFreehandRegion(Document& doc);

// A freehand region holding `pixels` laid down whole, as an area: pixels
// that came from outside -- an imported image -- made marks like any other.
ls::RegionId createFreehandRegion(Document& doc, const ls::IntervalSet& pixels);

// The strokes a freehand region is made of, and where they are kept. False
// when the region is not made of strokes.
bool readStrokes(Document& doc, ls::RegionId region, ls::GeometryId* geometry,
                 ls::StrokesDesc* out);

// Marks, as a freehand region keeps them: a path of pixel centres with the
// brush stamped along it, and a set of pixels laid down whole as an area.
ls::PenStroke pathMark(const std::vector<ls::Vec2i>& centres, const PenBrush& brush);
ls::PenStroke areaMark(const ls::IntervalSet& pixels);
// The pixels a mark draws where it was made.
ls::IntervalSet markPixels(const ls::PenStroke& mark);

// Takes pixels out of a region and leaves it made of what it was made of.
// Strokes lose them exactly -- a thin line is cut, an area trimmed -- and a
// wide stroke under them gets `eraser` over it as an erasing mark. An area is
// traced again without them. A shape or a fill keeps `eraser` as what was
// erased from it, so it stays a shape. Pixels lose the pixels. `eraser` is
// what took them: the eraser's path and brush, or an area. False when the
// region drew none of them. Does not bracket an action.
bool eraseFromRegion(Document& doc, ls::RegionId region, const ls::PenStroke& eraser,
                     const ls::IntervalSet& pixels);

// Deletes a region and the geometry it was made of, and what was erased
// from it. For an element that is going away.
void deleteRegionAndShapes(Document& doc, ls::RegionId region);

// Adds a layer to `sprite` that can be drawn on. Brackets its own undo action.
bool createPaintLayer(Document& doc, ls::SpriteId sprite, const std::string& name,
                      ls::Color color, PaintLayer* out);

// Adds or removes pixels -- as an area, since a set of pixels has no path.
// These do *not* bracket an undo action: a stroke is many calls and one
// history entry, so the caller brackets the whole drag.
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

// The same, for a sprite the caller has already chosen.
//
// A document holds a list of sprites, and once frames exist that list is the
// timeline -- so "the document's layers" stops being a question with one answer
// and becomes "the layers of the frame being looked at". The overload above
// answers the old question by taking the first sprite, which is the right answer
// for a document with one frame and the wrong one for any other.
bool adoptPaintLayers(Document& doc, ls::SpriteId sprite,
                      std::vector<PaintLayer>* outLayers);

// The pixels between two points, so a fast drag does not leave gaps. Bresenham,
// inclusive of both ends.
std::vector<ls::Vec2i> linePixels(ls::Vec2i from, ls::Vec2i to);

} // namespace fast
