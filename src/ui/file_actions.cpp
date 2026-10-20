// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

// file_actions.cpp -- opening, saving, closing and quitting, and the questions in front of them.
// See app_internal.h for how the window's files divide.

#include "ui/app_internal.h"

#include "app/import_aseprite.h"
#include "app/file_io.h"
#include "app/image_io.h"
#include "app/palette_io.h"
#include "app/ui_state.h"
#include "app/i18n.h"
#include "app/recent_files.h"
#include "app/recovery.h"
#include "ui/appearance.h"
#include "ui/widgets.h"
#include "ui/tabs.h"
#include "ui/panels.h"
#include "ui/reference_section.h"
#include "ui/theme.h"
#include "ui/thumbnails.h"

#include <algorithm>
#include <cstdint>
#include <ctime>
#include <string>
#include <utility>
#include <vector>

namespace fast {

// ------------------------------------------------------------ file actions --

// A document that has just replaced the old one from somewhere other than its
// own file -- an image, a sheet: everything held about the old document goes,
// the view fits the new canvas, and the strip opens if there are frames.
void adoptImported(Editor& editor, CanvasView& canvas, const ImportReport& report,
                   const std::string& from) {
    editor.activeLayer = 0;
    forgetInteraction(editor);
    canvas.referenceTextures().clear();
    canvas.frames().clear();
    resyncLayers(editor);
    keyTracks(editor);
    canvas.requestFit();
    editor.timeline.visible = editor.frames.size() > 1;
    resyncReferences(editor, canvas);
    refreshInks(editor);
    canvas.invalidate();
    editor.say("Opened " + from + ": " + std::to_string(report.frames) + " frame(s), " +
               std::to_string(report.colours) + " colour(s)" +
               (report.throughPalette ? ", every one a palette slot"
                                      : ", kept as colours of their own"));
}

// Opens a path, whatever asked for it: the dialog, a recent entry, a dropped
// file, the command line. A file that will not open is dropped from the recent
// list -- the only place that list is pruned, since a path missing because a
// drive is unplugged should not vanish from the menu on its own.
bool openPath(Editor& editor, CanvasView& canvas, const std::string& path) {
    std::string error;
    // A picture from another editor opens as a document of its own. It is
    // not added to the recent list's idea of "the file": saving asks where
    // the .lsprite goes, so the image it came from is never written over.
    if (looksLikeImageName(path)) {
        ImportReport report;
        if (!openImageAsDocument(editor.doc, path, &report, &error)) {
            editor.say("Could not open " + fileName(path) + ": " + error);
            return false;
        }
        adoptImported(editor, canvas, report, fileName(path));
        return true;
    }
    // Aseprite's own files open as the work they are: layers, frames, tags
    // as cycles, the palette as the palette.
    if (looksLikeAsepriteName(path)) {
        AsepriteReport report;
        if (!openAsepriteAsDocument(editor.doc, path, &report, &error)) {
            editor.say("Could not open " + fileName(path) + ": " + error);
            return false;
        }
        adoptImported(editor, canvas, report, fileName(path));
        std::string said = "Opened " + fileName(path) + ": " + std::to_string(report.frames) +
                           " frame(s), " + std::to_string(report.layers) + " layer(s), " +
                           std::to_string(report.tags) + " tag(s) as cycles";
        if (report.approximatedBlends) {
            said += "; some blend modes are the nearest Fast has";
        }
        if (report.skippedTilemaps) {
            said += "; a tilemap whose tileset is kept in another file was left out";
        }
        editor.say(said);
        return true;
    }
    if (!editor.doc.open(path, &error)) {
        editor.say("Could not open " + fileName(path) + ": " + error);
        editor.files.recent.remove(path);
        editor.files.recent.save();
        return false;
    }
    editor.activeLayer = 0;
    forgetInteraction(editor);
    canvas.referenceTextures().clear();
    // Every id in the new document is freshly minted, so nothing cached under
    // the old ones means anything.
    canvas.frames().clear();
    resyncLayers(editor);

    // Put the view back where it was when this file was last closed. It came
    // out of a file, so it is clamped before it reaches the canvas rather than
    // trusted: an absurd zoom or pan looks exactly like a file that failed.
    UiState view;
    if (fromJson(editor.doc.uiState(), &view)) {
        // Clamped against what this document actually holds. A file can name
        // frame 40 of a document with four, and a cycle that is no longer
        // there.
        const int frameCount = static_cast<int>(readFrames(editor.doc).size());
        view.clamp(static_cast<int>(editor.layers.size()), frameCount,
                   static_cast<int>(readCycles(editor.doc, frameCount).size()));
        canvas.setZoom(view.zoom);
        canvas.setPan(view.panX, view.panY);
        editor.activeLayer = view.activeLayer;

        editor.preview.scale = view.previewScale;
        editor.preview.transparent = view.previewTransparent != 0;
        editor.preview.color[0] = static_cast<float>((view.previewColor >> 16) & 0xFF) / 255.f;
        editor.preview.color[1] = static_cast<float>((view.previewColor >> 8) & 0xFF) / 255.f;
        editor.preview.color[2] = static_cast<float>(view.previewColor & 0xFF) / 255.f;
        editor.preview.color[3] = 1.f;

        // Which frame and cycle were open. Clamped above against what this
        // document actually holds, so a file naming frame 40 of four lands on
        // a frame that exists rather than on nothing.
        editor.timeline.activeCycle = view.activeCycle;
        selectFrame(editor, view.activeFrame);
        // The strip opens by itself for a document that has more than one
        // frame: an animation whose timeline is hidden looks like a still.
        editor.timeline.visible = editor.frames.size() > 1;
    } else {
        canvas.resetView();
    }

    ensurePalette(editor.doc, editor.sprite);
    resyncReferences(editor, canvas);
    refreshInks(editor);
    canvas.invalidate();
    editor.files.recent.add(path);
    editor.files.recent.save();
    keyTracks(editor);
    editor.say("Opened " + fileName(path) + ", " +
               std::to_string(editor.layers.size()) + " layer(s)");
    return true;
}

bool saveTo(Editor& editor, const CanvasView& canvas, const std::string& path) {
    // Where the user was looking is not part of the artwork, so it rides in the
    // package as a fast/ entry rather than going near the document.
    UiState view;
    view.zoom = canvas.zoom();
    view.panX = canvas.panX();
    view.panY = canvas.panY();
    view.activeLayer = editor.activeLayer;
    view.previewScale = editor.preview.scale;
    view.previewTransparent = editor.preview.transparent ? 1 : 0;
    view.previewColor =
        (static_cast<int>(editor.preview.color[0] * 255.f + 0.5f) << 16) |
        (static_cast<int>(editor.preview.color[1] * 255.f + 0.5f) << 8) |
         static_cast<int>(editor.preview.color[2] * 255.f + 0.5f);
    view.activeFrame = editor.timeline.activeFrame;
    view.activeCycle = editor.timeline.activeCycle;
    editor.doc.setUiState(toJson(view));
    // A picture of the first frame, so a library listing can show what this
    // file holds without opening it. Not worth failing a save over.
    updateThumbnail(editor.doc);

    std::string error;
    const std::string target = withExtension(path, kFileExtension);
    if (!editor.doc.save(target, &error)) {
        editor.say("Save failed: " + error);
        return false;
    }
    editor.files.recent.add(target);
    editor.files.recent.save();
    // The work is where the person put it, so the recovery copy would only
    // ever offer something older than what they have.
    editor.recovery.clear();
    editor.say("Saved " + fileName(target));
    return true;
}

// Save, or Save As when this document has never been written. Returns false
// when the answer is not known yet because a dialog is open.
bool saveOrAsk(Editor& editor, CanvasView& canvas, SDL_Window* window) {
    if (editor.doc.path().empty()) {
        showSaveAsDialog(editor.files, window, editor.doc);
        return false;
    }
    return saveTo(editor, canvas, editor.doc.path());
}

// What was asked for once the question about unsaved work is settled (or was
// never needed).
static bool performAction(Editor& editor, CanvasView& canvas, SDL_Window* window,
                          PendingAction action, const std::string& path) {
    switch (action) {
        case PendingAction::NewDocument: {
            const bool made = newDocument(editor, editor.files.newDocument);
            // The next new document starts unnamed again, whatever this one was.
            editor.files.newDocument.name = "untitled";
            canvas.frames().clear();          // the old document's textures
            // Fit rather than a fixed zoom: a 128 canvas at 8x does not fit the
            // window, and starting half off-screen is a poor first impression.
            canvas.requestFit();
            canvas.invalidate();
            return made;
        }
        case PendingAction::OpenDialog:
            showOpenDialog(editor.files, window, editor.doc);
            return true;
        case PendingAction::OpenPath:
            return openPath(editor, canvas, path);
        case PendingAction::ImportSheet: {
            Editor::SheetImport& sheet = editor.sheetImport;
            std::vector<ls::RasterBuffer> cells;
            std::string error;
            ImportReport report;
            if (!sliceSheet(sheet.picture, static_cast<uint32_t>(sheet.cellWidth),
                            static_cast<uint32_t>(sheet.cellHeight), &cells, &error) ||
                !documentFromFrames(editor.doc, fileStem(sheet.path), cells,
                                    std::vector<int>(cells.size(), sheet.holdMs),
                                    &report, &error)) {
                editor.say("Could not import the sheet: " + error);
                return false;
            }
            const std::string from = fileName(sheet.path);
            sheet = Editor::SheetImport{};
            adoptImported(editor, canvas, report, from);
            return true;
        }
        case PendingAction::CloseTab:
            closeActiveTab(editor, canvas);
            return true;
        case PendingAction::Quit: {
            // This document is dealt with. Any other with unsaved work is
            // asked about in its turn, shown while it is asked about.
            if (modifiedOtherTab(editor) < 0) {
                editor.quitRequested = true;
                return true;
            }
            closeActiveTab(editor, canvas);
            if (!editor.doc.modified()) {
                const int next = modifiedOtherTab(editor);
                if (next >= 0) {
                    switchToTab(editor, canvas, static_cast<size_t>(next));
                }
            }
            editor.files.pending = PendingAction::Quit;
            editor.files.pendingPath.clear();
            editor.files.askingToSave = editor.doc.modified();
            if (!editor.files.askingToSave) {
                editor.quitRequested = true;
            }
            return true;
        }
        case PendingAction::None:
            break;
    }
    return false;
}

// Anything that would discard a document goes through here. Opening and
// making documents never does any more: they open beside the work, in a tab
// of their own -- unless what is on screen is a blank nobody has touched,
// which they replace. Closing and quitting ask about unsaved work first.
// Declared in editor.h because the library panel opens documents too.
void requestAction(Editor& editor, CanvasView& canvas, SDL_Window* window,
                         PendingAction action, const std::string& path) {
    switch (action) {
        case PendingAction::OpenDialog:
            showOpenDialog(editor.files, window, editor.doc);
            return;
        case PendingAction::OpenPath: {
            // A file already open is shown, not opened twice.
            for (size_t i = 0; i < tabCount(editor); ++i) {
                const std::string& open = i == editor.activeTab ? editor.doc.path()
                                                                : editor.tabs[i]->doc.path();
                if (!open.empty() && open == path) {
                    switchToTab(editor, canvas, i);
                    editor.say(fileName(path) + " is already open");
                    return;
                }
            }
            [[fallthrough]];
        }
        case PendingAction::NewDocument:
        case PendingAction::ImportSheet:
            if (documentIsBlank(editor)) {
                performAction(editor, canvas, window, action, path);
                return;
            }
            openTab(editor, canvas);
            if (!performAction(editor, canvas, window, action, path)) {
                closeActiveTab(editor, canvas);
            }
            return;
        case PendingAction::CloseTab:
        case PendingAction::Quit:
            if (!editor.doc.modified()) {
                performAction(editor, canvas, window, action, path);
                return;
            }
            editor.files.pending = action;
            editor.files.pendingPath = path;
            editor.files.askingToSave = true;
            return;
        case PendingAction::None:
            return;
    }
}


namespace {

// "19 Oct 2026, 19:33" in the person's own time. The month is the C locale's.
std::string clockText(uint64_t seconds) {
    const std::time_t when = static_cast<std::time_t>(seconds);
    std::tm local {};
#if defined(_WIN32)
    localtime_s(&local, &when);
#else
    localtime_r(&when, &local);
#endif
    char text[64];
    std::strftime(text, sizeof(text), "%d %b %Y, %H:%M", &local);
    return text;
}

// A recovered copy is opened as an unsaved document of its own, which the person
// saves where they want it, and the copy is put away.
void recoverWork(Editor& editor, CanvasView& canvas, const RecoveredWork& work) {
    // Through openPath, so the view state and everything else in the package comes
    // back. It opens as the *copy*, which is then marked as changed: the original on
    // disk is untouched until the person saves.
    openPath(editor, canvas, work.path);
    // It is not that file: the recovery copy is about to be deleted, and Save must
    // ask where it really goes rather than writing back into the settings folder.
    editor.doc.forgetPath();
    editor.doc.markModified();
    if (!work.originalPath.empty()) {
        editor.say("Recovered. Save it back over " + fileName(work.originalPath) +
                   " when you are happy with it.");
    } else {
        editor.say("Recovered work that had never been saved.");
    }
    discardRecoveredWork(work);
}

} // namespace

// What was found waiting from a session that did not end normally: the work that was
// lost last, and nothing else. A list of copies, each called "untitled", asks the
// person to work out which is which; one piece of work, with its picture, its size,
// when it was last written and where it was going, asks only whether to keep it.
// Copies of earlier sessions, long forgotten, are put aside (not deleted) once this one
// is dealt with, and the prompt says so.
//
// "Later" leaves everything where it is and asks again next time: closing the question
// is not saying the work is worthless. "Discard" asks twice, since it is the one thing
// here that cannot be undone.
void drawRecoveryPrompt(Editor& editor, CanvasView& canvas) {
    if (!editor.askingToRecover || editor.recovered.empty()) {
        return;
    }
    const RecoveredWork work = editor.recovered.front();       // the newest
    const size_t older = editor.recovered.size() - 1;
    if (editor.recoverySummaryOf != work.path) {
        editor.recoverySummary = summariseRecoveredWork(work);
        editor.recoverySummaryOf = work.path;
        editor.recoveryConfirmDiscard = false;
    }
    const RecoverySummary& summary = editor.recoverySummary;
    const theme::Palette& c = theme::palette();
    const float k = theme::scale();

    if (!ImGui::IsPopupOpen("###unfinished")) {
        ImGui::OpenPopup("###unfinished");
    }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(),
                            ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!widgets::beginModal("Recover your work?###unfinished", nullptr,
                             ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }
    const float width = 440.f * k;
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width);

