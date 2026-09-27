// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors
#pragma once

// app_extension.h — Fast's window, for a program built on it.
//
// Fast is the editor; a program that is Fast and more -- more sources to draw
// from, more ways out -- runs the same window and adds to it: its own name on
// the title bar, menus after Fast's, panels beside Fast's that dock like
// them. Fast itself runs with nothing added (fast_main.cpp).

#include <functional>
#include <string>

namespace fast {

struct Editor;
class CanvasView;

struct AppExtension {
    std::string title = "Sprit's'fast";
    // The folder its settings, recent files, layout and recovery copies live
    // in (see setPreferencesFolder). Empty shares Fast's.
    std::string preferencesFolder;
    // Inside the main menu bar, after Fast's own menus.
    std::function<void(Editor&, CanvasView&)> menus;
    // Once a frame, after Fast's panels: windows of its own.
    std::function<void(Editor&, CanvasView&)> panels;
    // Inside the Timeline window, in place of Fast's strip: a program with an
    // animation system of its own draws it here, where the timeline docks. It
    // may still call Fast's drawTimelinePanel (ui/panels.h) for any part of it.
    std::function<void(Editor&, CanvasView&)> timeline;
};

// Runs the editor: its command line, its window, until it is closed. What
// main() does; the exit code is main's.
int runApp(int argc, char** argv, const AppExtension& extension);

} // namespace fast
