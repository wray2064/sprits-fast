// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

#include "ui/file_commands.h"
#include "app/document.h"
#include "app/file_io.h"

namespace fast {
namespace {

const SDL_DialogFileFilter kFilters[] = {
    { "Sprit's sprite", "lsprite" },
    { "All files",      "*" },
};

// SDL calls this when the user has chosen, which may be on another thread and
// will certainly be after the frame that opened the dialog has gone. So it does
// the least possible: record the answer, and let the main loop act on it.
void onChosen(void* userdata, const char* const* files, int /*filter*/) {
    auto* result = static_cast<DialogResult*>(userdata);
    SDL_LockMutex(result->mutex);
    if (files == nullptr || files[0] == nullptr) {
        // Null means the dialog failed; an empty list means the user cancelled.
        // Neither is worth interrupting anyone about.
        result->cancelled = true;
    } else {
        result->path = files[0];
    }
    result->ready = true;
    SDL_UnlockMutex(result->mutex);
}

// Where a dialog should start. The folder of the current file if there is one,
// so saving a second time lands next to the first.
std::string startingLocation(const Document& doc) {
    if (doc.path().empty()) {
        return std::string();
    }
    return directoryOf(doc.path());
}

} // namespace

void showOpenDialog(FileState& state, SDL_Window* window, const Document& doc) {
    SDL_LockMutex(state.dialog.mutex);
    state.dialog = { state.dialog.mutex, DialogResult::Kind::Open, false, false, {} };
    SDL_UnlockMutex(state.dialog.mutex);

    const std::string location = startingLocation(doc);
    SDL_ShowOpenFileDialog(onChosen, &state.dialog, window, kFilters,
                           static_cast<int>(SDL_arraysize(kFilters)),
                           location.empty() ? nullptr : location.c_str(), false);
}

void showSaveAsDialog(FileState& state, SDL_Window* window, const Document& doc) {
    SDL_LockMutex(state.dialog.mutex);
    state.dialog = { state.dialog.mutex, DialogResult::Kind::SaveAs, false, false, {} };
    SDL_UnlockMutex(state.dialog.mutex);

    // Suggest the current file, or the folder it lives in.
    const std::string suggestion = doc.path().empty() ? std::string() : doc.path();
    SDL_ShowSaveFileDialog(onChosen, &state.dialog, window, kFilters,
                           static_cast<int>(SDL_arraysize(kFilters)),
                           suggestion.empty() ? nullptr : suggestion.c_str());
}

std::string windowTitle(const Document& doc) {
    std::string name = doc.path().empty() ? std::string("untitled")
                                          : fileName(doc.path());
    if (doc.modified()) {
        name += "*";
    }
    return name + " - Sprit's'fast";
}

} // namespace fast
