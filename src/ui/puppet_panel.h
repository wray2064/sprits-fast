// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors
#pragma once

// puppet_panel.h — building, posing and capturing a cutout puppet (puppet.h)
// by hand.
//
// The panel lists the document's parts as the puppets they hang in, and
// says what the canvas shows:
//
//   - Frames: the animation, as ever. Pick a layer and make it a part.
//   - Draw part: the picked part, with every tool, its joint and sockets
//     marked -- dragged to move them, placed with a click. A posed root is
//     drawn at rest (its pose is its placement, which the tools do not see).
//   - Pose: the whole puppet as posed. Click a part to pick it; the ring
//     round its joint turns it, Shift for fifteen-degree steps; a root's
//     middle square moves the puppet by whole pixels, Shift along one axis;
//     Esc drops a drag. Capture makes a frame of the pose after the one
//     showing, and the next capture goes after that.

#include <imgui.h>

namespace fast {

struct Editor;
class CanvasView;

void drawPuppetPanel(Editor& editor, CanvasView& canvas);

// The canvas's input while a puppet is shown; true when it took it.
bool handlePuppetInput(Editor& editor, CanvasView& canvas, bool overCanvas);

void drawPuppetOverlay(Editor& editor, CanvasView& canvas, ImDrawList* draw, ImVec2 origin,
                       float zoom);

// Sets the canvas to what the panel says it shows: the puppet under the
// picked part in Pose, else nothing in place of the sprite. Once a frame,
// before the canvas draws; it also drops back to Frames when the part has
// gone or another frame was picked in the timeline.
void settlePuppetView(Editor& editor, CanvasView& canvas);

} // namespace fast
