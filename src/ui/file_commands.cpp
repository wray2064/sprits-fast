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

void showExportDialog(FileState& state, SDL_Window* window, const Document& doc) {
    SDL_LockMutex(state.dialog.mutex);
    state.dialog = { state.dialog.mutex, DialogResult::Kind::ExportPng, false, false, {} };
    SDL_UnlockMutex(state.dialog.mutex);

    static const SDL_DialogFileFilter pngFilters[] = {
        { "PNG image", "png" },
        { "All files", "*" },
    };

    // Suggest the sprite's own name with a .png beside it, which is what people
    // expect and saves them retyping.
    const std::string suggestion = doc.path().empty()
                                 ? std::string()
                                 : withExtension(fileStem(doc.path()), ".png");
    const std::string location = doc.path().empty() ? std::string()
                                                    : directoryOf(doc.path());
    const std::string start = location.empty() ? suggestion
                            : location + "/" + suggestion;

    SDL_ShowSaveFileDialog(onChosen, &state.dialog, window, pngFilters,
                           static_cast<int>(SDL_arraysize(pngFilters)),
                           start.empty() ? nullptr : start.c_str());
}

void showSheetDialog(FileState& state, SDL_Window* window, const Document& doc) {
    SDL_LockMutex(state.dialog.mutex);
    state.dialog = { state.dialog.mutex, DialogResult::Kind::ExportSheet, false, false, {} };
    SDL_UnlockMutex(state.dialog.mutex);

    static const SDL_DialogFileFilter sheetFilters[] = {
        { "PNG image", "png" },
        { "All files", "*" },
    };

    // Named "-sheet" so writing one does not quietly overwrite a single-frame
    // export made a minute earlier under the obvious name.
    const std::string suggestion = doc.path().empty()
                                 ? std::string()
                                 : withExtension(fileStem(doc.path()) + "-sheet", ".png");
    const std::string location = doc.path().empty() ? std::string()
                                                    : directoryOf(doc.path());
    const std::string start = location.empty() ? suggestion
                            : location + "/" + suggestion;

    SDL_ShowSaveFileDialog(onChosen, &state.dialog, window, sheetFilters,
                           static_cast<int>(SDL_arraysize(sheetFilters)),
                           start.empty() ? nullptr : start.c_str());
}

// The two palette formats a pixel artist meets. Offered on both dialogs so a
// palette can come from GIMP or Aseprite and go back to either, and from
// Lospec as a .hex.
static const SDL_DialogFileFilter kPaletteFilters[] = {
    { "GIMP / Aseprite palette", "gpl" },
    { "Lospec hex palette",      "hex" },
    { "All files",               "*" },
};

void showImportPaletteDialog(FileState& state, SDL_Window* window, const Document& doc) {
    SDL_LockMutex(state.dialog.mutex);
    state.dialog = { state.dialog.mutex, DialogResult::Kind::ImportPalette, false, false, {} };
    SDL_UnlockMutex(state.dialog.mutex);

    const std::string location = startingLocation(doc);
    SDL_ShowOpenFileDialog(onChosen, &state.dialog, window, kPaletteFilters,
                           static_cast<int>(SDL_arraysize(kPaletteFilters)),
                           location.empty() ? nullptr : location.c_str(), false);
}

void showExportPaletteDialog(FileState& state, SDL_Window* window, const Document& doc) {
    SDL_LockMutex(state.dialog.mutex);
    state.dialog = { state.dialog.mutex, DialogResult::Kind::ExportPalette, false, false, {} };
    SDL_UnlockMutex(state.dialog.mutex);

    const std::string suggestion = doc.path().empty()
                                 ? std::string("palette.gpl")
                                 : withExtension(fileStem(doc.path()), ".gpl");
    const std::string location = doc.path().empty() ? std::string()
                                                    : directoryOf(doc.path());
    const std::string start = location.empty() ? suggestion
                            : location + "/" + suggestion;
    SDL_ShowSaveFileDialog(onChosen, &state.dialog, window, kPaletteFilters,
                           static_cast<int>(SDL_arraysize(kPaletteFilters)),
                           start.c_str());
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
