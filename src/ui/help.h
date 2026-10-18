// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors
#pragma once

// help.h — the Help menu: what this program is, and every key it answers to.
//
// About says the name, version, licence and what Fast is built on, with the
// links a person needs to read the docs or report a problem. The shortcuts
// window lists every command that has a key, read from the same table the
// Preferences window edits, so what it shows is what the keys do *now*,
// including the person's own changes.

#include "ui/editor.h"

struct SDL_Window;

namespace fast {

// Opens Command search with nothing typed and the first command chosen.
void openCommandSearch(Editor& editor);

// The Help menu, inside the menu bar, after every other menu.
void drawHelpMenu(Editor& editor);

// The windows it opens; once a frame.
void drawHelpWindows(Editor& editor, SDL_Window* window);

} // namespace fast
