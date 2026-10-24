// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors
#pragma once

// tabs.h — several documents open at once.
//
// The editor works on one document: everything a tool, a panel or a shortcut
// touches is on the Editor. So a document that is open but not on screen is
// parked -- its document, layers, frames, selection, references, history and
// autosave copy moved into a DocumentTab -- and swapped back in when its tab
// is chosen. Nothing else in the program has to know there is more than one.
//
// The canvas's caches are keyed by the engine's ids, and every document has
// an engine of its own whose ids start again, so a switch empties them.
// Clipboard pixels travel between tabs; the dithers they name belong to the
// document they came from, so across tabs they paste as their colours.

#include "ui/canvas_view.h"
#include "ui/editor.h"

#include <functional>
#include <string>

struct SDL_Window;

namespace fast {

// One tab for the document the editor starts with. Call once.
void initTabs(Editor& editor);

size_t tabCount(const Editor& editor);

// A tab's name for the tab bar: the file's name, or "untitled", and whether it
// has unsaved work.
std::string tabName(const Editor& editor, size_t index);
bool tabModified(const Editor& editor, size_t index);

// Whether the document on screen is a blank one nobody has touched, which a
// newly opened file replaces rather than sitting beside.
bool documentIsBlank(const Editor& editor);

// Shows another tab's document. Drops any float first.
void switchToTab(Editor& editor, CanvasView& canvas, size_t index);

// Parks the document on screen and leaves the editor holding nothing, in a
// new tab beside it, for the caller to fill -- with a new document or a file.
// A caller whose fill fails calls closeActiveTab.
void openTab(Editor& editor, CanvasView& canvas);

// Closes the tab on screen, discarding its document without asking -- the
// asking is requestAction's -- and shows its neighbour. The last tab is not
// closed but given a new blank document.
void closeActiveTab(Editor& editor, CanvasView& canvas);

// The first tab other than this one with unsaved work, or -1.
int modifiedOtherTab(const Editor& editor);

// Every tab's autosave copy removed: the program is closing cleanly.
void clearAllRecovery(Editor& editor);

// For a program built on Fast that makes documents of its own (from a template, an
// import): a new document in a tab of its own, as File > New makes one -- a blank
// untouched tab is replaced instead -- and shown fitted. `fill` may change the
// document the new tab holds (frames, cycles, a palette) before it is shown; what it
// does is not an undo step and the document starts unmodified, as a new one does.
// False, with the tab closed again, when it cannot be made or `fill` says it failed.
bool newDocumentTab(Editor& editor, CanvasView& canvas, const FileState::NewDocument& spec,
                    const std::function<bool(Editor&)>& fill);

// The file at `path` opened in a tab of its own, as File > Open does. With `asCopy` it
// has no path -- the first save asks where -- so a template or a model file is never
// written over. False, with the tab closed again, when it cannot be opened.
bool openFileTab(Editor& editor, CanvasView& canvas, const std::string& path, bool asCopy);

// The row of tabs over the canvas.
void drawTabBar(Editor& editor, CanvasView& canvas, SDL_Window* window);

} // namespace fast
