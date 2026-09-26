// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors
#pragma once

// rulers.h — rulers along the canvas, and the guides pulled out of them.
//
// The top ruler counts columns and the left one rows, in canvas pixels, with
// the pointer's position marked on both. Dragging out of the top ruler pulls
// a guide across the canvas; out of the left one, a guide down it. A guide's
// marker on its ruler drags it again, and dropping it back on the ruler
// takes it away. Every guide change is one undo step (see app/guides.h).

#include "ui/canvas_view.h"
#include "ui/editor.h"

#include <imgui.h>

namespace fast {

// The rulers and their input. Call inside the canvas window after the canvas
// is drawn, so they sit over its edges.
void drawRulers(Editor& editor, CanvasView& canvas);

// The guides, as lines across the canvas's view.
void drawGuides(Editor& editor, const CanvasView& canvas, ImDrawList* draw, ImVec2 origin,
                float zoom);

} // namespace fast
