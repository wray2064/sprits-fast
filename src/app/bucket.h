// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors
#pragma once

// bucket.h — filling an area, when there is no bitmap to fill.
//
// A paint bucket normally floods a stored image. There isn't one here: a layer
// is a region and a rule, and the picture only exists once it is compiled. So
// the flood runs over the *compiled* picture to work out what the user meant,
// and the result becomes a fill of its own on the layer, coloured by a rule, so
// the bucket stays as non-destructive as everything else: recolouring
// afterwards costs nothing, and removing the fill brings back what was under it.
//
// It floods what is on screen, including pixels contributed by layers underneath.
// That is deliberate: it is what the user is looking at and pointing to. What it
// found is kept as a shape -- the edge of those pixels and a seed inside -- not as
// the pixels.

#include "app/document.h"
#include "app/ink.h"
#include "app/paint.h"

#include <cstdint>
#include <vector>

namespace fast {

struct BucketSettings {
    // 0 fills only pixels of exactly the colour clicked. Higher values follow a
    // gradient or an anti-aliased edge from somewhere else; pixel art rarely
    // wants it, so it defaults off.
    int tolerance = 0;

    // Whether diagonally touching pixels count as connected. Off by default:
    // a 1px diagonal outline is a wall in pixel art, and leaking through it is
    // the classic paint-bucket annoyance.
    bool diagonal = false;

    // Fill the whole matching colour wherever it appears, rather than only the
    // connected area under the cursor.
    bool global = false;
};

// The pixels a bucket at `seed` would cover, in canvas space. Empty if the seed
// is outside the canvas or nothing matches.
//
// Separated from the filling so it can be tested against a known picture, and so
// a preview could show the area before committing to it.
std::vector<ls::Vec2i> bucketArea(Document& doc, ls::SpriteId sprite, ls::Vec2i seed,
                                  const BucketSettings& settings);

// The pixels of `layer`'s own space a bucket at canvas `seed` would fill:
// the flood of what is on screen, or, on a turned or scaled layer, of the
// layer's own drawing from the point clicked carried back. Kept inside
// `within`, a canvas mask, when it is given.
ls::IntervalSet bucketAreaOnLayer(Document& doc, ls::SpriteId sprite, ls::LayerId layer,
                                  ls::Vec2i seed, const BucketSettings& settings,
                                  const ls::IntervalSet* within = nullptr);

// Floods from `seed` and makes the result a fill of its own on the stroke's
// layer, on top, coloured the way the stroke paints. The fill is a face (see
// the engine's FaceDesc): exactly what the flood found where it was made, and
// found again against the line round it wherever the layer is turned -- so
// it never runs out through a turned outline and never leaves a gap along
// it. Covers what is under it rather than cutting it away. Does not bracket
// an undo action: the caller decides what one action is.
//
// `within`, when given, is a canvas mask the fill stays inside: the selection,
// which every tool respects while there is one. `made`, when given, is the
// fill's element.
bool bucketFill(Document& doc, ls::SpriteId sprite, const InkStroke& stroke,
                ls::Vec2i seed, const BucketSettings& settings,
                const ls::IntervalSet* within = nullptr, PaintLayer* made = nullptr);

// The same, into one particular element of the layer.
bool bucketFill(Document& doc, ls::SpriteId sprite, const PaintLayer& target,
                ls::Vec2i seed, const BucketSettings& settings);

} // namespace fast
