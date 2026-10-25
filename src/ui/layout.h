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

#include <string>
#include <vector>

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
constexpr const char* kPuppet     = "###puppet";
constexpr const char* kReferences = "###references";
constexpr const char* kTimeline   = "###timeline";
constexpr const char* kCanvas     = "###canvas";
} // namespace panel

// --- named layouts ---------------------------------------------------------------
//
// Three arrangements the program builds itself, and the person's own: Save
// current layout keeps the dock as it is (ImGui's own layout text, with which
// panels are open) under a name, in `<settings>/layouts`, and choosing it puts
// it back. A saved layout is only ever read as a layout: bounded, and by a
// plain id that is a file name.

enum class LayoutPreset { Drawing = 0, Animation = 1, Compact = 2 };
const char* presetName(LayoutPreset preset);

// Rebuilds the dock as `preset` on the next frame, with the panels it wants open.
void useLayoutPreset(Editor& editor, LayoutPreset preset);

struct SavedLayout {
    std::string id;     // "my-layout": the file's name
    std::string name;   // "My layout"
};

// Where this run keeps them: the person's settings, or beside a scripted run.
std::string layoutsFolderFor(const Editor& editor);
std::vector<SavedLayout> savedLayouts(const std::string& folder);
bool saveLayoutAs(const std::string& folder, const std::string& name, std::string* savedId,
                  std::string* error);
bool deleteSavedLayout(const std::string& folder, const std::string& id);
// Queues a saved layout; applyPendingLayout, called before the frame begins,
// puts it in force.
void useSavedLayout(Editor& editor, const std::string& folder, const std::string& id);
void applyPendingLayout(Editor& editor);

// The dialog behind Window > Layouts > Save current layout: a name, and Save.
void drawSaveLayoutDialog(Editor& editor);

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

// Begins a docked panel the way Fast's own are: `panelFlags()`, and a panel
// narrower than its contents scrolls sideways rather than cutting a label or a
// button off at its edge. Use it in place of ImGui::Begin(title, open, flags),
// and end it with ImGui::End(), as for any window; true while the panel shows.
//
// A window that scrolls sideways lays its full-width items (a row of buttons that
// share the width, a wrapped hint, a line) out to the widest thing it held last
// frame, so one that once overflowed would keep every row that wide for good. A
// panel is begun here with that undone: what fills the width fills the width the
// person can see, and only what is fixed wider than that is scrolled to.
bool beginPanel(const char* title, bool* open);

} // namespace fast
