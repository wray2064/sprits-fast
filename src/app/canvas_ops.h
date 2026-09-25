// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors
#pragma once

// canvas_ops.h — the canvas itself: its size, its crop, and turning it over.
//
// In a bitmap editor these are image operations: every layer of every frame
// is resampled into a new buffer. Here there is no buffer. What a layer holds
// is regions of authored pixels and shapes described by geometry, so changing
// the canvas is changing those descriptions -- a region's runs move, a shape's
// corners move -- by a mapping that sends a pixel to a pixel. A flip or a
// quarter turn maps the grid onto itself, and a whole-number enlargement maps
// each pixel to a block, so all of these are exact: nothing is sampled, and
// turning a canvas four times gives back the same bytes.
//
// Two things behave differently from the incumbents, both on purpose:
//
// * **Shrinking the canvas keeps what falls outside it.** The pixels are still
//   in their regions, just off the edge; growing the canvas back, or cropping
//   somewhere else, brings them back. Aseprite throws them away.
// * **Shapes stay shapes.** A rectangle on a canvas turned a quarter is a
//   rectangle of the other proportions, still with its corner radius, not a
//   picture of one.
//
// Reducing by a whole number is the one lossy thing here -- two pixels become
// one -- and it is the only one done by sampling. It is offered because people
// need it, and named for what it does.

#include "app/document.h"

namespace fast {

enum class CanvasAnchor { TopLeft, Top, TopRight, Left, Centre, Right,
                          BottomLeft, Bottom, BottomRight };

// Each brackets its own undo action and changes every frame. False, with a
// reason, when the result would be a canvas Fast does not work on.

bool resizeCanvas(Document& doc, uint32_t width, uint32_t height, CanvasAnchor anchor,
                  std::string* error);

// Crops to a rectangle of the current canvas: it becomes the whole canvas.
bool cropCanvas(Document& doc, ls::Rect2i to, std::string* error);

// Crops to the smallest rectangle holding everything any frame draws.
bool trimCanvas(Document& doc, std::string* error);

bool flipCanvas(Document& doc, bool horizontally, std::string* error);

// Quarter turns clockwise: 1, 2 or 3.
bool rotateCanvas(Document& doc, int quarterTurns, std::string* error);

// Enlarges by a whole number, each pixel becoming a block of pixels -- exact.
bool enlargeSprite(Document& doc, uint32_t factor, std::string* error);

// Reduces by a whole number, keeping one pixel of each block -- lossy, and
// the only operation here that is.
bool reduceSprite(Document& doc, uint32_t factor, std::string* error);

// Resizes to any size, nearest neighbour: each new pixel takes the old pixel
// its centre falls in. Growing by the same whole number both ways is
// enlargeSprite, exactly; anything else drops or doubles some rows and
// columns, which is what a nearest-neighbour resize is. Shapes are scaled as
// shapes. Pixels off the canvas are carried along like the rest.
bool resizeSprite(Document& doc, uint32_t width, uint32_t height, std::string* error);

// The smallest rectangle holding everything any frame draws, or an empty one
// for a document that draws nothing.
ls::Rect2i contentBounds(Document& doc);

} // namespace fast
