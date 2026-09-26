// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors
#pragma once

// ink.h — many colours on one layer.
//
// A layer's pixels used to be one region with one fill, so the layer was one
// colour and a character in eight colours was eight layers. Every other pixel
// editor lets a pencil lay down any colour anywhere on a layer, and a person
// coming from one of them hits that wall in the first minute.
//
// A colour is an *ink*: a palette slot, or a literal colour where there is no
// slot. What the pencil draws goes into a *run*: a freehand element -- a
// region made of strokes (see paint.h) and a fill that colours them -- and a
// run is one ink. Nothing is baked: each colour is a standing rule about the
// strokes, a slot still recolours everything painted through it, and swapping
// palettes still recolours from the drawing.
//
// Paint lands on top. Painting in the ink of the layer's topmost run adds to
// that run; any other ink starts a run above everything already on the
// layer. So painting red over blue covers the blue rather than cutting it
// away, and removing the red brings the blue back -- the element list is a
// stack of marks, each still what it was when it was made.
//
// Erasing reaches everything under the eraser on the layer, and leaves each
// thing made of what it was made of: a thin line is cut, an area trimmed, a
// wide stroke, a shape or a fill keeps the eraser's mark as what was erased
// from it (see eraseFromRegion). An erase belongs to what it erased, so it
// never reaches anything drawn later -- or anything drawn beside it.

#include "app/document.h"
#include "app/paint.h"

#include <array>
#include <map>
#include <vector>

namespace fast {

// What the pencil paints with. A slot when there is one, so the pixel follows
// the palette; the colour is the literal and the fallback, the rule every fill
// follows.
struct Ink {
    ls::Color     colour { 0, 0, 0, 255 };
    ls::ColorRole role = ls::kColorRoleNone;

