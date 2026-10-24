// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors
#pragma once

// app_extension.h — Fast's window, for a program built on it.
//
// Fast is the editor; a program that is Fast and more -- more sources to draw
// from, more ways out -- runs the same window and adds to it: its own name on
// the title bar, menus after Fast's, panels beside Fast's that dock like
// them. Fast itself runs with nothing added (fast_main.cpp).

#include "app/theme_tokens.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

struct ImDrawList;

namespace fast {

struct Editor;
class CanvasView;

// --- stages ---------------------------------------------------------------------------
//
// A program whose work has an order -- prepare, import, draw, arrange -- can give the
// window a bar of numbered stages. A stage is a saved arrangement of the dock: which
// panels show and where, over the one document. Any stage can be opened at any time;
// the bar says where the work is and what is next, and locks nothing. (Affinity's
// personas are the same mechanism without the order.)

// Where a panel goes in the dock. Left and Right are columns; the Lower ones sit under
// them, Bottom spans the width under the canvas.
enum class DockRegion { Left, LeftLower, Right, RightLower, Bottom };

struct StagePanel {
    // The panel's window id, the part after ### in its title ("###tool",
    // "###pract.importer"). Fast's own panels have ids in ui/layout.h (panel::).
    std::string id;
    DockRegion  region = DockRegion::Right;
};

struct Stage {
    std::string id;                      // "import": for scripts and for saved state
    std::string title;                   // what the bar says
    // Fast's own arrangement under the stage (a LayoutPreset: 0 Drawing, 1 Animation,
    // 2 Compact), with the panels below added to it; or -1: the stage is exactly the
    // panels listed, Fast's included, and the Fast panels it does not list are closed.
    int         base = -1;
    std::vector<StagePanel> panels;
    float       sideWidth = 1.f;         // the columns' width, against Fast's own
};

struct AppExtension {
    std::string title = "Sprit's'fast";
    // The folder its settings, recent files, layout and recovery copies live
    // in (see setPreferencesFolder). Empty shares Fast's.
    std::string preferencesFolder;
    // The window's icon, as PNG bytes (square; 256 on a side does for every
    // size). Empty: Fast's. The icon Windows shows for the executable itself
    // is a resource in that executable (see sprits_fast.rc).
    std::vector<uint8_t> icon;
    // What Help > About says. Fast's own unless a program built on it names
    // its own; an empty link is not shown.
    std::string version;                       // empty: the version Fast was built as
    std::string homepage = "https://github.com/wray2064/sprits-fast";
    std::string issues = "https://github.com/wray2064/sprits-fast/issues";
    std::string licence = "Apache License 2.0";
    // Colours its windows draw with that Fast has no role for (timeline tracks,
    // reference tints), as roles like Fast's own: named, themable, listed in
    // Preferences > Appearance, kept in theme files, checked for readability.
    // Drawn with theme::extra("pract.track"), never with a literal.
    std::vector<ExtraRole> roles;
    // Built-in themes of its own, each the text of a `.lstheme` file that starts
    // from one of Fast's (`base =`) and says what it changes -- and may set its
    // own roles. They are listed with Fast's, after its dark or light ones.
    std::vector<std::string> themes;
    // Inside the main menu bar, after Fast's own menus.
    std::function<void(Editor&, CanvasView&)> menus;
    // Once a frame, after Fast's panels: windows of its own.
    std::function<void(Editor&, CanvasView&)> panels;
    // Inside the Timeline window, in place of Fast's strip: a program with an
    // animation system of its own draws it here, where the timeline docks. It
    // may still call Fast's drawTimelinePanel (ui/panels.h) for any part of it.
    std::function<void(Editor&, CanvasView&)> timeline;
    // Under the artwork, in the canvas window, for the frame on show: pictures the
    // program wants behind the drawing -- a reference per frame, a guide that
    // follows the animation. Called each time the canvas is drawn, after Fast's
    // own references that sit behind it and before the onion skin. It is given
    // the draw list and the canvas's origin (screen pixels of canvas pixel 0,0)
    // and zoom exactly as Fast's references get them, so a picture placed in
    // canvas pixels lines up with the drawing at every zoom; `frame` is the index
    // of the frame on show (as Editor::frames counts them). Textures are the
    // program's own to make and keep (CanvasView::extensionTextures() is there
    // for the purpose; Fast never prunes it).
    std::function<void(Editor&, CanvasView&, ImDrawList*, float originX, float originY,
                       float zoom, int frame)> underlay;
    // A bar of numbered stages over the dock (see Stage). Empty: no bar, and the window
    // is Fast's. `startStage` is the one the window opens in; `stageStatus` is the
    // line the bar shows for the stage in force ("8 references, 3 on frames").
    std::vector<Stage> stages;
    int startStage = 0;
    std::function<std::string(Editor&)> stageStatus;
};

// Runs the editor: its command line, its window, until it is closed. What
// main() does; the exit code is main's.
int runApp(int argc, char** argv, const AppExtension& extension);

} // namespace fast
