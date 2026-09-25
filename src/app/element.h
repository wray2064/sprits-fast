// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors
#pragma once

// element.h — several marks on one layer.
//
// A layer used to be one thing: a freehand region with a fill, or a shape with
// its own layer. Draw a rectangle, then a line, then a few pixels, and you had
// three layers -- which is not how anyone thinks of a character's belt. So a
// layer is now a list of *elements*, each an operation of its own: a freehand
// region the pencil writes into, and any number of rectangles, ellipses and
// lines that stay editable shapes. The shape tools add to the active layer;
// the pencil finds the layer's freehand element, or makes one.
//
// The engine had this already -- a layer is an ordered list of operations,
// each naming what it draws -- so an element is nothing new to it. What is new
// is the interface treating the list as a list.
//
// Colour and role apply to every element of a layer at once, so a layer still
// reads as one thing in the palette; a dither applies to the freehand element,
// since a ramp over a rectangle is a different picture from a ramp over a
// stroke and the panel edits one.

#include "app/document.h"
#include "app/ink.h"
#include "app/paint.h"
#include "app/shape.h"

#include <vector>

namespace fast {

// Text is an element of its own: pixels rebuilt from words kept on its
// region (see text.h). Like a shape, it is an object on the layer rather than
// a colour the pencil paints into.
enum class ElementKind { Paint, Rectangle, Ellipse, Line, Text, Polygon, Curve };

const char* elementKindName(ElementKind kind);

struct Element {
    ls::OperationId fill;         // the fill or stroke that draws it
    ls::RegionId    region;       // null for a line
    ls::GeometryId  geometry;     // null for freehand
    ElementKind     kind = ElementKind::Paint;
    bool            outlined = false;    // a shape drawn as its edge only

    bool valid() const { return fill.valid(); }
    // Anything that is an object on the layer rather than loose pixels: the
    // shapes, and text.
    bool isShape() const { return kind != ElementKind::Paint; }
    bool isGeometry() const {
        return kind == ElementKind::Rectangle || kind == ElementKind::Ellipse ||
               kind == ElementKind::Line || kind == ElementKind::Polygon ||
               kind == ElementKind::Curve;
    }
};

// Every element of a layer, in draw order.
std::vector<Element> elementsOf(Document& doc, ls::LayerId layer);

// The element the panel is editing as a shape.
ShapeLayer shapeOfElement(ls::LayerId layer, const Element& element);

// Adds a shape to a layer, on top of its other elements, in the layer's
// colour and role. Brackets its own action. Returns the shape so a drag can
// keep driving it.
bool addShapeElement(Document& doc, ls::LayerId layer, ShapeKind kind,
                     const ShapeParams& params, ShapeLayer* out);

// The same, in a given ink -- what the shape tools draw with, now that a
// layer holds as many colours as it has been painted with.
bool addShapeElement(Document& doc, ls::LayerId layer, ShapeKind kind,
                     const ShapeParams& params, const Ink& ink, ShapeLayer* out);

// Points `layer` at its freehand element for the pencil, creating one -- in
// the layer's colour and role -- if every element is a shape. A handle
// adopted from a shape-only layer names the shape's region, and drawing into
// that would turn the shape into pixels; this is what the pencil calls
// first. Brackets its own action when it makes one.
bool ensurePaintElement(Document& doc, PaintLayer& layer);

// Removes one element. Refuses the last, because a layer with nothing in it
// is a layer to delete instead. Brackets its own action.
bool removeElement(Document& doc, ls::LayerId layer, const Element& element);

// The colour and role of a layer, applied to every element. The layer's own
// colour is its first element's.
bool setElementsColor(Document& doc, ls::LayerId layer, ls::Color colour);
bool setElementsRole(Document& doc, ls::LayerId layer, ls::ColorRole role);

} // namespace fast
