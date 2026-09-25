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

#include <functional>
#include <string>
#include <vector>

namespace fast {

enum class ShapeKind { Rectangle, Ellipse, Line, Polygon, Curve };

// A shape's parameters, in canvas coordinates. One struct for all three kinds
// because a drag produces the same two corners whichever tool is held, and the
// kind decides what those corners mean.
struct ShapeParams {
    ls::Vec2f from;
    ls::Vec2f to;
    float     cornerRadius = 0.f;   // rectangles only
    float     thickness = 1.f;      // a line's width, or an outlined shape's
    bool      outline = false;      // a rectangle or ellipse drawn as its edge only

    // A polygon's corners in order, or a curve's points: its first anchor,
    // then for each segment two control points and the next anchor -- 3n + 1
    // points for n segments. Empty for the kinds that are two corners; for
    // these, `from` and `to` are their bounds, for showing.
    std::vector<ls::Vec2f> points;
    bool      closed = false;       // a curve that returns to where it began, filled
};

// Every point of a shape moved by one mapping: its corners and each of its
// points. What a move, a flip or a turn does to a shape of any kind.
void mapShapePoints(ShapeParams& params, const std::function<ls::Vec2f(ls::Vec2f)>& map);

// The points a person drags to edit a shape: a box's two corners, a line's
// ends, a polygon's corners, a curve's anchors and control points -- a closed
// curve's last anchor, which is its first, only once.
std::vector<ls::Vec2f> shapeHandles(ShapeKind kind, const ShapeParams& params);

// Whether a handle is a curve's control point rather than a point it passes
// through; they are drawn differently.
bool isControlHandle(ShapeKind kind, size_t index);

// Moves one handle to `to`. A curve's anchor carries its control points with
// it, so the curve keeps its shape about the point; a closed curve's first
// anchor is its last one too.
void moveShapeHandle(ShapeKind kind, ShapeParams& params, size_t index, ls::Vec2f to);

// A curve as a pen tool makes one: through `anchors`, each with an outgoing
// handle offset (the incoming handle is its mirror, so the curve is smooth
// there; a zero handle makes a corner). Closed adds the segment back to the
// first anchor. The points layout of ShapeParams.
std::vector<ls::Vec2f> curveThrough(const std::vector<ls::Vec2f>& anchors,
                                    const std::vector<ls::Vec2f>& handles, bool closed);

// A layer holding one editable shape. Everything a PaintLayer is, plus the
// geometry that still defines it.
struct ShapeLayer {
    PaintLayer   paint;
    ls::GeometryId geometry;
    ShapeKind    kind = ShapeKind::Rectangle;

    bool valid() const { return paint.valid() && geometry.valid(); }
};

const char* shapeKindName(ShapeKind kind);

// What kind of shape a geometry is, asked of the geometry itself.
ShapeKind shapeKindOf(Document& doc, ls::GeometryId geometry);

// Adds a layer holding a shape. Brackets its own undo action.
bool createShapeLayer(Document& doc, ls::SpriteId sprite, ShapeKind kind,
                      const ShapeParams& params, ls::Color colour, ShapeLayer* out);

// Changes a shape that already exists. This is the point of the whole file: it
// drives the geometry rather than redrawing anything, so the fill, the outline
// and anything else built on that region all follow.
// Adds a shape to a layer that already exists, as one more of its elements,
// in the given colour and role. What createShapeLayer does after making the
// layer; see element.h for the list this joins.
bool addShapeTo(Document& doc, ls::LayerId layer, ShapeKind kind,
                const ShapeParams& params, ls::Color colour, ls::ColorRole role,
                ShapeLayer* out);

bool updateShape(Document& doc, const ShapeLayer& shape, const ShapeParams& params);

// Filled or outlined: the same shape, the same colour and slot, drawn as its
// area or as its edge `width` pixels thick. Swaps the operation in place, so
// the element keeps its position in the layer. `op` is updated to the new
// operation. Does not bracket an action.
bool setShapeOutlined(Document& doc, ls::LayerId layer, ls::OperationId* op, bool outlined,
                      float width);
// Whether an element's operation draws an outline, and how thick.
bool shapeIsOutlined(Document& doc, ls::OperationId op, float* width);

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

// What the outline traces.
//
// These are genuinely different pictures, not a preference. A character built
// from a body layer and an arm layer wants one line around the *figure*; a
// highlight or a held object wants a line around *that part*. Where the two
// layers meet, a per-layer outline draws a seam and a figure outline does not.
enum class OutlineScope : uint8_t {
    Layer,      // this layer alone -- the default, and what one part wants
    Sprite,     // everything the sprite draws, however many layers that is
};

// The maximum a line may be. Beyond a few pixels an outline stops reading as a
// line and starts eating the sprite, and on a 16-pixel canvas four is already
// most of it.
constexpr int kMaxOutlineThickness = 8;

struct OutlineSettings {
    OutlineScope   scope     = OutlineScope::Layer;
    int            thickness = 1;
    ls::OutlineSide side     = ls::OutlineSide::Outside;

    // The colour, as a value or as a palette slot. A slot is the better answer
    // when there is one: changing what the slot means recolours every outline
    // using it on the next compile, from the drawing rather than over it, which
    // is the same promise the fills make.
    ls::Color     colour { 20, 22, 28, 255 };
    ls::ColorRole role = ls::kColorRoleNone;
};

bool hasOutline(Document& doc, const PaintLayer& layer);

// Adds an outline, or replaces the settings of the one already there -- so the
// interface can drive every control through one call rather than four, and a
// drag through forty thicknesses still leaves one operation.
bool setOutline(Document& doc, const PaintLayer& layer, const OutlineSettings& settings);

bool removeOutline(Document& doc, const PaintLayer& layer);

// What the layer's outline is currently set to. Defaults when it has none, so a
// panel can show the controls it would create.
OutlineSettings outlineOf(Document& doc, const PaintLayer& layer);

} // namespace fast
