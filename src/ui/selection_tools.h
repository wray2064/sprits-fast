// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors
#pragma once

// selection_tools.h — the marquee, the lasso, the wand and the move tool, on
// the canvas.
//
// The commands they drive are declared in editor.h and hold no ImGui, so the
// self-test can reach them. What is here is only the part a mouse and a
// keyboard do: which pixels a drag means, and the marching ants that show it.

#include "ui/editor.h"

#include <imgui.h>

namespace fast {

// Takes the canvas input when a selection tool or the move tool is in hand,
// or when a float is being dragged whatever the tool. Returns true when it
// took it, so the drawing tools stand aside.
bool handleSelectionInput(Editor& editor, CanvasView& canvas, bool overCanvas,
                          ls::Vec2i pixel);

// The keys a selection answers to: arrows nudge, Enter drops a float, Escape
// abandons one, Delete clears. Returns true when it used the key press, so the
// other shortcuts do not see it.
bool handleSelectionKeys(Editor& editor);

// Marching ants round the selection, or round the float while one is up, and
// the shape being dragged out. Drawn over the artwork.
void drawSelectionOverlay(const Editor& editor, ImDrawList* draw, ImVec2 origin, float zoom);

} // namespace fast