    ImGui::TextColored(c.textDim, "%s", tr("Sprit's'fast closed before this was saved."));
    ImGui::Dummy(ImVec2(0.f, 8.f * k));

    int budget = 1;
    const float picture = 96.f * k;
    drawDocumentThumbnail(canvas, work.path, picture, true, &budget);
    ImGui::SameLine(0.f, 14.f * k);
    ImGui::BeginGroup();
    const bool named = !work.originalPath.empty();
    theme::pushText(theme::TextStyle::Title);
    ImGui::TextColored(c.textBright, "%s", named ? work.name.c_str() : tr("Untitled"));
    theme::popText();
    const uint64_t now = static_cast<uint64_t>(std::time(nullptr));
    const std::string age = ageText(work.savedSeconds, now);
    ImGui::TextColored(c.text, "%s", (std::string(tr("Last written")) + " " +
                                       (age.empty() ? clockText(work.savedSeconds)
                                                    : age)).c_str());
    ImGui::TextColored(c.textDim, "%s", clockText(work.savedSeconds).c_str());
    if (summary.readable) {
        std::string holds = std::to_string(summary.width) + " x " + std::to_string(summary.height) +
                            " px, " + std::to_string(summary.frames) + " " +
                            tr(summary.frames == 1 ? "frame" : "frames");
        ImGui::TextColored(c.textDim, "%s", holds.c_str());
    } else {
        ImGui::TextColored(c.warning, "%s", tr("This copy could not be read."));
    }
    ImGui::EndGroup();