    bool usesSlot() const { return role != ls::kColorRoleNone; }
};

bool operator==(const Ink& a, const Ink& b);
inline bool operator!=(const Ink& a, const Ink& b) { return !(a == b); }

// The ink a freehand solid element paints with. False for anything that is
// not a solid fill (a dither has a ramp, not a colour).
bool inkOfElement(Document& doc, ls::OperationId fill, Ink* out);

// One stroke's worth of painting, resolved once at the start.
//
// `target` is the run the stroke paints into; invalid for an eraser. The open
// paths are the marks being drawn, one for each mirror image, so a drag is one
// mark however many frames it took: which stroke of the run each is, and the
// pixel it last reached, so the next piece carries on from it.
struct InkStroke {
    ls::LayerId layer;
    PaintLayer  target;
    bool        eraser = false;
    struct Open {
        int       index = -1;
        ls::Vec2i last { 0, 0 };
        PenBrush  brush;
    };
    std::array<Open, 4> open;
    bool erasing() const { return eraser; }
};

// Starts painting `ink` onto `layer`: into the layer's topmost run if it is
// this ink, or a new run on top. Does not bracket an action; the caller holds
// one open for the whole stroke, and a new run made here joins it.
bool beginInkStroke(Document& doc, ls::LayerId layer, const Ink& ink, InkStroke* out);

// Starts painting with one particular element's rule, whatever it is -- a
// dithered element picked in the element list paints dither. Into the element
// itself when it is a run; a fill or an area gets a run of its own on top,
// coloured by a copy of the rule.
bool beginElementStroke(Document& doc, const PaintLayer& element, InkStroke* out);

// Starts erasing everything on `layer` the eraser passes over.
bool beginEraseStroke(Document& doc, ls::LayerId layer, InkStroke* out);

// Draws along a path: `centres` are the pixels the brush is stamped on, in
// order, in the layer's own space. It carries on open path `copy` when it
// starts where that one stopped with the same brush, and starts a new mark
// otherwise. Erasing, it takes the brush's pixels from everything under them.
bool strokeAlong(Document& doc, InkStroke& stroke, int copy, const std::vector<ls::Vec2i>& centres,
                 const PenBrush& brush);

// A spray's dots, each on its own.
bool sprayDots(Document& doc, const InkStroke& stroke, const std::vector<ls::Vec2i>& dots);

// Ends the open paths, so the next piece starts a mark of its own.
void closePaths(InkStroke& stroke);

// Lays pixels down whole, as an area -- a filled selection, a lasso, a stamp
// of a custom brush -- or, erasing, takes them away.
bool strokeInk(Document& doc, const InkStroke& stroke, const std::vector<ls::Vec2i>& pixels);

// Takes `pixels` from everything on `layer` that draws them, each keeping what
// it is made of (see eraseFromRegion); `eraser` is the mark that took them.
bool eraseFromLayer(Document& doc, ls::LayerId layer, const ls::PenStroke& eraser,
                    const ls::IntervalSet& pixels);

// Recolours one solid element: its literal colour and the slot it paints
// through. The drawing is not touched -- this is Aseprite's "replace colour",
// except that it is a parameter and can be changed back. False for a dither.
bool setElementInk(Document& doc, ls::OperationId fill, const Ink& ink);

// Removes the elements of `layer` that no longer draw anything -- erased
// away, or never drawn in -- so the element list does not fill up with
// marks that are not there. Keeps `keep`, and never removes a layer's last
// element. A dither is never removed this way: its settings are work, even
// with nothing under them yet. Returns how many went.
int pruneEmptyInks(Document& doc, ls::LayerId layer, ls::OperationId keep = {});

// --- ink modes ------------------------------------------------------------------
//
// What a pencil stroke is allowed to touch, and with what. Aseprite calls these
// inks; here, where "ink" is already a colour, they are modes of one.
//
//   Simple      paint every pixel the brush covers
//   LockAlpha   paint only where the layer already draws -- recolour a shape
//               without spilling past its edge
//   Replace     paint only pixels of the second colour, turning them to the
//               first
//   Shading     step each pixel along the palette: to the next slot in the
//               palette's order, or the one before with the other button.
//               Once per pixel per stroke, so going over it again does not
//               keep stepping.
//
// Every mode keeps the stroke a path. The filtered ones put it in a run
// clipped to what the layer held when the stroke began (see
// ls::LSContext::setRegionClip) -- where anything draws, where the second
// colour shows, where each slot shows -- so what they paint stays a stroke,
// and stays over what it painted, wherever the layer is turned. A selection
// is a clip too: the stroke is kept inside it the same way.
enum class InkMode { Simple, LockAlpha, Replace, Shading };

struct InkModeState {
    InkMode         mode = InkMode::Simple;
    ls::LayerId     layer;
    Ink             second;           // Replace: the colour it replaces
    bool            forward = true;   // Shading: up the palette, or down
    // What the layer held when the stroke began, in draw order: what a clip
    // names, and -- its pixels then -- which slots a stroke passes over.
    struct Under {
        ls::RegionId    region;
        ls::GeometryId  shape;        // a line or a curve, which has no region
        Ink             ink;
        bool            solid = false;
        bool            erase = false;
        ls::IntervalSet pixels;
    };
    std::vector<Under> under;
    // The selection, when there is one, as a shape; each run kept inside it
    // gets a copy of its own, so it goes when that run does.
    bool            selected = false;
    ls::AreaDesc    selection;
    bool nothing = false;             // the mode allows nowhere: nothing is painted
    // Shading: a run for each slot stepped from, and the stroke so far, laid
    // again into a run made partway through it.
    std::map<ls::ColorRole, InkStroke> strokes;
    struct Piece {
        enum class Kind { Path, Dots, Area } kind = Kind::Path;
        int                    copy = 0;
        std::vector<ls::Vec2i> points;
        PenBrush               brush;
    };
    std::vector<Piece>         pieces;
    std::vector<ls::ColorRole> ramp;          // the palette, in order
    std::vector<ls::Color>     rampColours;
};

// Captures what the mode needs from `layer` as the stroke starts, leaving out
// `own`, the run the stroke paints into. `second` is the colour Replace
// replaces; `palette` is the palette in display order, for Shading;
// `selection`, in the layer's own space, keeps the stroke inside it.
bool beginInkMode(Document& doc, ls::LayerId layer, InkMode mode, const Ink& second,
                  const std::vector<std::pair<ls::ColorRole, ls::Color>>& palette,
                  InkModeState* out, ls::OperationId own = {},
                  const ls::IntervalSet* selection = nullptr);

// Keeps `stroke` to what the mode and the selection allow: its run clipped
// so -- that run itself when it already is, or has nothing in it yet, and a
// new run of its colour on top otherwise. Shading's runs are its own (see
// shadeMark), which only take the selection from here.
bool keepStrokeToMode(Document& doc, InkModeState& state, InkStroke& stroke);

// Shading: a path, dots or an area stepped along the palette where it
// passes -- for each slot it passes over, a run of the next slot (the one
// before, going down) clipped to where that slot showed, each given the
// whole stroke. Once per pixel per stroke, since the clips are of what showed
// when it began.
bool shadeMark(Document& doc, InkModeState& state, const InkModeState::Piece& piece);

// Lays `pixels` down whole, as an area, as the mode says: into `stroke`'s
// run, which keepStrokeToMode clipped, or for Shading through shadeMark.
bool strokeInkMode(Document& doc, InkModeState& state, const InkStroke& stroke,
                   const std::vector<ls::Vec2i>& pixels);

// The freehand elements of a layer that paint `ink`, for tests and the panel.
std::vector<ls::OperationId> elementsWithInk(Document& doc, ls::LayerId layer, const Ink& ink);

// The ink under a canvas pixel on `sprite`, looked up in the drawing rather
// than read off the compiled picture: the topmost visible layer's element
// covering that pixel. That is what lets the picker hand back a slot rather
// than only a colour. False where nothing with an ink is, and the caller falls
// back to the colour.
bool inkAt(Document& doc, ls::SpriteId sprite, ls::Vec2i pixel, Ink* out);

} // namespace fast
