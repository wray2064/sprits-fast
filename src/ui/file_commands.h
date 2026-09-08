// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors
#pragma once

// file_commands.h — New, Open, Save, and the question in front of them.
//
// Two things make this more than a few menu items.
//
// **The native dialogs are asynchronous.** SDL calls back when the user has
// chosen, possibly on another thread, possibly long after the frame that opened
// the dialog has been drawn. So a choice is parked behind a mutex and picked up
// by the main loop, rather than acted on where it arrives.
//
// **An unsaved document must not be thrown away silently.** New, Open, opening a
// recent file, a dropped file and Quit all have to ask first, and then carry on
// with whatever they were doing once answered. That is what `pending` is: the
// thing to do after the question is settled.

#include "app/recent_files.h"

#include <SDL3/SDL.h>

#include <string>

namespace fast {

class Document;

// What a file dialog came back with. Written from SDL's callback, which may be
// on another thread; read by the main loop.
struct DialogResult {
    enum class Kind { None, Open, SaveAs };

    SDL_Mutex*  mutex = nullptr;
    Kind        kind = Kind::None;
    bool        ready = false;
    bool        cancelled = false;
    std::string path;

    void init()    { mutex = SDL_CreateMutex(); }
    void destroy() { if (mutex) { SDL_DestroyMutex(mutex); mutex = nullptr; } }
};

// What to do once the user has answered "save your changes?".
enum class PendingAction {
    None,
    NewDocument,
    OpenDialog,
    OpenPath,       // a recent entry or a dropped file, in pendingPath
    Quit,
};

struct FileState {
    DialogResult  dialog;
    RecentFiles   recent;

    PendingAction pending = PendingAction::None;
    std::string   pendingPath;
    uint32_t      pendingNewSize = 32;
    bool          askingToSave = false;

    // Set when a save is needed before the pending action can continue, so the
    // save-as dialog knows to resume rather than stop.
    bool resumeAfterSave = false;
};

// Opens the native dialogs. The answer arrives later, in DialogResult.
void showOpenDialog(FileState& state, SDL_Window* window, const Document& doc);
void showSaveAsDialog(FileState& state, SDL_Window* window, const Document& doc);

// The window title: the file, whether it has unsaved changes, and the program.
std::string windowTitle(const Document& doc);

} // namespace fast