    ImGui::Dummy(ImVec2(0.f, 6.f * k));
    if (named) {
        ImGui::TextColored(c.textDim, "%s", (std::string(tr("It was being saved as")) + " " +
                                              work.originalPath).c_str());
    } else {
        ImGui::TextColored(c.textDim, "%s", tr("It had not been saved yet, so it has no file name."));
    }
    if (older > 0) {
        ImGui::Dummy(ImVec2(0.f, 4.f * k));
        const std::string folder = archivedRecoveryDirectory(editor.recovered);
        ImGui::TextColored(c.textDim, "%s",
                           (std::to_string(older) + " " +
                            tr(older == 1 ? "older copy from an earlier session will be kept in"
                                          : "older copies from earlier sessions will be kept in") +
                            " " + folder + ".").c_str());
    }
    ImGui::PopTextWrapPos();
    ImGui::Dummy(ImVec2(0.f, 10.f * k));

    const auto finish = [&]() {
        archiveOlderRecoveredWork(editor.recovered);
        editor.recovered.clear();
        editor.askingToRecover = false;
        editor.recoveryConfirmDiscard = false;
        ImGui::CloseCurrentPopup();
    };
    if (widgets::button(tr("Recover"), widgets::ButtonStyle::Primary)) {
        recoverWork(editor, canvas, work);
        finish();
    } else {
        ImGui::SameLine();
        if (editor.recoveryConfirmDiscard) {
            if (widgets::button(tr("Discard for good"), widgets::ButtonStyle::Danger)) {
                discardRecoveredWork(work);
                finish();
            }
        } else if (widgets::button(tr("Discard"))) {
            editor.recoveryConfirmDiscard = true;
        }
        if (editor.askingToRecover) {
            ImGui::SameLine();
            if (widgets::button(tr("Later"))) {
                // Kept, not deleted: it will be offered again next time.
                editor.askingToRecover = false;
                editor.recoveryConfirmDiscard = false;
                ImGui::CloseCurrentPopup();
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", tr("Leave it where it is. You will be asked again "
                                           "next time."));
            }
        }
    }
    ImGui::EndPopup();
}

