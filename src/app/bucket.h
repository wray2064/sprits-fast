// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors
#pragma once

// bucket.h — filling an area, when there is no bitmap to fill.
//
// A paint bucket normally floods a stored image. There isn't one here: a layer
// is a region and a rule, and the picture only exists once it is compiled. So
// the flood runs over the *compiled* raster to work out which pixels the user
// meant, and the result is added to the layer's region -- the same place the
// pencil writes. The fill rule then colours it, so the bucket stays as
// non-destructive as everything else, and recolouring afterwards costs nothing.
//
// It floods what is on screen, including pixels contributed by layers underneath
// and by transforms above. That is deliberate: it is what the user is looking at
// and pointing to. The pixels it produces are mapped back into the layer's own
// space before being written, exactly as the pencil is, so a bucket on a rotated
// layer lands where it was aimed.

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

// Floods from `seed` and lays the result down with a stroke's ink: into the
// ink's element, and out of the layer's other colours, exactly as the pencil
// does. Does not bracket an undo action: the caller decides what one action is.
bool bucketFill(Document& doc, ls::SpriteId sprite, const InkStroke& stroke,
                ls::Vec2i seed, const BucketSettings& settings);

// The same, into one particular element of the layer.
bool bucketFill(Document& doc, ls::SpriteId sprite, const PaintLayer& target,
                ls::Vec2i seed, const BucketSettings& settings);

} // namespace fast
