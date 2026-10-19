// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors
#pragma once

// appearance.h — Preferences > Appearance: choosing, changing and keeping a theme.
//
// A person picks a theme, changes its accent with a click, changes any single
// role with a colour picker, and sees every change at once on the real window.
// What they have changed is held as a working copy and shown as such until it
// is saved as a theme of their own (a `*.lstheme` file in their settings, which
// they can copy, edit and send); the built-in themes are never written over.
// Every change is measured against the contrast targets (app/theme_tokens.h):
// the tab says which pairs are hard to read and can move them to the nearest
// colour that is not -- it warns, it never forbids.

#include "ui/canvas_view.h"
#include "ui/editor.h"

#include <string>

struct SDL_Window;

namespace fast {

// What a frame of the tab did, for the Preferences window to act on.
struct AppearanceResult {
    bool changed = false;        // something to save to the settings
    bool themeChanged = false;   // the canvas colours may need re-applying
};

// Where this run keeps themes: the person's settings folder, or, for a run that
// never read their settings (a script, a capture), a folder beside it so that
// theirs is not touched.
std::string themeFolderFor(const Editor& editor);

// Reads the person's themes and puts the one they chose in force. Once, after
// the settings are read; a theme that is gone falls back to Slate.
void loadAppearance(Editor& editor);

// Puts `theme` in force as the chosen one: it becomes the working copy,
// Preferences keeps its id, and canvas colours that were still the old theme's
// follow the new one.
void selectTheme(Editor& editor, const Theme& theme);

// One role of the working theme set outright, shown at once and noted as
// unsaved: what the colour picker in the tab does, for a caller (a script) that
// has no picker to hand.
void editRole(Editor& editor, Token token, Rgba colour);
// The same for a role a program built on Fast registered (an `ExtraRole` id).
// Nothing happens for an id that is not registered.
void editExtraRole(Editor& editor, const std::string& id, Rgba colour);

// The tab itself.
AppearanceResult drawAppearanceTab(Editor& editor, CanvasView& canvas, SDL_Window* window);

// A theme file the person chose: read, kept among their themes and chosen.
// An answer to the native Open dialog (see processDialogResult).
void importThemeFrom(Editor& editor, const std::string& path);
// The working theme written to a file the person chose.
void exportThemeTo(Editor& editor, const std::string& path);

} // namespace fast
