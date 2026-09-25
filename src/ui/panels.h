// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors
#pragma once

// panels.h — the parts of the window, each one drawn by one function.
//
// Split out of the main loop so that the loop reads as layout and input, and so
// that a panel can be changed without scrolling past the file dialogs to find
// it.

#include "ui/editor.h"

#include <SDL3/SDL.h>

namespace fast {

// The vertical strip of tools down the left edge.
void drawToolbar(Editor& editor);

// Tool options, the colour, and the palette. What the current tool will do.
void drawToolPanel(Editor& editor);

// The palette: the document's named colours, and which one the layer paints
// through. Changing an entry recolours every layer using it.
void drawPalettePanel(Editor& editor, CanvasView& canvas, SDL_Window* window);

// The shape on the active layer, if it has one, and the outline that follows
// whatever the layer draws.
void drawShapePanel(Editor& editor, CanvasView& canvas);

// The layer stack, topmost first.
void drawLayerPanel(Editor& editor, CanvasView& canvas);

// One layer's properties: name, blend, opacity, visibility, lock, a colour
// tag for its row, and notes. Open while editor.propertiesLayer is set.
void drawLayerPropertiesPanel(Editor& editor, CanvasView& canvas);

// An image on the system clipboard, kept as a reference. False, with the
// reason said, when there is none or it cannot be kept.
bool pasteReference(Editor& editor, CanvasView& canvas);

// The pictures being drawn from: import, place, fade, and remove.
void drawReferencePanel(Editor& editor, CanvasView& canvas, SDL_Window* window);

// The two folders: the sprites beside this one, and the images to draw from.
void drawLibraryPanel(Editor& editor, CanvasView& canvas, SDL_Window* window);

// References under and over the artwork, for CanvasView's underlay hooks.
void drawReferences(Editor& editor, CanvasView& canvas, ImDrawList* draw,
                    ImVec2 origin, float zoom, bool behind);

// The transform list: a stack that can be edited, not a history of actions.
void drawTransformPanel(Editor& editor, CanvasView& canvas);

// The corner preview: the sprite at the size it will really be seen, over a
// background the author chooses. Drawn as an overlay inside the canvas window,
// so it must be called while that window is current.
void drawPreviewOverlay(Editor& editor, const CanvasView& canvas);

// The frame strip along the bottom: every frame as a thumbnail, its duration,
// and the playback controls. Thumbnails are the textures the cache already
// holds, scaled down -- drawing eight frames costs eight quads, not eight
// compiles.
void drawTimelinePanel(Editor& editor, CanvasView& canvas);

// How tall the strip needs to be. It grows when a cycle is selected, because a
// cycle's steps are a second row: the frames are what was drawn, the steps are
// the order they play in, and those are different lists.
float timelinePanelHeight(const Editor& editor);

// The frames either side of this one, faint, under the live artwork. Drawn as
// an overlay inside the canvas window, so it must be called while that window
// is current and before the canvas image itself.
void drawOnionSkin(Editor& editor, CanvasView& canvas, ImDrawList* draw,
                   ImVec2 origin, float zoom);

// The sheet export window: what goes in it, how it is arranged, and what it
// will come out as. A modal, because the numbers only mean anything together
// and a person wants to see the result size before committing.
// The animation export: GIF, animated PNG or a numbered sequence, from the
// selected cycle or every frame, at a whole-number scale.
void drawAnimationPanel(Editor& editor, SDL_Window* window);

void drawSheetPanel(Editor& editor, SDL_Window* window);

// The bar along the bottom: where the cursor is, how big the canvas is, what
// the last compile cost.
void drawStatusBar(Editor& editor, const CanvasView& canvas);

} // namespace fast
