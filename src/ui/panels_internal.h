// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors
#pragma once

// panels_internal.h — what the panel files share.
//
// panels.h is the list of panels, each drawn by one function; the functions live in
// panel_tools.cpp (the toolbar and the tool options), panel_palette.cpp,
// panel_elements.cpp (a layer's elements and the picked one's properties),
// panel_layers.cpp (the stack and a layer's own properties), panel_references.cpp (the
// references and the library), panel_transform.cpp and panel_view.cpp (the preview in
// the corner and the status bar). They were one file once. What more than one of them
// needs is here.

#include "ui/panels.h"

#include <imgui.h>

namespace fast {

// Opens a history bracket on the frame a control is grabbed and closes it when
// released, so a whole drag is one undo step rather than none or hundreds.
//
// Every live control in the program needs this, and getting it wrong is invisible
// until somebody tries to undo, so it is written once. Call it straight after the
// control. (`holdForUndo`, in panels.h, is the same thing for code outside the panels.)
void bracketDrag(Editor& editor, bool& flag, const char* label);

// A change made by a control that is not being dragged -- a combo, a checkbox --
// still needs to be one undo step.
void singleAction(Editor& editor, const char* label);

// The pattern, scale and colours of a dithered layer, edited in place (panel_tools.cpp,
// where the Tool panel shows it; panel_elements.cpp, where Properties does for a
// dithered element).
void drawDitherControls(Editor& editor, CanvasView& canvas, const PaintLayer& layer);

} // namespace fast
