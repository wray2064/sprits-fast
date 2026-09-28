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
//     down, the square between them both at once; Shift for quarter steps;
//   - Esc while dragging puts things back as they were.
//
// What it drives is the same operation the panel's number fields edit, so a
// turn made here is one to type over there, and the other way round. A layer
// with a Rotate and no Scale grows one from the arrows, at the same pivot.

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
