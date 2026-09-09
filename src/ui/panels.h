// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors
#pragma once

// panels.h — the parts of the window, each one drawn by one function.
//
// Split out of the main loop so that the loop reads as layout and input, and so
// that a panel can be changed without scrolling past the file dialogs to find
// it.

#include "ui/editor.h"

namespace fast {

// The vertical strip of tools down the left edge.
void drawToolbar(Editor& editor);

// Tool options, the colour, and the palette. What the current tool will do.
void drawToolPanel(Editor& editor, CanvasView& canvas);

// The palette: the document's named colours, and which one the layer paints
// through. Changing an entry recolours every layer using it.
void drawPalettePanel(Editor& editor, CanvasView& canvas);

// The layer stack, topmost first.
void drawLayerPanel(Editor& editor, CanvasView& canvas);

// The transform list: a stack that can be edited, not a history of actions.
void drawTransformPanel(Editor& editor, CanvasView& canvas);

// The bar along the bottom: where the cursor is, how big the canvas is, what
// the last compile cost.
void drawStatusBar(Editor& editor, const CanvasView& canvas);

} // namespace fast
