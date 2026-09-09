// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors
#pragma once

// shape.h — rectangles and ellipses that stay rectangles and ellipses.
//
// In every other pixel editor, a shape tool is a way of producing pixels. You
// drag out a rectangle, release, and what you have is a rectangular arrangement
// of pixels -- indistinguishable from having drawn it by hand, and no longer a
// rectangle in any sense the program can act on. Getting it two pixels wider
// means undoing and drawing it again.
//
// Here a shape is *geometry*, and a region tracks the geometry it was built
// from. Resize the geometry and the region follows; every fill on that region
// follows; an outline generated from it follows too. So a rectangle drawn an
// hour ago is still a rectangle, still has an origin and a width and a corner
// radius, and all three can be changed with the drawing intact.
//
// That is the same property as the transform panel, arriving somewhere a person
// meets it much earlier. It is also why a shape gets its own layer: it is a
// distinct editable object rather than a mark on a shared surface, and the layer
// stack is already the place where objects are listed.

#include "app/document.h"
#include "app/paint.h"

#include <string>
#include <vector>

namespace fast {

enum class ShapeKind { Rectangle, Ellipse, Line };

// A shape's parameters, in canvas coordinates. One struct for all three kinds
// because a drag produces the same two corners whichever tool is held, and the
// kind decides what those corners mean.
struct ShapeParams {
    ls::Vec2f from;
    ls::Vec2f to;
    float     cornerRadius = 0.f;   // rectangles only
    float     thickness = 1.f;      // lines only
};

// A layer holding one editable shape. Everything a PaintLayer is, plus the
// geometry that still defines it.
struct ShapeLayer {
    PaintLayer   paint;
    ls::GeometryId geometry;
    ShapeKind    kind = ShapeKind::Rectangle;

    bool valid() const { return paint.valid() && geometry.valid(); }
};

const char* shapeKindName(ShapeKind kind);

// Adds a layer holding a shape. Brackets its own undo action.
bool createShapeLayer(Document& doc, ls::SpriteId sprite, ShapeKind kind,
                      const ShapeParams& params, ls::Color colour, ShapeLayer* out);

// Changes a shape that already exists. This is the point of the whole file: it
// drives the geometry rather than redrawing anything, so the fill, the outline
// and anything else built on that region all follow.
bool updateShape(Document& doc, const ShapeLayer& shape, const ShapeParams& params);

// Reads a shape's current parameters back, so the interface shows the shape's
// own values rather than whatever was last dragged.
bool readShapeParams(Document& doc, const ShapeLayer& shape, ShapeParams* out);

// Recognises an editable shape on a layer, or reports that there is none.
//
// This is what makes a shape survive a file. A loaded layer is just operations;
// what marks it as a shape is that its fill names a region which still knows the
// geometry it came from.
bool shapeOfLayer(Document& doc, const PaintLayer& layer, ShapeLayer* out);

// ------------------------------------------------------------------ outline --
//
// An outline that follows the artwork instead of being stamped into it. In a
// conventional editor "outline" is a one-shot filter: it adds pixels, and if the
// shape moves afterwards the outline stays where it was. Here it is an operation
// resolved during the compile, so it tracks whatever the layer currently draws.

bool hasOutline(Document& doc, const PaintLayer& layer);
bool addOutline(Document& doc, const PaintLayer& layer, ls::Color colour,
                int thickness);
bool removeOutline(Document& doc, const PaintLayer& layer);
bool setOutlineColor(Document& doc, const PaintLayer& layer, ls::Color colour);
bool setOutlineThickness(Document& doc, const PaintLayer& layer, int thickness);
ls::Color outlineColor(Document& doc, const PaintLayer& layer);
int outlineThickness(Document& doc, const PaintLayer& layer);

} // namespace fast
