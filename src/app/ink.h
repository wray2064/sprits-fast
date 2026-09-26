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
// The filtered modes lay their pixels down as areas: what they paint is a
// choice of pixels, not a path.
enum class InkMode { Simple, LockAlpha, Replace, Shading };

struct InkModeState {
    InkMode         mode = InkMode::Simple;
    ls::LayerId     layer;
    ls::IntervalSet allowed;      // LockAlpha and Replace: where painting may land
    struct Held { ls::IntervalSet pixels; Ink ink; };
    std::vector<Held> held;       // Shading: each colour's pixels, at the start
    ls::IntervalSet shaded;       // Shading: pixels already stepped this stroke
    std::vector<ls::ColorRole> ramp;          // Shading: the palette, in order
    std::vector<ls::Color>     rampColours;
    std::map<ls::ColorRole, InkStroke> strokes;  // Shading: one per slot stepped to
};

// Captures what the mode needs from `layer` as the stroke starts. `second` is
// the colour Replace replaces; `palette` is the palette in display order, for
// Shading.
bool beginInkMode(Document& doc, ls::LayerId layer, InkMode mode, const Ink& second,
                  const std::vector<std::pair<ls::ColorRole, ls::Color>>& palette,
                  InkModeState* out);

// Lays `pixels` down as the mode says: Simple and the filters through
// `stroke`, Shading through strokes of its own. `forward` is which way
// Shading steps.
bool strokeInkMode(Document& doc, InkModeState& state, const InkStroke& stroke,
                   const std::vector<ls::Vec2i>& pixels, bool forward);

// The freehand elements of a layer that paint `ink`, for tests and the panel.
std::vector<ls::OperationId> elementsWithInk(Document& doc, ls::LayerId layer, const Ink& ink);

// The ink under a canvas pixel on `sprite`, looked up in the drawing rather
// than read off the compiled picture: the topmost visible layer's element
// covering that pixel. That is what lets the picker hand back a slot rather
// than only a colour. False where nothing with an ink is, and the caller falls
// back to the colour.
bool inkAt(Document& doc, ls::SpriteId sprite, ls::Vec2i pixel, Ink* out);

} // namespace fast
