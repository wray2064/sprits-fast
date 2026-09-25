// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors
#pragma once

// shape_tools.h — shapes placed point by point, and every shape's handles.
//
// The polygon and the curve are placed a click at a time: a polygon corner by
// corner, a curve anchor by anchor, each press dragged out into that anchor's
// handle as in any vector editor. Enter, a double-click or a click on the
// first point finishes; Escape lets it go; Backspace takes the last point back.
//
// Once placed, a shape of any kind keeps handles on the canvas while it is the
// element being edited: a box's corners, a line's ends, a polygon's corners, a
// curve's anchors and control points. Dragging one drives the geometry, so
// everything built on the shape follows -- which is the point of shapes that
// stay shapes, and what no bitmap editor can offer after the mouse is let go.

#include "app/shape.h"
#include "ui/canvas_view.h"
#include "ui/editor.h"

#include <imgui.h>

namespace fast {

// The active element, when it is a shape with geometry.
bool activeShape(Editor& editor, ShapeLayer* out);

// Puts a finished shape on the active layer (or a layer of its own, as the
// shape options say) in the current colour, and makes it the element being
// edited. One undo step.
bool placeShape(Editor& editor, CanvasView& canvas, ShapeKind kind, const ShapeParams& params);

// The polygon and curve tools: true when they took the input.
bool handlePathInput(Editor& editor, CanvasView& canvas, bool overCanvas);

// Enter, Escape and Backspace while a path is being placed: true when used.
bool handlePathKeys(Editor& editor, CanvasView& canvas);

// Finishes the path being placed -- closed, or open where a curve may be.
bool finishPath(Editor& editor, CanvasView& canvas, bool closed);

// A press on one of the active shape's handles drags it: true while it does.
bool handleShapeHandles(Editor& editor, CanvasView& canvas, bool overCanvas);

// The path being placed, and the active shape's handles, over the canvas.
void drawShapeOverlay(Editor& editor, CanvasView& canvas, ImDrawList* draw, ImVec2 origin,
                      float zoom);

} // namespace fast
