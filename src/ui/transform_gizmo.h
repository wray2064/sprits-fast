// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors
#pragma once

// transform_gizmo.h — a layer's rotation and scale, by hand, on the canvas.
//
// A slider cannot land on 90 degrees: its steps are the width of the panel,
// not the angles anyone wants. So with the Move tool in hand, a layer that has
// a Rotate or a Scale (the Transform panel) shows the usual gizmo at their
// pivot:
//
//   - a ring round it: drag it and the layer turns with the pointer, Shift
//     for steps of fifteen degrees -- 90 is then where it stops;
//   - a cross at its middle: the red arrow stretches across, the green one
//     down, Shift for quarter steps; the square between them moves the
//     sprite on the canvas, by whole pixels, Shift along one axis;
//   - Esc while dragging puts things back as they were.
//
// The gizmo sits where the pivot shows -- carried by a move after the turn --
// so it stays on the sprite.
//
// What it drives is the same operation the panel's number fields edit, so a
// turn made here is one to type over there, and the other way round. A layer
// with a Rotate and no Scale grows one from the arrows, at the same pivot, and
// a move is an Offset at the end of the list -- the last one, or a new one.

#include <imgui.h>

namespace fast {

struct Editor;
class CanvasView;

// The gizmo's hold on the pointer: true when it took this frame's input --
// a part hovered or held -- so no tool acts on it too.
bool handleTransformGizmo(Editor& editor, CanvasView& canvas, bool overCanvas);

void drawTransformGizmo(Editor& editor, CanvasView& canvas, ImDrawList* draw, ImVec2 origin,
                        float zoom);

} // namespace fast
