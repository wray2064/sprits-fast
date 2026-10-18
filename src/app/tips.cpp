// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

#include "app/tips.h"

namespace fast {

const std::vector<std::string>& tips() {
    static const std::vector<std::string> all = {
        "Right-click on the canvas paints with the second colour, and X swaps the two.",
        "Hold Space and drag, or drag with the middle button, to pan the canvas.",
        "Alt+click a layer's eye to show or hide all the other layers.",
        "Shift+click a second frame in the timeline to select a run, then reverse or tween it.",
        "Right-click a frame in the timeline to duplicate, add, move or delete it.",
        "Select pixels and press Ctrl+B to make them a brush.",
        "Drop a file onto the window to open it, as a tab beside your work.",
        "Ctrl+Shift+E exports again, to the same file, with the settings as they are now.",
        "Edit > Fill selection and Stroke selection use the current colour.",
        "A colour that is a palette slot repaints everywhere it was used when the slot changes.",
        "Help > Keyboard shortcuts lists every key, and a click on one opens Preferences to change it.",
        "Drag a panel by its tab to dock it beside another, share its place, or float it over the canvas.",
    };
    return all;
}

const std::string& tipAt(unsigned index) {
    const std::vector<std::string>& all = tips();
    return all[index % all.size()];
}

} // namespace fast
