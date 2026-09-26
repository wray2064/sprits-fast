// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors
#pragma once

// layout.h — where the panels are.
//
// Panels dock, tab, float and resize the way they do in every other editor:
// drag one by its tab to the side of another, onto another to share its place
// as a tab, or out over the canvas to float it; drag the line between two to
// resize them. The canvas keeps the middle and cannot be covered or moved.
//
// The arrangement is kept between sessions -- beside the preferences, in
// ImGui's own layout file -- together with which panels are open. Window >
// Reset layout puts the default back. A run driven by a script or captured
// for a picture always starts from the default, so what it checks does not
// depend on how someone last left their panels.

#include "ui/editor.h"

#include <imgui.h>

namespace fast {

// The IDs panels are known by: the part after ### in their titles, so a
// translated title is still the same panel in the layout file.
namespace panel {
constexpr const char* kTool       = "###tool";
constexpr const char* kPalette    = "###palette";
constexpr const char* kLayers     = "###layers";
constexpr const char* kElements   = "###elements";
constexpr const char* kProperties = "###properties";
constexpr const char* kTransform  = "###transform";
constexpr const char* kReferences = "###references";
constexpr const char* kTimeline   = "###timeline";
constexpr const char* kCanvas     = "###canvas";
} // namespace panel

// Tells ImGui about the panels' open flags, so they are kept in the layout
// file with the arrangement. Call once, after the ImGui context is made.
void registerPanelSettings(Editor& editor);

// Reads the arrangement kept from the last session, if there is one. Call
// before the first frame, and only for an interactive run.
void loadLayout();

// Writes the arrangement when it has changed. Call once a frame.
void saveLayoutWhenChanged();

// The dock space over `pos`, `size`: every panel docks into it, around the
// canvas in the middle. Built as the default arrangement the first time, and
// again after Window > Reset layout.
void drawDockSpace(Editor& editor, ImVec2 pos, ImVec2 size);

// The flags a panel window is begun with: it moves, resizes, docks and tabs.
ImGuiWindowFlags panelFlags();

} // namespace fast
