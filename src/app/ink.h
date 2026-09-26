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
// The fix keeps the rule rather than bending it. A colour is an *ink*: a
// palette slot, or a literal colour where there is no slot. A layer holds one
// freehand element per ink it has been painted with -- a region and a solid
// fill -- and painting with an ink adds pixels to that ink's region and takes
// the same pixels out of every other freehand element on the layer, so a
// pixel belongs to exactly one of them. Nothing is baked: each colour is still
// a standing rule about a shape, a slot still recolours everything painted
// through it, and swapping palettes still recolours from the drawing.
//
// That is Aseprite's indexed mode without its cost. There, a pixel is an index
// only while the whole sprite is indexed; here every pixel painted through a
// slot is a role whatever else the document does, and a literal colour sits
// beside it without anything having to be converted.
//
// Order is the one subtlety. Elements draw in list order, and a layer can hold
// shapes as well as pixels. Fresh paint must land on top of everything already
// on the layer -- a stroke across a rectangle has to show -- so an ink paints
// into the topmost element of its colour only if no shape sits above it, and
// otherwise gets a new element at the top. Two elements may then share a
// colour; they are still disjoint, so nothing is drawn twice.

#include "app/document.h"
#include "app/paint.h"

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

// One stroke's worth of painting, resolved once at the start so the samples
// that follow do not look the layer up again.
//
// `target` is where pixels go; invalid for an eraser. `others` is every other
// freehand region on the layer, which loses whatever the target gains.
struct InkStroke {
    ls::LayerId               layer;
    PaintLayer                target;
    std::vector<ls::RegionId> others;
    // Erasing only: where the shapes' pixels go (see eraseFromShapes), and
    // what the shapes cover, so pixels with no shape under them are not kept.
    ls::RegionId              shapesErase;
    ls::IntervalSet           shapeCover;
    bool erasing() const { return !target.region.valid(); }
};

// Starts painting `ink` onto `layer`: finds the element that paints it, above
// every shape on the layer, or adds one at the top. Does not bracket an action;
// the caller holds one open for the whole stroke, and a new element made here
// joins it.
bool beginInkStroke(Document& doc, ls::LayerId layer, const Ink& ink, InkStroke* out);

// Starts painting into one particular freehand element, whatever rule colours
// it -- a dithered element picked in the element list paints dither.
bool beginElementStroke(Document& doc, const PaintLayer& element, InkStroke* out);

// Starts erasing: every freehand element on the layer loses the pixels, and
// the shapes lose them through an erase -- a mask over them (see
// eraseFromShapes) -- so they stay shapes that can still be edited.
bool beginEraseStroke(Document& doc, ls::LayerId layer, InkStroke* out);

// What the layer's shapes cover, in its own space.
ls::IntervalSet shapeCoverage(Document& doc, ls::LayerId layer);

// The erase that takes pixels from the layer's shapes: its topmost one with
// no shape above it, or a new one after everything the layer draws -- so a
// shape drawn after an erase is not erased by it. Invalid when the layer has
// no shapes. Does not bracket an action.
ls::RegionId shapesEraseOf(Document& doc, ls::LayerId layer);

// Takes `pixels` out of the layer's shapes without baking them: the pixels
// join the erase, which clears what is drawn before it and nothing after.
// False when no shape is under any of them. Does not bracket an action.
bool eraseFromShapes(Document& doc, ls::LayerId layer, const std::vector<ls::Vec2i>& pixels);

// Lays pixels down, in the layer's own space.
bool strokeInk(Document& doc, const InkStroke& stroke, const std::vector<ls::Vec2i>& pixels);

// Recolours one solid element: its literal colour and the slot it paints
// through. The drawing is not touched -- this is Aseprite's "replace colour",
// except that it is a parameter and can be changed back. False for a dither.
bool setElementInk(Document& doc, ls::OperationId fill, const Ink& ink);

// Removes the solid freehand elements of `layer` that no longer draw anything
// -- painted over entirely, or erased -- so the element list does not fill up
// with the ghosts of colours that were tried. Keeps `keep`, and never removes a
// layer's last element. A dither is never removed this way: its settings are
// work, even with nothing under them yet. Returns how many went.
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
// Shading is where this model shines. The result is not a new colour but the
// neighbouring slot, so a shaded area still recolours with the palette, and a
// ramp laid out in order is the shading scale.
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
// than read off the compiled picture: the topmost visible layer's freehand
// element covering that pixel. That is what lets the picker hand back a slot
// rather than only a colour. False where no freehand pixel is -- a shape, a
// transformed layer, an empty spot -- and the caller falls back to the colour.
bool inkAt(Document& doc, ls::SpriteId sprite, ls::Vec2i pixel, Ink* out);

} // namespace fast
