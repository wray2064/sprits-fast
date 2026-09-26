// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors
#pragma once

// slice_tool.h — drawing, moving and naming slices (see app/slices.h).
//
// The slice tool drags out a new slice, or takes an existing one: its inside
// to move it, a corner to resize it. Every change is one undo step. The
// slices window names them and gives them a nine-slice centre and a pivot.
// On the canvas every slice shows as a thin frame in its colour, the one
// being edited with its corners and its centre's lines.

#include "ui/canvas_view.h"
#include "ui/editor.h"

#include <imgui.h>

namespace fast {

// The tool's input: true when it took the press or drag.
bool handleSliceInput(Editor& editor, CanvasView& canvas, bool overCanvas);

// Delete removes the slice being edited: true when it did.
bool handleSliceKeys(Editor& editor);

void drawSliceOverlay(Editor& editor, const CanvasView& canvas, ImDrawList* draw, ImVec2 origin,
                      float zoom);

// The slices window: the list, and the one being edited.
void drawSlicesPanel(Editor& editor, CanvasView& canvas);

} // namespace fast