// The question in front of anything that would discard unsaved work. Cancel has
// to abandon the pending action rather than quietly proceeding, which is the
// bug this kind of prompt usually has.
void drawUnsavedPrompt(Editor& editor, CanvasView& canvas, SDL_Window* window) {
    if (editor.files.askingToSave && !ImGui::IsPopupOpen("Unsaved changes")) {
        ImGui::OpenPopup("Unsaved changes");
    }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(),
                            ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!widgets::beginModal("Unsaved changes", nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    const std::string name = editor.doc.path().empty()
                           ? std::string("This document")
                           : fileName(editor.doc.path());
    ImGui::Text("%s has unsaved changes.", name.c_str());
    ImGui::Dummy(ImVec2(0.f, 6.f));

    if (ImGui::Button(tr("Save"), ImVec2(112.f, 0.f))) {
        const PendingAction action = editor.files.pending;
        const std::string path = editor.files.pendingPath;
        if (editor.doc.path().empty()) {
            // Save As is asynchronous, so the pending action has to survive
            // until the dialog comes back.
            editor.files.resumeAfterSave = true;
            showSaveAsDialog(editor.files, window, editor.doc);
        } else if (saveTo(editor, canvas, editor.doc.path())) {
            editor.files.pending = PendingAction::None;
            performAction(editor, canvas, window, action, path);
        }
        editor.files.askingToSave = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button(tr("Discard"), ImVec2(112.f, 0.f))) {
        const PendingAction action = editor.files.pending;
        const std::string path = editor.files.pendingPath;
        editor.files.pending = PendingAction::None;
        editor.files.askingToSave = false;
        performAction(editor, canvas, window, action, path);
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button(tr("Cancel"), ImVec2(112.f, 0.f)) ||
        ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        editor.files.pending = PendingAction::None;
        editor.files.pendingPath.clear();
        editor.files.askingToSave = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

// Repeats the last export, to the same file, as if the dialog had just been
// answered with it: the answer goes in the same place a native dialog's does,
// so the one handler below does the work and says what happened.
void exportAgain(Editor& editor) {
    if (editor.lastExport.kind == DialogResult::Kind::None) {
        return;
    }
    SDL_LockMutex(editor.files.dialog.mutex);
    editor.files.dialog.kind = editor.lastExport.kind;
    editor.files.dialog.path = editor.lastExport.path;
    editor.files.dialog.cancelled = false;
    editor.files.dialog.ready = true;
    SDL_UnlockMutex(editor.files.dialog.mutex);
}

// Picks up whatever a native dialog came back with. The callback runs on SDL's
// terms -- possibly another thread, certainly another frame -- so the answer is
// read here under the lock and acted on in the main loop's own time.
void processDialogResult(Editor& editor, CanvasView& canvas, SDL_Window* window) {
    DialogResult::Kind kind = DialogResult::Kind::None;
    bool cancelled = false;
    std::string path;

    SDL_LockMutex(editor.files.dialog.mutex);
    if (editor.files.dialog.ready) {
        kind = editor.files.dialog.kind;
        cancelled = editor.files.dialog.cancelled;
        path = editor.files.dialog.path;
        editor.files.dialog.ready = false;
        editor.files.dialog.kind = DialogResult::Kind::None;
    }
    SDL_UnlockMutex(editor.files.dialog.mutex);

    if (kind == DialogResult::Kind::None) {
        return;
    }
    if (cancelled || path.empty()) {
        // Cancelling a save that something was waiting on cancels that too.
        editor.files.resumeAfterSave = false;
        editor.files.pending = PendingAction::None;
        editor.say("Cancelled");
        return;
    }

    if (kind == DialogResult::Kind::Open) {
        requestAction(editor, canvas, window, PendingAction::OpenPath, path);
        return;
    }
    if (kind == DialogResult::Kind::ImportSheet) {
        std::vector<uint8_t> bytes;
        std::string error;
        ls::RasterBuffer picture;
        if (!readFile(path, bytes, &error) || !decodeImage(bytes, &picture, &error)) {
            editor.say("Could not read " + fileName(path) + ": " + error);
            return;
        }
        Editor::SheetImport& sheet = editor.sheetImport;
        sheet.open = true;
        sheet.path = path;
        // A guess at the grid: square cells as tall as the sheet for a strip,
        // as wide as it for a column, and otherwise the common 32.
        const int w = static_cast<int>(picture.width);
        const int h = static_cast<int>(picture.height);
        if (w >= h && w % h == 0) {
            sheet.cellWidth = sheet.cellHeight = h;
        } else if (h > w && h % w == 0) {
            sheet.cellWidth = sheet.cellHeight = w;
        } else {
            sheet.cellWidth = std::min(32, w);
            sheet.cellHeight = std::min(32, h);
        }
        sheet.picture = std::move(picture);
        return;
    }

    if (kind == DialogResult::Kind::ExportPng) {
        ExportSettings settings;
        settings.scale = static_cast<uint32_t>(editor.exportScale);
        std::string error;
        IndexedReport report;
        const bool done = editor.exportIndexed
            ? exportSpriteToIndexedPng(editor.doc, editor.sprite, path, settings, &report, &error)
            : exportSpriteToPng(editor.doc, editor.sprite, path, settings, &error);
        if (done) {
            editor.lastExport = { kind, path };
            std::string said = "Exported " + fileName(path) + " at " +
                               std::to_string(editor.exportScale) + "x";
            if (editor.exportIndexed) {
                said += ", indexed: " + std::to_string(report.paletteEntries) + " slot(s)";
                if (report.extraColours > 0) {
                    said += " and " + std::to_string(report.extraColours) +
                            " colour(s) not in the palette, after them";
                }
            }
            editor.say(said);
        } else {
            editor.say("Export failed: " + error);
        }
        return;
    }

    if (kind == DialogResult::Kind::ImportReference) {
        std::string error;
        if (!importReferenceToPick(editor, canvas, path, &error)) {
            editor.say("Could not import " + fileName(path) + ": " + error);
        }
        return;
    }

    if (kind == DialogResult::Kind::ProjectFolder ||
        kind == DialogResult::Kind::ReferenceFolder) {
        if (kind == DialogResult::Kind::ProjectFolder) {
            editor.libraryFolders.project = path;
            editor.libraryShowingReferences = false;
        } else {
            editor.libraryFolders.references = path;
            editor.libraryShowingReferences = true;
        }
        editor.libraryFolders.save();
        editor.libraryStale = true;
        canvas.libraryThumbnails().clear();
        editor.say("Library folder: " + path);
        return;
    }

    if (kind == DialogResult::Kind::ImportPalette) {
        int dropped = 0;
        std::string error;
        if (importPaletteFile(editor.doc, editor.sprite, path, &dropped, &error)) {
            resyncLayers(editor);
            resyncReferences(editor, canvas);
            refreshInks(editor);
            canvas.invalidate();
            std::string said = "Loaded palette " + fileName(path);
            if (dropped > 0) {
                // Say it rather than let someone discover a layer changed.
                said += " -- " + std::to_string(dropped) +
                        (dropped == 1 ? " slot in use was not in the file; its layers "
                                        "keep their colour"
                                      : " slots in use were not in the file; their "
                                        "layers keep their colour");
            }
            editor.say(said);
        } else {
            editor.say("Could not load palette: " + error);
        }
        return;
    }

    if (kind == DialogResult::Kind::ImportTheme) {
        importThemeFrom(editor, path);
        applyViewPreferences(editor.prefs, canvas);
        saveSettings(editor);
        return;
    }
    if (kind == DialogResult::Kind::ExportTheme) {
        exportThemeTo(editor, path);
        return;
    }

    if (kind == DialogResult::Kind::ExportPalette) {
        std::string error;
        // Default to .gpl, which carries names; .hex if that is what was typed.
        const std::string target = hasExtension(path, ".hex") ? path
                                                              : withExtension(path, ".gpl");
        if (exportPaletteFile(editor.doc, editor.activeSprite(), target, &error)) {
            editor.say("Wrote palette " + fileName(target));
        } else {
            editor.say("Could not write palette: " + error);
        }
        return;
    }

    if (kind == DialogResult::Kind::SaveCopy) {
        const std::string target = withExtension(path, kFileExtension);
        std::string error;
        if (editor.doc.saveCopy(target, &error)) {
            editor.say("Wrote a copy to " + fileName(target) + "; still working in " +
                       (editor.doc.path().empty() ? std::string("the untitled document")
                                                  : fileName(editor.doc.path())));
        } else {
            editor.say("The copy failed: " + error);
        }
        return;
    }
    if (kind == DialogResult::Kind::ExportAnimation) {
        const Cycle cycle = editor.animationFromCycle
            ? activeCycle(editor) : everyFrame(static_cast<int>(editor.frames.size()));
        const char* extension = animationExtension(editor.animation.format);
        const std::string target = withExtension(path, extension);
        AnimationReport report;
        std::string error;
        if (exportAnimation(editor.doc, editor.frames, cycle, target, editor.animation,
                            &report, &error)) {
            editor.lastExport = { kind, target };
            std::string said = "Wrote " + fileName(target) + ": " +
                               std::to_string(report.frames) + " frame(s)";
            if (report.reducedColours) {
                said += "; a frame had more than 255 colours and was reduced";
            }
            if (report.droppedAlpha) {
                said += "; partial transparency became all or nothing, as GIF requires";
            }
            editor.say(said);
        } else {
            editor.say("Animation failed: " + error);
        }
        return;
    }
    if (kind == DialogResult::Kind::ExportSheet) {
        const std::vector<int> steps = sheetSteps(editor);
        std::string error;
        if (exportSheetToPng(editor.doc, editor.frames, steps, editor.cycles,
                             withExtension(path, ".png"), editor.sheet, &error)) {
            editor.lastExport = { kind, withExtension(path, ".png") };
            editor.say("Wrote " + fileName(path) + ": " +
                       std::to_string(steps.size()) + " cells at " +
                       std::to_string(editor.sheet.scale) + "x");
        } else {
            editor.say("Sheet failed: " + error);
        }
        return;
    }

    if (saveTo(editor, canvas, path) && editor.files.resumeAfterSave) {
        const PendingAction action = editor.files.pending;
        const std::string pendingPath = editor.files.pendingPath;
        editor.files.resumeAfterSave = false;
        editor.files.pending = PendingAction::None;
        performAction(editor, canvas, window, action, pendingPath);
    }
}

} // namespace fast
