// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors
#pragma once

// floating.h — selected pixels on the move, and the clipboard.
//
// Moving a selection is where a pixel editor usually stops pretending: the
// pixels are cut into a floating bitmap, dragged, and stamped down. Here
// what a layer draws is marks -- strokes, areas, fills, shapes -- and the move
// is done in those terms. Lifting takes what the selection covers out of each
// element, leaving each made of what it was made of (see eraseFromRegion),
// and gives each colour a *piece*: a run of its own at the top of the layer,
// in the same colour or the same dither, holding the marks the selection cut
// out -- a line cut at the selection's edge is still a line. The drag moves
// the marks. Nothing is rasterised on the way -- a piece through a palette
// slot is still through that slot while it floats, so a palette swap
// mid-drag recolours it like everything else.
//
// A piece sits over the layer, so dragging across other pixels does not eat
// them, and dropping leaves it there: it covers what it lands on rather than
// cutting it away. A piece that lands straight above a run of its own colour
// joins it.
//
// The whole float -- lift, drag, turn, drop -- happens inside one history
// action that the caller holds open, so one Ctrl+Z undoes a move and Escape
// abandons it with nothing to clean up.
//
// Layers with a transform refuse: a selection is drawn over the canvas, and a
// turned layer's marks are not where the canvas shows them.

#include "app/document.h"
#include "app/ink.h"
#include "app/shape.h"

#include <vector>

namespace fast {

// Pixels copied off a layer, by colour. A dithered piece remembers the element
// it came from and pastes as a copy of that rule -- in the same document. In
// another it pastes as the ramp's middle colour, since the ramp and pattern it
// names belong to the first.
struct PixelClip {
    struct Piece {
        Ink             ink;
        ls::OperationId dither;       // null for a solid piece
        ls::IntervalSet pixels;       // canvas pixels where they were copied
        // The marks, where they were copied -- a line still a line. Empty for
        // a piece that came from an image, which is its pixels.
        ls::StrokesDesc marks;
    };
    std::vector<Piece> pieces;
    ls::IntervalSet    mask;          // everything copied, for re-selecting on paste
    ls::DocumentId     from;          // which document the dithers belong to

    bool empty() const { return pieces.empty(); }
};

// A clip as a brush: each piece's pixels placed so the middle of what was
// copied sits on `centre`. What a custom brush stamps, colour by colour.
std::vector<std::vector<ls::Vec2i>> stampOf(const PixelClip& clip, ls::Vec2i centre);

// Whether a layer's pixels can be lifted, copied and cleared through a
// selection: false for a layer with any transform on it.
bool layerTakesSelections(Document& doc, ls::LayerId layer);

// What `mask` covers on `layer`, colour by colour.
bool copyPixels(Document& doc, ls::LayerId layer, const ls::IntervalSet& mask, PixelClip* out);

// Removes what `mask` covers from every colour on `layer` -- and, with
// `shapesToo`, from its shapes, through an erase that leaves them shapes (see
// ink.h). Cut and the commands that lift pixels onto a layer of their own
// leave shapes be, since what they carry is the pixels, not the shapes. Does
// not bracket an action.
bool clearPixels(Document& doc, ls::LayerId layer, const ls::IntervalSet& mask,
                 bool shapesToo = true);

struct Floating {
    struct Piece {
        ls::OperationId fill;
        ls::RegionId    region;
        ls::IntervalSet original;     // where it was lifted or pasted, before any move
        ls::StrokesDesc marks;        // its marks there
    };
    // A shape the selection wholly contains goes along with the pixels -- as a
    // shape: its geometry is moved, so it is still a rectangle when it lands.
    struct Shape {
        ShapeLayer  shape;
        ShapeParams original;
    };
    // What was erased from a shape that goes along goes with it -- it is the
    // shape's own -- so a rectangle with a corner rubbed out lands with the
    // corner still rubbed out.
    struct ShapeErase {
        ls::GeometryId  strokes;
        ls::StrokesDesc original;
    };
    ls::LayerId             layer;
    std::vector<Piece>      pieces;
    std::vector<Shape>      shapes;
    std::vector<ShapeErase> shapeErases;
    ls::IntervalSet    originalMask;
    ls::Vec2i          offset { 0, 0 };

    bool active() const { return layer.valid(); }
};

// Lifts the pixels `mask` covers on `layer` into floating pieces, drawn where
// they were, and takes along every shape the mask wholly contains. False, and
// nothing changed, when there is nothing under the mask. Does not bracket an
// action.
bool liftPixels(Document& doc, ls::LayerId layer, const ls::IntervalSet& mask, Floating* out);

// Puts a clip on `layer` as floating pieces, where it was copied from.
bool floatClip(Document& doc, ls::LayerId layer, const PixelClip& clip, Floating* out);

// Moves the pieces to `offset` from where they started.
bool moveFloating(Document& doc, Floating& floating, ls::Vec2i offset);

// Mirrors or turns the pieces in place, about the middle of what floats. A
// shape along for the ride is mirrored or turned too, by its corners.
enum class FloatTurn { FlipHorizontal, FlipVertical, Clockwise, Anticlockwise, HalfTurn };
bool turnFloating(Document& doc, Floating& floating, FloatTurn turn);

// Where the pieces are now, as a canvas mask, for the marching ants.
ls::IntervalSet floatingMask(const Floating& floating);

// Drops the pieces where they are: clipped to the canvas, each joining the
// run straight below it if that is its own colour. Leaves `floating`
// inactive.
bool dropFloating(Document& doc, Floating& floating);

} // namespace fast
