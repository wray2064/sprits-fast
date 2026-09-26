// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors
#pragma once

// floating.h — selected pixels on the move, and the clipboard.
//
// Moving a selection is where a pixel editor usually stops pretending: the
// pixels are cut into a floating bitmap, dragged, and stamped down. Here the
// pixels a layer holds are regions, one per colour, and the move is done in
// those terms. Lifting takes the selected pixels out of each colour's region
// and gives each colour a *piece*: an element of its own at the top of the
// layer, in the same colour or the same dither, which the drag then moves by
// rewriting its runs. Nothing is rasterised on the way -- a piece through a
// palette slot is still through that slot while it floats, so a palette swap
// mid-drag recolours it like everything else.
//
// While it floats a piece sits over the layer without disturbing it, so
// dragging across other pixels does not eat them. Dropping is where it joins
// the layer: its pixels leave every other colour of the layer, and a piece
// whose colour the layer already has merges into that colour's element.
//
// The whole float -- lift, drag, turn, drop -- happens inside one history
// action that the caller holds open, so one Ctrl+Z undoes a move and Escape
// abandons it with nothing to clean up.
//
// Layers with a transform refuse: a selection is drawn in canvas pixels and a
// rotated layer's pixels are not canvas pixels, so any mapping between them
// would be a resample, which is the one thing this program does not do to
// authored pixels.

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
    };
    // A shape the selection wholly contains goes along with the pixels -- as a
    // shape: its geometry is moved, so it is still a rectangle when it lands.
    struct Shape {
        ShapeLayer  shape;
        ShapeParams original;
    };
    // What was erased from the shapes that go along goes with them, as an
    // erase of its own that moves with the pieces -- so a rectangle with a
    // corner rubbed out lands with the corner still rubbed out.
    struct Erase {
        ls::OperationId clear;
        ls::RegionId    region;
        ls::IntervalSet original;
    };
    ls::LayerId        layer;
    std::vector<Piece> pieces;
    std::vector<Shape> shapes;
    Erase              erase;
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

// Drops the pieces into the layer: each leaves the layer's other colours
// where it lands, joins its own colour's element if the layer has one above
// every shape, and is clipped to the canvas. Leaves `floating` inactive.
bool dropFloating(Document& doc, Floating& floating);

} // namespace fast
