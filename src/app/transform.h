// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors
#pragma once

// transform.h — rotating and scaling without spending anything.
//
// This is the reason LiveSprite exists, so it is worth being precise about what
// is different.
//
// In a conventional editor, rotating a layer resamples its pixels and replaces
// them. The old pixels are gone. Rotate 45 degrees, change your mind, rotate
// back: what returns is a blurred, chewed-up version of what you had, because
// each turn resampled the result of the last one.
//
// Here a rotation is an *operation in the layer's stack*, resolved during the
// compile from the region and fill that were authored. Changing the angle
// changes a parameter and recompiles from source. Setting it back to zero gives
// back the original pixels exactly -- not approximately -- because the original
// was never overwritten. Removing the operation does the same. There is no
// accumulated loss because there is no accumulation: only ever one transform
// applied to untouched source.
//
// The practical consequence for the interface is that transforms are a *list you
// can edit*, not a series of actions you have performed.

#include "app/document.h"

#include <string>
#include <vector>

namespace fast {

enum class TransformKind { Rotate, Scale, Mirror };

// One transform in a layer's stack, with whatever parameters its kind carries.
struct TransformEntry {
    ls::OperationId id;
    TransformKind   kind = TransformKind::Rotate;

    float           angleDegrees = 0.f;             // Rotate
    ls::Vec2f       factor { 1.f, 1.f };            // Scale
    ls::MirrorAxis  axis = ls::MirrorAxis::X;       // Mirror
    ls::Vec2f       pivot;
    // How a rotation or scale picks each pixel. RotSprite is the one made for
    // pixel art: clean diagonals, no colour the drawing did not have.
    ls::SamplingPolicy sampling = ls::SamplingPolicy::Coverage;

    std::string label() const;
};

// Adding. Each appends to the layer, after the fill, because a transform acts on
// what the layer has already resolved. None of these bracket an undo action: the
// caller decides what one action is.
ls::OperationId addRotate(Document& doc, ls::LayerId layer, float degrees, ls::Vec2f pivot,
                          ls::SamplingPolicy sampling = ls::SamplingPolicy::Coverage);
ls::OperationId addScale(Document& doc, ls::LayerId layer, ls::Vec2f factor, ls::Vec2f pivot,
                         ls::SamplingPolicy sampling = ls::SamplingPolicy::Coverage);
ls::OperationId addMirror(Document& doc, ls::LayerId layer, ls::MirrorAxis axis,
                          ls::Vec2f pivot);

// What is currently on this layer, in the order it resolves.
std::vector<TransformEntry> listTransforms(Document& doc, ls::LayerId layer);

// Editing, which is the whole point: these drive a parameter on an operation
// that already exists rather than adding another one. Spinning a slider through
// a hundred angles leaves one operation, and the hundredth compile is exactly as
// clean as the first.
bool setRotateAngle(Document& doc, ls::OperationId op, float degrees);
bool setScaleFactor(Document& doc, ls::OperationId op, ls::Vec2f factor);
bool setTransformPivot(Document& doc, ls::OperationId op, ls::Vec2f pivot);
bool setTransformSampling(Document& doc, ls::OperationId op, ls::SamplingPolicy sampling);

bool removeTransform(Document& doc, ls::LayerId layer, ls::OperationId op);
bool clearTransforms(Document& doc, ls::LayerId layer);

// Where a point on the canvas lands in the layer's own, untransformed space.
//
// A layer being shown rotated is still drawn on straight: the pencil writes into
// the region, which the transform then acts on. Without this the cursor and the
// mark would part company as soon as an angle was set. Returns the point
// unchanged when the layer has no transforms, and reports failure only when the
// composed transform cannot be inverted -- a scale of zero, say.
bool mapCanvasPointToLayer(Document& doc, ls::LayerId layer, ls::Vec2f canvasPoint,
                           ls::Vec2f* out);

// The composed transform of a layer, source to canvas.
ls::Mat3f layerTransform(Document& doc, ls::LayerId layer);

} // namespace fast
