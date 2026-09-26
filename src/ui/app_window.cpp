// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

// app_window.cpp — the window: layout, input, and the file commands.
//
// The panels themselves live in panels.cpp and the look in theme.cpp, so what
// is left here is the shape of the window and what happens when keys and mouse
// buttons are pressed.
//
// This is still the only part of Fast that knows what toolkit is in use.
// Everything it does goes through fast_core, which knows nothing about windows.

#include "app/batch.h"
#include "app/import_aseprite.h"
#include "app/palette_tools.h"
#include "app/pixel_font.h"
#include "app/guides.h"
#include "app/slices.h"
#include "app/tracks.h"
#include "app/import_image.h"
#include "app/animation.h"
#include "app/export_png.h"
#include "app/file_io.h"
#include "app/grid_snap.h"
#include "app/image_io.h"
#include "app/palette_io.h"
#include "app/shape.h"
#include "app/sheet.h"
#include "app/transform.h"
#include "app/ui_state.h"
#include "ui/keys.h"
#include "app/i18n.h"
#include "ui/os_clipboard.h"
#include "ui/shape_tools.h"
#include "ui/rulers.h"
#include "ui/slice_tool.h"
#include "ui/tabs.h"
#include "ui/ui_script.h"
#include "ui/selection_tools.h"
#include "ui/editor.h"
#include "ui/panels.h"
#include "ui/theme.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>

#include <SDL3/SDL.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

using namespace fast;

// ------------------------------------------------------------ file actions --

// Opens a path, whatever asked for it: the dialog, a recent entry, a dropped
// file, the command line. A file that will not open is dropped from the recent
// list -- the only place that list is pruned, since a path missing because a
// drive is unplugged should not vanish from the menu on its own.
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
            said += "; tilemap layers were left out";
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

bool performAction(Editor& editor, CanvasView& canvas, SDL_Window* window,
                   PendingAction action, const std::string& path) {
    switch (action) {
        case PendingAction::NewDocument: {
            const bool made = newDocument(editor, editor.files.newDocument);
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

} // namespace

// Anything that would discard a document goes through here. Opening and
// making documents never does any more: they open beside the work, in a tab
// of their own -- unless what is on screen is a blank nobody has touched,
// which they replace. Closing and quitting ask about unsaved work first.
// Outside this file's anonymous namespace because the library panel opens
// documents too.
void fast::requestAction(Editor& editor, CanvasView& canvas, SDL_Window* window,
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

// ------------------------------------------------------------------- menu --

void drawMenuBar(Editor& editor, CanvasView& canvas, SDL_Window* window) {
    if (!ImGui::BeginMainMenuBar()) {
        return;
    }
    if (ImGui::BeginMenu(tr("File"))) {
        if (ImGui::MenuItem(tr("New..."), keysLabel(editor.keys, "file.new").c_str())) {
            editor.newDocumentOpen = true;
        }
        if (ImGui::BeginMenu(tr("Autosave"))) {
            if (ImGui::MenuItem(tr("On"), nullptr, editor.autosaveOn,
                                editor.recovery.active())) {
                editor.autosaveOn = !editor.autosaveOn;
                if (!editor.autosaveOn) {
                    editor.recovery.clear();
                }
            }
            if (!editor.recovery.active()) {
                ImGui::TextDisabled("No settings folder to write to.");
            }
            ImGui::Separator();
            for (uint32_t minutes : { 1u, 2u, 5u, 10u }) {
                const std::string label = std::to_string(minutes) +
                                          (minutes == 1 ? " minute" : " minutes");
                if (ImGui::MenuItem(label.c_str(), nullptr,
                                    editor.recovery.intervalSeconds() == minutes * 60u)) {
                    editor.recovery.setIntervalSeconds(minutes * 60u);
                }
            }
            ImGui::Separator();
            if (ImGui::MenuItem(tr("Write a copy now"), nullptr, false,
                                editor.autosaveOn && editor.doc.modified())) {
                if (editor.recovery.writeNow(editor.doc, SDL_GetTicks() / 1000ull)) {
                    editor.say("Recovery copy written");
                }
            }
            ImGui::EndMenu();
        }
        ImGui::Separator();
        if (ImGui::MenuItem(tr("Library..."), keysLabel(editor.keys, "file.library").c_str())) {
            editor.libraryOpen = true;
            editor.libraryStale = true;
        }
        if (ImGui::MenuItem(tr("Import reference..."))) {
            showImportReferenceDialog(editor.files, window, editor.doc);
        }
        if (ImGui::MenuItem(tr("Import sprite sheet..."))) {
            showImportSheetDialog(editor.files, window, editor.doc);
        }
        ImGui::Separator();
        if (ImGui::MenuItem(tr("Open..."), keysLabel(editor.keys, "file.open").c_str())) {
            requestAction(editor, canvas, window, PendingAction::OpenDialog);
        }
        if (ImGui::BeginMenu(tr("Open recent"), !editor.files.recent.empty())) {
            for (const std::string& entry : editor.files.recent.entries()) {
                if (ImGui::MenuItem(fileName(entry).c_str())) {
                    requestAction(editor, canvas, window, PendingAction::OpenPath, entry);
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s", entry.c_str());
                }
            }
            ImGui::Separator();
            if (ImGui::MenuItem(tr("Clear"))) {
                editor.files.recent.clear();
                editor.files.recent.save();
            }
            ImGui::EndMenu();
        }
        ImGui::Separator();
        if (ImGui::MenuItem(tr("Save"), keysLabel(editor.keys, "file.save").c_str())) {
            saveOrAsk(editor, canvas, window);
        }
        if (ImGui::MenuItem(tr("Save as..."), keysLabel(editor.keys, "file.save-as").c_str())) {
            showSaveAsDialog(editor.files, window, editor.doc);
        }
        if (ImGui::MenuItem(tr("Save a copy..."))) {
            showSaveCopyDialog(editor.files, window, editor.doc);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Write the document somewhere else, and carry on in the "
                              "file you are in -- its name and its unsaved state stay "
                              "as they are.");
        }
        if (ImGui::BeginMenu(tr("Export PNG"))) {
            ImGui::MenuItem(tr("Indexed, the palette as its table"), nullptr, &editor.exportIndexed);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("A colour table and one byte a pixel, with the palette's "
                                  "slots in order as the\nfirst entries -- what a game that "
                                  "swaps palettes at run time wants.");
            }
            ImGui::Separator();
            const int scales[] = { 1, 2, 4, 8, 16 };
            for (int scale : scales) {
                const std::string label = std::to_string(scale) + "x";
                if (ImGui::MenuItem(label.c_str())) {
                    editor.exportScale = scale;
                    showExportDialog(editor.files, window, editor.doc);
                }
            }
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem(tr("Export sheet..."), nullptr, false,
                            !editor.frames.empty())) {
            editor.sheetPanelOpen = true;
        }
        if (ImGui::MenuItem(tr("Export animation..."), nullptr, false,
                            !editor.frames.empty())) {
            editor.animationPanelOpen = true;
        }
        ImGui::Separator();
        if (ImGui::MenuItem(tr("Close tab"), keysLabel(editor.keys, "file.close").c_str())) {
            requestAction(editor, canvas, window, PendingAction::CloseTab);
        }
        if (ImGui::MenuItem(tr("Quit"), keysLabel(editor.keys, "file.quit").c_str())) {
            requestAction(editor, canvas, window, PendingAction::Quit);
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu(tr("Edit"))) {
        const std::string undo = editor.doc.canUndo()
            ? "Undo " + editor.doc.undoLabel() : std::string("Undo");
        const std::string redo = editor.doc.canRedo()
            ? "Redo " + editor.doc.redoLabel() : std::string("Redo");
        if (ImGui::MenuItem(undo.c_str(), keysLabel(editor.keys, "edit.undo").c_str(), false,
                            editor.doc.canUndo())) {
            editor.doc.undo();
            resyncLayers(editor);
            resyncReferences(editor, canvas);
            refreshInks(editor);
            canvas.invalidate();
        }
        if (ImGui::MenuItem(redo.c_str(), keysLabel(editor.keys, "edit.redo").c_str(), false,
                            editor.doc.canRedo())) {
            editor.doc.redo();
            resyncLayers(editor);
            resyncReferences(editor, canvas);
            refreshInks(editor);
            canvas.invalidate();
        }
        if (ImGui::MenuItem(tr("History..."), nullptr, editor.historyOpen)) {
            editor.historyOpen = !editor.historyOpen;
        }
        if (ImGui::MenuItem(tr("Preferences..."))) {
            editor.preferencesOpen = true;
        }
        ImGui::Separator();
        const bool selected = !editor.selection.empty();
        if (ImGui::MenuItem(tr("Cut"), keysLabel(editor.keys, "edit.cut").c_str(), false,
                            selected)) {
            cutSelectionPixels(editor);
        }
        if (ImGui::MenuItem(tr("Copy"), keysLabel(editor.keys, "edit.copy").c_str())) {
            copyCommand(editor);
        }
        if (ImGui::MenuItem(tr("Paste"), keysLabel(editor.keys, "edit.paste").c_str())) {
            pasteCommand(editor, canvas, false);
        }
        if (ImGui::MenuItem(tr("Paste as new layer"), keysLabel(editor.keys, "edit.paste-layer").c_str(),
                            false, pixelsToPaste(editor))) {
            pasteCommand(editor, canvas, true);
        }
        if (ImGui::MenuItem(tr("Paste as reference"), nullptr, false, clipboardHasImage())) {
            pasteReference(editor, canvas);
        }
        if (ImGui::MenuItem(tr("Delete"), "Del", false, selected)) { deleteSelectionPixels(editor); }
        if (ImGui::MenuItem(tr("Fill selection"), keysLabel(editor.keys, "edit.fill").c_str(), false,
                            selected)) {
            fillSelection(editor, false);
        }
        if (ImGui::MenuItem(tr("Stroke selection"), keysLabel(editor.keys, "edit.stroke").c_str(), false,
                            selected)) {
            fillSelection(editor, true);
        }
        if (ImGui::MenuItem(tr("Brush from selection"), keysLabel(editor.keys, "edit.brush").c_str(),
                            false, selected)) {
            brushFromSelection(editor);
        }
        ImGui::Separator();
        const bool movable = selected || editor.floating.active();
        if (ImGui::MenuItem(tr("Flip horizontally"), "Shift+H", false, movable)) {
            turnSelection(editor, FloatTurn::FlipHorizontal);
        }
        if (ImGui::MenuItem(tr("Flip vertically"), "Shift+V", false, movable)) {
            turnSelection(editor, FloatTurn::FlipVertical);
        }
        if (ImGui::MenuItem(tr("Rotate 90 clockwise"), nullptr, false, movable)) {
            turnSelection(editor, FloatTurn::Clockwise);
        }
        if (ImGui::MenuItem(tr("Rotate 90 anticlockwise"), nullptr, false, movable)) {
            turnSelection(editor, FloatTurn::Anticlockwise);
        }
        if (ImGui::MenuItem(tr("Rotate 180"), nullptr, false, movable)) {
            turnSelection(editor, FloatTurn::HalfTurn);
        }
        if (ImGui::MenuItem(tr("Rotate freely"), nullptr, false, movable)) {
            rotateSelectionFreely(editor);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Any angle, pixel-art style (RotSprite): the selection goes "
                              "onto a layer of its own with a rotation the Transform panel "
                              "turns, and can turn again whenever you like.");
        }
        if (ImGui::MenuItem(tr("Scale freely"), nullptr, false, movable)) {
            scaleSelectionFreely(editor);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("The selection onto a layer of its own with handles to size "
                              "it by, and move it by. The drawing is never resampled: any "
                              "size can go back to the original exactly.");
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu(tr("Sprite"))) {
        if (ImGui::MenuItem(tr("Canvas size..."))) {
            auto size = editor.doc.engine().getCanvasSize(editor.doc.id());
            if (size.ok()) {
                editor.canvasDialog.width = size.value.x;
                editor.canvasDialog.height = size.value.y;
            }
            editor.canvasDialog.open = true;
        }
        if (ImGui::MenuItem(tr("Crop to selection"), nullptr, false, !editor.selection.empty())) {
            const ls::Rect2i box = editor.selection.bounds();
            changeCanvas(editor, canvas,
                         [&](std::string* error) { return cropCanvas(editor.doc, box, error); },
                         "Cropped to the selection");
        }
        if (ImGui::MenuItem(tr("Trim"))) {
            changeCanvas(editor, canvas,
                         [&](std::string* error) { return trimCanvas(editor.doc, error); },
                         "Trimmed to what is drawn");
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Crop to the smallest rectangle holding everything any "
                              "frame draws.");
        }
        ImGui::Separator();
        {
            bool together = tracksOn(editor.doc);
            if (ImGui::MenuItem(tr("Same layers in every frame"), nullptr, &together)) {
                editor.doc.beginAction(together ? "Layers in every frame"
                                                : "Layers frame by frame");
                if (together) {
                    adoptTracks(editor.doc, editor.activeSprite());
                } else {
                    setTracksOn(editor.doc, false);
                }
                editor.doc.endAction();
                resyncLayers(editor);
                canvas.invalidate();
                editor.say(together ? "A layer added, moved, renamed or hidden in one frame now is "
                                      "in all of them; each keeps its own pixels"
                                    : "Each frame's layers are its own again");
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("On: layers run through the whole animation, as in "
                                  "Aseprite -- each frame holds its own pixels of each. "
                                  "Turning it on for an older file matches layers by name "
                                  "and loses nothing.");
            }
        }
        if (ImGui::MenuItem(tr("Adjust colours..."), nullptr, false, editor.active() != nullptr)) {
            editor.adjustDialog = Editor::AdjustDialog{};
            editor.adjustDialog.open = true;
            editor.doc.beginAction("Adjust colours");
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Hue, saturation, lightness, brightness, contrast, invert -- "
                              "of the elements' own colours and, if asked, the palette "
                              "slots they paint through. Nothing is turned into pixels.");
        }
        if (ImGui::MenuItem(tr("Sprite size..."))) {
            auto size = editor.doc.engine().getCanvasSize(editor.doc.id());
            if (size.ok()) {
                editor.spriteDialog.width = size.value.x;
                editor.spriteDialog.height = size.value.y;
                editor.spriteDialog.percentX = 100.f;
                editor.spriteDialog.percentY = 100.f;
            }
            editor.spriteDialog.open = true;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Scale everything to any size, nearest neighbour: every "
                              "frame, every layer, shapes as shapes.");
        }
        if (ImGui::BeginMenu(tr("Enlarge"))) {
            for (uint32_t factor : { 2u, 3u, 4u, 8u }) {
                const std::string label = std::to_string(factor) + "x";
                if (ImGui::MenuItem(label.c_str())) {
                    changeCanvas(editor, canvas, [&](std::string* error) {
                        return enlargeSprite(editor.doc, factor, error);
                    }, "Enlarged " + label + ", every pixel a block");
                }
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu(tr("Reduce"))) {
            ImGui::TextDisabled("Keeps one pixel of each block: detail is lost.");
            for (uint32_t factor : { 2u, 3u, 4u }) {
                const std::string label = "1/" + std::to_string(factor);
                if (ImGui::MenuItem(label.c_str())) {
                    changeCanvas(editor, canvas, [&](std::string* error) {
                        return reduceSprite(editor.doc, factor, error);
                    }, "Reduced to " + label);
                }
            }
            ImGui::EndMenu();
        }
        ImGui::Separator();
        if (ImGui::MenuItem(tr("Rotate canvas 90 clockwise"))) {
            changeCanvas(editor, canvas,
                         [&](std::string* error) { return rotateCanvas(editor.doc, 1, error); },
                         "Turned clockwise");
        }
        if (ImGui::MenuItem(tr("Rotate canvas 90 anticlockwise"))) {
            changeCanvas(editor, canvas,
                         [&](std::string* error) { return rotateCanvas(editor.doc, 3, error); },
                         "Turned anticlockwise");
        }
        if (ImGui::MenuItem(tr("Rotate canvas 180"))) {
            changeCanvas(editor, canvas,
                         [&](std::string* error) { return rotateCanvas(editor.doc, 2, error); },
                         "Turned around");
        }
        if (ImGui::MenuItem(tr("Flip canvas horizontally"))) {
            changeCanvas(editor, canvas,
                         [&](std::string* error) { return flipCanvas(editor.doc, true, error); },
                         "Flipped left to right");
        }
        if (ImGui::MenuItem(tr("Flip canvas vertically"))) {
            changeCanvas(editor, canvas,
                         [&](std::string* error) { return flipCanvas(editor.doc, false, error); },
                         "Flipped top to bottom");
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu(tr("Select"))) {
        if (ImGui::MenuItem(tr("All"), "Ctrl+A")) { selectAll(editor); }
        if (ImGui::MenuItem(tr("Deselect"), "Ctrl+D", false, !editor.selection.empty())) {
            deselect(editor);
        }
        if (ImGui::MenuItem(tr("Reselect"), nullptr, false, !editor.selection.previous.empty())) {
            reselect(editor);
        }
        if (ImGui::MenuItem(tr("Invert"), "Ctrl+Shift+I")) { invertSelection(editor); }
        if (ImGui::BeginMenu(tr("Modify"), !editor.selection.empty())) {
            const struct { SelectionModify how; const char* name; } kinds[] = {
                { SelectionModify::Expand, "Expand..." },
                { SelectionModify::Contract, "Contract..." },
                { SelectionModify::Border, "Border..." },
            };
            for (const auto& k : kinds) {
                if (ImGui::MenuItem(tr(k.name))) {
                    editor.modifyDialog.how = k.how;
                    editor.modifyDialog.open = true;
                }
            }
            ImGui::EndMenu();
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu(tr("View"))) {
        if (ImGui::MenuItem(tr("Zoom in"), "Ctrl+=")) { canvas.setZoom(canvas.zoom() + 1.f); }
        if (ImGui::MenuItem(tr("Zoom out"), "Ctrl+-")) { canvas.setZoom(canvas.zoom() - 1.f); }
        if (ImGui::MenuItem(tr("Fit to window"), "Ctrl+0")) { canvas.requestFit(); }
        if (ImGui::MenuItem(tr("Reset view"))) { canvas.resetView(); }
        ImGui::Separator();
        bool grid = canvas.gridVisible();
        if (ImGui::MenuItem(tr("Pixel grid"), nullptr, &grid)) { canvas.setGridVisible(grid); }
        ImGui::MenuItem(tr("Rulers"), nullptr, &editor.rulersOn);
        ImGui::MenuItem(tr("Guides"), nullptr, &editor.guidesShown);
        if (ImGui::MenuItem(tr("Clear guides"), nullptr, false, !readGuides(editor.doc).empty())) {
            editor.doc.beginAction("Clear guides");
            writeGuides(editor.doc, {});
            editor.doc.endAction();
        }
        if (ImGui::MenuItem(tr("Slices..."), nullptr, editor.slicesOpen)) {
            editor.slicesOpen = !editor.slicesOpen;
        }
        if (ImGui::MenuItem(tr("Snap to grid"), keysLabel(editor.keys, "view.snap").c_str(),
                            &editor.snapToGrid)) {
            if (editor.snapToGrid) {
                canvas.tileGrid().visible = true;
            }
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Marquees cover whole tiles, shapes put their corners on "
                              "tile lines, and a moved selection steps from tile to tile. "
                              "The tile grid's size and offset are below.");
        }
        if (ImGui::BeginMenu(tr("Tile grid"))) {
            TileGrid& tiles = canvas.tileGrid();
            ImGui::MenuItem(tr("Show"), nullptr, &tiles.visible);
            if (ImGui::MenuItem(tr("Isometric"), nullptr, &tiles.isometric) && tiles.isometric) {
                tiles.visible = true;
                // The usual 2:1 diamond, unless a shape was already chosen.
                if (tiles.width == tiles.height) {
                    tiles.height = std::max(1, tiles.width / 2);
                }
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Diamonds for isometric tiles: width across, height "
                                  "down, 2:1 by default. Snapping keeps to the square grid.");
            }
            ImGui::SetNextItemWidth(120.f);
            ImGui::InputInt("width", &tiles.width);
            ImGui::SetNextItemWidth(120.f);
            ImGui::InputInt("height", &tiles.height);
            ImGui::SetNextItemWidth(120.f);
            ImGui::InputInt("offset x", &tiles.offsetX);
            ImGui::SetNextItemWidth(120.f);
            ImGui::InputInt("offset y", &tiles.offsetY);
            tiles.width = std::clamp(tiles.width, 1, 4096);
            tiles.height = std::clamp(tiles.height, 1, 4096);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu(tr("Tiled mode"))) {
            const char* names[] = { "Off", "Across", "Down", "Both ways" };
            for (int i = 0; i < 4; ++i) {
                if (ImGui::MenuItem(names[i], nullptr,
                                    static_cast<int>(canvas.tiledMode()) == i)) {
                    canvas.setTiledMode(static_cast<TiledMode>(i));
                }
            }
            ImGui::EndMenu();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("The canvas drawn again beside itself, so a tile's "
                              "seams show while it is drawn -- and a stroke off one "
                              "edge comes back on the other.");
        }
        ImGui::Separator();
        ImGui::MenuItem(tr("Symmetry across"), nullptr, &editor.symmetryAcross);
        ImGui::MenuItem(tr("Symmetry down"), nullptr, &editor.symmetryDown);
        ImGui::MenuItem(tr("Preview"), "P", &editor.preview.visible);
        ImGui::EndMenu();
    }
    ImGui::EndMainMenuBar();
}

// The grid a sprite sheet is cut on. Numbers rather than a drawn grid on the
// picture, because the numbers are what the person already knows -- "16 by 16"
// -- and the frame count beside them says at once whether they are right.
void drawSheetImportPanel(Editor& editor, CanvasView& canvas, SDL_Window* window) {
    Editor::SheetImport& sheet = editor.sheetImport;
    if (!sheet.open) {
        return;
    }
    ImGui::SetNextWindowSize(ImVec2(320.f, 0.f), ImGuiCond_Appearing);
    bool open = true;
    if (ImGui::Begin("Import sprite sheet", &open, ImGuiWindowFlags_NoCollapse)) {
        ImGui::TextUnformatted(fileName(sheet.path).c_str());
        ImGui::TextDisabled("%u x %u", sheet.picture.width, sheet.picture.height);
        ImGui::Dummy(ImVec2(0.f, 4.f));
        ImGui::SetNextItemWidth(120.f);
        ImGui::InputInt("cell width", &sheet.cellWidth);
        ImGui::SetNextItemWidth(120.f);
        ImGui::InputInt("cell height", &sheet.cellHeight);
        ImGui::SetNextItemWidth(120.f);
        ImGui::InputInt("hold (ms)", &sheet.holdMs, 10, 100);
        sheet.cellWidth = std::max(1, sheet.cellWidth);
        sheet.cellHeight = std::max(1, sheet.cellHeight);
        sheet.holdMs = std::clamp(sheet.holdMs, kMinFrameMs, kMaxFrameMs);

        std::vector<ls::RasterBuffer> cells;
        std::string error;
        const bool fits = sliceSheet(sheet.picture, static_cast<uint32_t>(sheet.cellWidth),
                                     static_cast<uint32_t>(sheet.cellHeight), &cells, &error);
        if (fits) {
            const uint32_t columns = sheet.picture.width / static_cast<uint32_t>(sheet.cellWidth);
            const uint32_t rows = sheet.picture.height / static_cast<uint32_t>(sheet.cellHeight);
            ImGui::Text("%zu frame(s): %u across, %u down", cells.size(), columns, rows);
            if (sheet.picture.width % static_cast<uint32_t>(sheet.cellWidth) != 0 ||
                sheet.picture.height % static_cast<uint32_t>(sheet.cellHeight) != 0) {
                ImGui::TextDisabled("The edge that does not make a whole cell is left out.");
            }
        } else {
            ImGui::PushStyleColor(ImGuiCol_Text, theme::palette().danger);
            ImGui::TextWrapped("%s", error.c_str());
            ImGui::PopStyleColor();
        }
        ImGui::Dummy(ImVec2(0.f, 6.f));
        ImGui::BeginDisabled(!fits);
        if (ImGui::Button("Open as frames", ImVec2(-1.f, 0.f))) {
            requestAction(editor, canvas, window, PendingAction::ImportSheet);
        }
        ImGui::EndDisabled();
    }
    ImGui::End();
    if (!open) {
        sheet = Editor::SheetImport{};
    }
}

// The new size, and a three-by-three grid saying where the drawing stays put
// as the canvas grows or shrinks around it -- the control every editor has,
// because it is the one question the numbers alone cannot answer.
void drawCanvasSizePanel(Editor& editor, CanvasView& canvas) {
    Editor::CanvasSizeDialog& dialog = editor.canvasDialog;
    if (!dialog.open) {
        return;
    }
    ImGui::OpenPopup("Canvas size");
    if (!ImGui::BeginPopupModal("Canvas size", &dialog.open,
                                ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }
    auto size = editor.doc.engine().getCanvasSize(editor.doc.id());
    if (size.ok()) {
        ImGui::TextDisabled("Now %d x %d", size.value.x, size.value.y);
    }
    ImGui::SetNextItemWidth(120.f);
    ImGui::InputInt("width", &dialog.width);
    ImGui::SetNextItemWidth(120.f);
    ImGui::InputInt("height", &dialog.height);
    dialog.width = std::clamp(dialog.width, 1, static_cast<int>(kMaxCanvasDimension));
    dialog.height = std::clamp(dialog.height, 1, static_cast<int>(kMaxCanvasDimension));

    ImGui::Dummy(ImVec2(0.f, 4.f));
    ImGui::TextUnformatted("Keep the drawing at");
    for (int i = 0; i < 9; ++i) {
        if (i % 3 != 0) {
            ImGui::SameLine();
        }
        ImGui::PushID(i);
        const bool chosen = static_cast<int>(dialog.anchor) == i;
        if (ImGui::Selectable(chosen ? "#" : ".", chosen, 0, ImVec2(22.f, 22.f))) {
            dialog.anchor = static_cast<CanvasAnchor>(i);
        }
        ImGui::PopID();
    }
    ImGui::PushStyleColor(ImGuiCol_Text, theme::palette().textDim);
    ImGui::TextWrapped("Pixels a smaller canvas leaves outside are kept, not "
                       "thrown away: grow it again and they are back.");
    ImGui::PopStyleColor();

    ImGui::Dummy(ImVec2(0.f, 6.f));
    if (ImGui::Button("Resize", ImVec2(110.f, 0.f))) {
        const uint32_t w = static_cast<uint32_t>(dialog.width);
        const uint32_t h = static_cast<uint32_t>(dialog.height);
        const CanvasAnchor anchor = dialog.anchor;
        changeCanvas(editor, canvas, [&](std::string* error) {
            return resizeCanvas(editor.doc, w, h, anchor, error);
        }, "Canvas resized");
        dialog.open = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(90.f, 0.f))) {
        dialog.open = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

// The layers an adjustment covers.
std::vector<ls::LayerId> adjustLayers(Editor& editor, int scope) {
    std::vector<ls::LayerId> layers;
    if (scope == 0) {
        for (ls::LayerId layer : editor.selectedLayers) {
            layers.push_back(layer);
        }
        if (layers.empty() && editor.active() != nullptr) {
            layers.push_back(editor.active()->layer);
        }
        return layers;
    }
    if (scope == 1) {
        return layerOrder(editor.doc, editor.activeSprite());
    }
    for (const Frame& frame : editor.frames) {
        for (ls::LayerId layer : layerOrder(editor.doc, frame.sprite)) {
            layers.push_back(layer);
        }
    }
    return layers;
}

void drawAdjustPanel(Editor& editor, CanvasView& canvas) {
    Editor::AdjustDialog& dialog = editor.adjustDialog;
    if (!dialog.open) {
        return;
    }
    // What it covers is read once, then again whenever the choice changes --
    // the old choice put back first, so nothing is adjusted twice.
    if (!dialog.read || dialog.readScope != dialog.scope || dialog.readSlots != dialog.slots) {
        if (dialog.read) {
            applyAdjust(editor.doc, dialog.base, ColourAdjust{});
        }
        dialog.base = adjustBase(editor.doc, adjustLayers(editor, dialog.scope), dialog.slots);
        applyAdjust(editor.doc, dialog.base, dialog.adjust);
        dialog.read = true;
        dialog.readScope = dialog.scope;
        dialog.readSlots = dialog.slots;
        canvas.invalidate();
    }
    ImGui::OpenPopup("Adjust colours");
    ImGui::SetNextWindowSize(ImVec2(380.f, 0.f), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal("Adjust colours", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }
    const char* scopes[] = { "This layer", "Every layer of this frame", "Every frame" };
    ImGui::SetNextItemWidth(220.f);
    ImGui::Combo("covers", &dialog.scope, scopes, 3);
    ImGui::Checkbox("Palette slots too", &dialog.slots);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("The slots these layers paint through change as well -- and so "
                          "does everything else that paints through them.");
    }
    ImGui::TextDisabled("%zu colour(s)%s", dialog.base.sites.size() + dialog.base.slots.size(),
                        dialog.base.slots.empty() ? "" : ", slots included");

    ColourAdjust& a = dialog.adjust;
    bool changed = false;
    ImGui::SetNextItemWidth(260.f);
    changed |= ImGui::SliderFloat("hue", &a.hue, -180.f, 180.f, "%.0f deg");
    ImGui::SetNextItemWidth(260.f);
    changed |= ImGui::SliderFloat("saturation", &a.saturation, -1.f, 1.f, "%.2f");
    ImGui::SetNextItemWidth(260.f);
    changed |= ImGui::SliderFloat("lightness", &a.lightness, -1.f, 1.f, "%.2f");
    ImGui::SetNextItemWidth(260.f);
    changed |= ImGui::SliderFloat("brightness", &a.brightness, -1.f, 1.f, "%.2f");
    ImGui::SetNextItemWidth(260.f);
    changed |= ImGui::SliderFloat("contrast", &a.contrast, -1.f, 1.f, "%.2f");
    changed |= ImGui::Checkbox("Invert", &a.invert);
    if (changed) {
        applyAdjust(editor.doc, dialog.base, a);
        canvas.invalidate();
    }

    ImGui::Dummy(ImVec2(0.f, 6.f));
    const auto close = [&](bool keep) {
        if (keep) {
            editor.doc.endAction();
        } else {
            editor.doc.abandonAction();
        }
        dialog.open = false;
        dialog.read = false;
        refreshInks(editor);
        canvas.invalidate();
        ImGui::CloseCurrentPopup();
    };
    if (ImGui::Button("Apply", ImVec2(100.f, 0.f))) {
        close(true);
        editor.say("Adjusted; every element is still what it was");
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset", ImVec2(80.f, 0.f))) {
        a = ColourAdjust{};
        applyAdjust(editor.doc, dialog.base, a);
        canvas.invalidate();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(90.f, 0.f)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        close(false);
    }
    ImGui::EndPopup();
}

// Select > Modify: grow, shrink or border the selection by a number of
// pixels, square or round, previewed as the marching ants move.
void drawModifyPanel(Editor& editor) {
    Editor::ModifyDialog& dialog = editor.modifyDialog;
    if (!dialog.open) {
        return;
    }
    const char* title = dialog.how == SelectionModify::Expand ? "Expand the selection"
                      : dialog.how == SelectionModify::Contract ? "Contract the selection"
                                                                : "Border of the selection";
    const std::string id = std::string(tr(title)) + "###modify";
    ImGui::OpenPopup(id.c_str());
    if (!ImGui::BeginPopupModal(id.c_str(), &dialog.open, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }
    ImGui::SetNextItemWidth(160.f);
    if (ImGui::InputInt("pixels", &dialog.pixels)) {
        dialog.pixels = std::clamp(dialog.pixels, 1, 256);
    }
    ImGui::Checkbox("Round corners", &dialog.round);
    ImGui::PushStyleColor(ImGuiCol_Text, theme::palette().textDim);
    ImGui::TextWrapped(dialog.how == SelectionModify::Border
                           ? "Keeps only a band this wide just inside the edge -- the "
                             "pixels Edit > Stroke would paint."
                           : "Square keeps a rectangle a rectangle; round keeps a circle "
                             "a circle.");
    ImGui::PopStyleColor();
    ImGui::Dummy(ImVec2(0.f, 6.f));
    if (ImGui::Button("OK", ImVec2(110.f, 0.f))) {
        auto size = editor.doc.engine().getCanvasSize(editor.doc.id());
        if (size.ok()) {
            settleFloating(editor);
            editor.selection.previous = editor.selection.mask;
            editor.selection.mask = modifySelection(
                editor.selection.mask, dialog.how, dialog.pixels, dialog.round,
                static_cast<uint32_t>(size.value.x), static_cast<uint32_t>(size.value.y));
            editor.say(editor.selection.empty() ? "Nothing left selected -- Reselect brings it back"
                                                : "Selection changed; Reselect brings the old one back");
        }
        dialog.open = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(90.f, 0.f))) {
        dialog.open = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

// Scaling the whole sprite to any size, by pixels or by percent, with the
// proportion kept unless unlocked.
void drawSpriteSizePanel(Editor& editor, CanvasView& canvas) {
    Editor::SpriteSizeDialog& dialog = editor.spriteDialog;
    if (!dialog.open) {
        return;
    }
    ImGui::OpenPopup("Sprite size");
    if (!ImGui::BeginPopupModal("Sprite size", &dialog.open, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }
    auto size = editor.doc.engine().getCanvasSize(editor.doc.id());
    if (size.fail() || size.value.x <= 0 || size.value.y <= 0) {
        ImGui::EndPopup();
        return;
    }
    const float w0 = static_cast<float>(size.value.x);
    const float h0 = static_cast<float>(size.value.y);
    ImGui::TextDisabled("Now %d x %d", size.value.x, size.value.y);
    ImGui::Checkbox("Keep the proportion", &dialog.lockRatio);

    // Pixels and percent are two views of one choice: editing either sets the
    // other, and a locked proportion carries each change across.
    const auto fromPixels = [&](bool widthChanged) {
        if (dialog.lockRatio) {
            if (widthChanged) {
                dialog.height = std::max(1, static_cast<int>(std::lround(dialog.width * h0 / w0)));
            } else {
                dialog.width = std::max(1, static_cast<int>(std::lround(dialog.height * w0 / h0)));
            }
        }
        dialog.percentX = static_cast<float>(dialog.width) * 100.f / w0;
        dialog.percentY = static_cast<float>(dialog.height) * 100.f / h0;
    };
    ImGui::SetNextItemWidth(120.f);
    if (ImGui::InputInt("width", &dialog.width)) {
        dialog.width = std::clamp(dialog.width, 1, static_cast<int>(kMaxCanvasDimension));
        fromPixels(true);
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120.f);
    if (ImGui::InputInt("height", &dialog.height)) {
        dialog.height = std::clamp(dialog.height, 1, static_cast<int>(kMaxCanvasDimension));
        fromPixels(false);
    }
    const auto fromPercent = [&](bool across) {
        if (dialog.lockRatio) {
            if (across) {
                dialog.percentY = dialog.percentX;
            } else {
                dialog.percentX = dialog.percentY;
            }
        }
        dialog.width = std::clamp(static_cast<int>(std::lround(w0 * dialog.percentX / 100.f)), 1,
                                  static_cast<int>(kMaxCanvasDimension));
        dialog.height = std::clamp(static_cast<int>(std::lround(h0 * dialog.percentY / 100.f)), 1,
                                   static_cast<int>(kMaxCanvasDimension));
    };
    ImGui::SetNextItemWidth(120.f);
    if (ImGui::InputFloat("% across", &dialog.percentX, 25.f, 100.f, "%.1f")) {
        dialog.percentX = std::clamp(dialog.percentX, 1.f, 10000.f);
        fromPercent(true);
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120.f);
    if (ImGui::InputFloat("% down", &dialog.percentY, 25.f, 100.f, "%.1f")) {
        dialog.percentY = std::clamp(dialog.percentY, 1.f, 10000.f);
        fromPercent(false);
    }
    for (int percent : { 50, 200, 300, 400 }) {
        const std::string label = std::to_string(percent) + "%";
        if (ImGui::SmallButton(label.c_str())) {
            dialog.percentX = static_cast<float>(percent);
            dialog.percentY = static_cast<float>(percent);
            fromPercent(true);
        }
        ImGui::SameLine();
    }
    ImGui::NewLine();

    const bool whole = dialog.width % size.value.x == 0 && dialog.height % size.value.y == 0 &&
                       dialog.width / size.value.x == dialog.height / size.value.y;
    ImGui::PushStyleColor(ImGuiCol_Text, theme::palette().textDim);
    ImGui::TextWrapped(whole ? "A whole-number enlargement: every pixel becomes a block, "
                               "exactly, and reducing again gives it back."
                             : "Nearest neighbour: some rows and columns are doubled or "
                               "dropped. Shapes are scaled as shapes, not as pixels.");
    ImGui::PopStyleColor();

    ImGui::Dummy(ImVec2(0.f, 6.f));
    if (ImGui::Button("Resize", ImVec2(110.f, 0.f))) {
        const uint32_t w = static_cast<uint32_t>(dialog.width);
        const uint32_t h = static_cast<uint32_t>(dialog.height);
        changeCanvas(editor, canvas, [&](std::string* error) {
            return resizeSprite(editor.doc, w, h, error);
        }, "Resized to " + std::to_string(w) + " x " + std::to_string(h));
        dialog.open = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(90.f, 0.f))) {
        dialog.open = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

// The canvas as the preferences have it: grid on or off, the chequer's
// colours and size, and the grid's colour.
ImU32 packedColour(uint32_t rgb, int alpha) {
    return IM_COL32((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF, alpha);
}

void applyViewPreferences(const Preferences& prefs, CanvasView& canvas) {
    canvas.setGridVisible(prefs.pixelGrid);
    canvas.setChecker(packedColour(prefs.checkerLight, 255), packedColour(prefs.checkerDark, 255),
                      static_cast<float>(prefs.checkerSize));
    canvas.setGridColour(packedColour(prefs.gridColour, prefs.gridOpacity));
}

// An 0xRRGGBB preference as a colour button; true when it was changed.
bool editColour(const char* label, uint32_t* rgb) {
    float colour[3] = { static_cast<float>((*rgb >> 16) & 0xFF) / 255.f,
                        static_cast<float>((*rgb >> 8) & 0xFF) / 255.f,
                        static_cast<float>(*rgb & 0xFF) / 255.f };
    if (!ImGui::ColorEdit3(label, colour, ImGuiColorEditFlags_NoInputs)) {
        return false;
    }
    const auto byte = [](float v) {
        return static_cast<uint32_t>(std::clamp(v, 0.f, 1.f) * 255.f + 0.5f);
    };
    *rgb = (byte(colour[0]) << 16) | (byte(colour[1]) << 8) | byte(colour[2]);
    return true;
}

// Preferences: what a new document is, autosave, the grid and the history's
// length on one tab; every command and its keys on the other, where a key is
// given by pressing it. Everything is written the moment it changes.
void drawPreferencesPanel(Editor& editor, CanvasView& canvas) {
    if (!editor.preferencesOpen) {
        return;
    }
    ImGui::SetNextWindowSize(ImVec2(520.f, 560.f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Preferences", &editor.preferencesOpen)) {
        ImGui::End();
        return;
    }
    Preferences& prefs = editor.prefs;
    bool changed = false;
    if (ImGui::BeginTabBar("preference-tabs")) {
        if (ImGui::BeginTabItem("General")) {
            theme::sectionHeader("A NEW DOCUMENT");
            int size[2] = { static_cast<int>(prefs.newWidth), static_cast<int>(prefs.newHeight) };
            ImGui::SetNextItemWidth(200.f);
            if (ImGui::InputInt2("size", size)) {
                size[0] = std::clamp(size[0], 1, static_cast<int>(kMaxCanvasDimension));
                size[1] = std::clamp(size[1], 1, static_cast<int>(kMaxCanvasDimension));
                if (static_cast<uint64_t>(size[0]) * static_cast<uint64_t>(size[1]) <=
                    kMaxCanvasPixels) {
                    prefs.newWidth = static_cast<uint32_t>(size[0]);
                    prefs.newHeight = static_cast<uint32_t>(size[1]);
                    changed = true;
                }
            }
            const char* backgrounds[] = { "Transparent", "White", "Black", "The palette's first colour" };
            ImGui::SetNextItemWidth(200.f);
            changed |= ImGui::Combo("background", &prefs.newBackground, backgrounds, 4);
            const std::vector<PalettePreset>& presets = palettePresets();
            const char* current = prefs.newPreset < 0 ||
                                  prefs.newPreset >= static_cast<int>(presets.size())
                ? "Starter" : presets[static_cast<size_t>(prefs.newPreset)].name.c_str();
            ImGui::SetNextItemWidth(200.f);
            if (ImGui::BeginCombo("palette", current)) {
                if (ImGui::Selectable("Starter", prefs.newPreset < 0)) {
                    prefs.newPreset = -1;
                    changed = true;
                }
                for (size_t i = 0; i < presets.size(); ++i) {
                    if (ImGui::Selectable(presets[i].name.c_str(),
                                          prefs.newPreset == static_cast<int>(i))) {
                        prefs.newPreset = static_cast<int>(i);
                        changed = true;
                    }
                }
                ImGui::EndCombo();
            }

            theme::sectionHeader("WORKING");
            if (ImGui::Checkbox("Autosave a copy", &prefs.autosaveOn)) {
                editor.autosaveOn = prefs.autosaveOn && editor.recovery.active();
                changed = true;
            }
            int minutes = static_cast<int>(prefs.autosaveSeconds / 60);
            ImGui::SetNextItemWidth(200.f);
            if (ImGui::SliderInt("every", &minutes, 1, 30, "%d min")) {
                prefs.autosaveSeconds = static_cast<uint32_t>(minutes) * 60u;
                editor.recovery.setIntervalSeconds(prefs.autosaveSeconds);
                changed = true;
            }
            int history = static_cast<int>(prefs.historyLimit);
            ImGui::SetNextItemWidth(200.f);
            if (ImGui::SliderInt("undo steps", &history, 10, 2000)) {
                prefs.historyLimit = static_cast<uint32_t>(history);
                editor.doc.setHistoryLimit(prefs.historyLimit);
                changed = true;
            }

            theme::sectionHeader("LOOK");
            int look = prefs.lightTheme ? 1 : 0;
            const char* looks[] = { "Slate (dark)", "Paper (light)" };
            ImGui::SetNextItemWidth(200.f);
            if (ImGui::Combo("theme", &look, looks, 2)) {
                prefs.lightTheme = look == 1;
                theme::setLight(prefs.lightTheme);
                canvasDefaultsFor(prefs.lightTheme, &prefs);
                applyViewPreferences(prefs, canvas);
                changed = true;
            }

            {
                // The languages there are catalogues for, English first.
                std::vector<std::string> languages = languagesIn(languageFolder());
                languages.insert(languages.begin(), std::string());
                const auto shown = [](const std::string& code) {
                    return code.empty() ? std::string("English") : code;
                };
                ImGui::SetNextItemWidth(200.f);
                if (ImGui::BeginCombo(tr("Language"), shown(prefs.language).c_str())) {
                    for (const std::string& code : languages) {
                        if (ImGui::Selectable(shown(code).c_str(), code == prefs.language)) {
                            prefs.language = code;
                            applyLanguage(code);
                            changed = true;
                        }
                    }
                    ImGui::EndCombo();
                }
            }

            theme::sectionHeader("THE CANVAS");
            bool view = false;
            view |= ImGui::Checkbox("Pixel grid", &prefs.pixelGrid);
            ImGui::SameLine();
            view |= editColour("##grid", &prefs.gridColour);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(120.f);
            view |= ImGui::SliderInt("grid opacity", &prefs.gridOpacity, 0, 255);
            view |= editColour("##checker-light", &prefs.checkerLight);
            ImGui::SameLine();
            view |= editColour("##checker-dark", &prefs.checkerDark);
            ImGui::SameLine();
            ImGui::TextUnformatted("chequer");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(120.f);
            view |= ImGui::SliderInt("square", &prefs.checkerSize, 2, 64, "%d px");
            if (ImGui::Button("Canvas defaults")) {
                const Preferences defaults;
                prefs.pixelGrid = defaults.pixelGrid;
                prefs.gridColour = defaults.gridColour;
                prefs.gridOpacity = defaults.gridOpacity;
                prefs.checkerLight = defaults.checkerLight;
                prefs.checkerDark = defaults.checkerDark;
                prefs.checkerSize = defaults.checkerSize;
                view = true;
            }
            if (view) {
                applyViewPreferences(prefs, canvas);
                changed = true;
            }
            ImGui::EndTabItem();
        }
        const ImGuiTabItemFlags keysFlags =
            editor.preferencesShowKeys ? ImGuiTabItemFlags_SetSelected : 0;
        editor.preferencesShowKeys = false;
        if (ImGui::BeginTabItem("Keys", nullptr, keysFlags)) {
            // A key being given: the next chord pressed is it, Escape gives
            // up, Backspace removes the chord.
            if (!editor.rebinding.empty()) {
                const CommandInfo* info = findCommand(editor.rebinding);
                ImGui::TextColored(theme::palette().accent, "Press the keys for %s...",
                                   info != nullptr ? tr(info->label) : "?");
                ImGui::TextDisabled("Escape to give up, Backspace to remove this key.");
                Chord chord;
                if (chordPressed(&chord)) {
                    std::vector<Chord> chords = editor.keys.chordsFor(editor.rebinding);
                    const int at = editor.rebindingChord;
                    if (chord.key == "Backspace" && !chord.ctrl && !chord.shift && !chord.alt) {
                        if (at >= 0 && at < static_cast<int>(chords.size())) {
                            chords.erase(chords.begin() + at);
                        }
                        editor.keys.setChords(editor.rebinding, chords);
                        changed = true;
                    } else if (!(chord.key == "Escape")) {
                        if (at >= 0 && at < static_cast<int>(chords.size())) {
                            chords[static_cast<size_t>(at)] = chord;
                        } else if (chords.size() < 4) {
                            chords.push_back(chord);
                        }
                        editor.keys.setChords(editor.rebinding, chords);
                        changed = true;
                    }
                    editor.rebinding.clear();
                }
                if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
                    editor.rebinding.clear();
                }
                ImGui::Separator();
            }
            if (ImGui::Button("Reset every key")) {
                editor.keys.resetAll();
                changed = true;
            }
            ImGui::BeginChild("keys", ImVec2(0.f, 0.f));
            const char* group = "";
            for (const CommandInfo& info : commandList()) {
                if (std::string(group) != info.group) {
                    group = info.group;
                    // In capitals like the other headings -- in English; a
                    // translation is shown as it was written.
                    std::string header = tr(group);
                    if (header == group) {
                        for (char& ch : header) {
                            ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
                        }
                    }
                    theme::sectionHeader(header.c_str());
                }
                ImGui::PushID(info.id);
                ImGui::TextUnformatted(tr(info.label));
                ImGui::SameLine(190.f);
                const std::vector<Chord>& chords = editor.keys.chordsFor(info.id);
                for (size_t i = 0; i < chords.size(); ++i) {
                    ImGui::PushID(static_cast<int>(i));
                    const std::string text = chordText(chords[i]);
                    // A chord two commands share is marked: pressing it would
                    // fire both.
                    const bool shared = editor.keys.commandsUsing(chords[i]).size() > 1;
                    if (shared) {
                        ImGui::PushStyleColor(ImGuiCol_Text, theme::palette().danger);
                    }
                    if (ImGui::SmallButton(text.c_str())) {
                        editor.rebinding = info.id;
                        editor.rebindingChord = static_cast<int>(i);
                    }
                    if (shared) {
                        ImGui::PopStyleColor();
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("Another command has this key too.");
                        }
                    }
                    ImGui::SameLine();
                    ImGui::PopID();
                }
                if (chords.size() < 4 && ImGui::SmallButton("+")) {
                    editor.rebinding = info.id;
                    editor.rebindingChord = -1;
                }
                if (!editor.keys.isDefault(info.id)) {
                    ImGui::SameLine();
                    if (ImGui::SmallButton("default")) {
                        editor.keys.reset(info.id);
                        changed = true;
                    }
                }
                ImGui::PopID();
            }
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::End();
    if (changed) {
        editor.files.newDocument.width = prefs.newWidth;
        editor.files.newDocument.height = prefs.newHeight;
        editor.files.newDocument.background = prefs.newBackground;
        editor.files.newDocument.preset = prefs.newPreset;
        saveSettings(editor);
    }
}

// The history as a list. Clicking a step goes to just after it -- undoing or
// redoing as many steps as that takes -- and nothing is lost by looking: the
// steps after the one chosen stay as redo until something new is done.
void drawHistoryPanel(Editor& editor, CanvasView& canvas) {
    if (!editor.historyOpen) {
        return;
    }
    ImGui::SetNextWindowSize(ImVec2(260.f, 360.f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("History", &editor.historyOpen)) {
        const std::vector<std::string> done = editor.doc.undoLabels();
        const std::vector<std::string> undone = editor.doc.redoLabels();
        int target = -1;           // how many steps should be done afterwards
        if (ImGui::Selectable("(as opened)", done.empty())) {
            target = 0;
        }
        for (size_t i = 0; i < done.size(); ++i) {
            ImGui::PushID(static_cast<int>(i));
            if (ImGui::Selectable(done[i].c_str(), i + 1 == done.size())) {
                target = static_cast<int>(i) + 1;
            }
            ImGui::PopID();
        }
        ImGui::PushStyleColor(ImGuiCol_Text, theme::palette().textDim);
        for (size_t i = 0; i < undone.size(); ++i) {
            ImGui::PushID(static_cast<int>(1000000 + i));
            if (ImGui::Selectable(undone[i].c_str(), false)) {
                target = static_cast<int>(done.size() + i) + 1;
            }
            ImGui::PopID();
        }
        ImGui::PopStyleColor();
        if (target >= 0 && !editor.busy()) {
            settleFloating(editor);
            int now = static_cast<int>(editor.doc.undoLabels().size());
            while (now > target && editor.doc.undo()) { --now; }
            while (now < target && editor.doc.redo()) { ++now; }
            resyncLayers(editor);
            resyncReferences(editor, canvas);
            refreshInks(editor);
            canvas.invalidate();
        }
    }
    ImGui::End();
}

// The words for a text element, where the Text tool was clicked. Placed on
// the active layer in the left colour, as one undo step.
void drawTextPanel(Editor& editor, CanvasView& canvas) {
    if (!editor.textOpen) {
        return;
    }
    ImGui::OpenPopup("Text");
    if (!ImGui::BeginPopupModal("Text", &editor.textOpen, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }
    if (ImGui::IsWindowAppearing()) {
        ImGui::SetKeyboardFocusHere();
    }
    // Enter places the words, as in any text dialog; Ctrl+Enter starts a
    // new line of them. Escape lets it go.
    const bool entered = ImGui::InputTextMultiline(
        "##words", editor.textBuffer, sizeof(editor.textBuffer),
        ImVec2(300.f, ImGui::GetTextLineHeight() * 4.f),
        ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CtrlEnterForNewLine);
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        editor.textOpen = false;
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }
    ImGui::TextDisabled("Enter places it; Ctrl+Enter for a new line");
    ImGui::SetNextItemWidth(160.f);
    ImGui::SliderInt("size", &editor.textScale, 1, 8, "%dx");
    int width = 0;
    int height = 0;
    layOutText(editor.textBuffer, editor.textAt, editor.textScale, &width, &height);
    ImGui::TextDisabled("%d x %d pixels at %d, %d", width, height, editor.textAt.x,
                        editor.textAt.y);
    const bool empty = editor.textBuffer[0] == '\0';
    ImGui::BeginDisabled(empty);
    if (ImGui::Button("Place", ImVec2(110.f, 0.f)) || (entered && !empty)) {
        PaintLayer* layer = editor.active();
        TextSpec spec;
        spec.text = editor.textBuffer;
        spec.at = editor.textAt;
        spec.scale = editor.textScale;
        PaintLayer made;
        editor.doc.beginAction("Text");
        if (layer != nullptr && !activeLayerLocked(editor) &&
            addTextElement(editor.doc, layer->layer, spec, foregroundInk(editor), &made)) {
            editor.doc.endAction();
            editor.activeElement = made.fill;
            resyncLayers(editor);
            canvas.invalidate();
            editor.say("Text placed -- retype it in the Element panel whenever you like");
        } else {
            editor.doc.abandonAction();
            editor.say(layer == nullptr ? "No layer to write on"
                                        : "This layer is locked -- unlock it to write on it");
        }
        editor.textOpen = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(90.f, 0.f))) {
        editor.textOpen = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

// The New window: a size -- typed, or one of the sizes sprites are usually
// drawn at -- what the canvas starts on, and which palette.
void drawNewDocumentPanel(Editor& editor, CanvasView& canvas, SDL_Window* window) {
    if (!editor.newDocumentOpen) {
        return;
    }
    FileState::NewDocument& spec = editor.files.newDocument;
    ImGui::OpenPopup("New document");
    if (!ImGui::BeginPopupModal("New document", &editor.newDocumentOpen,
                                ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }
    int width = static_cast<int>(spec.width);
    int height = static_cast<int>(spec.height);
    ImGui::SetNextItemWidth(120.f);
    ImGui::InputInt("width", &width);
    ImGui::SetNextItemWidth(120.f);
    ImGui::InputInt("height", &height);
    for (int size : { 16, 24, 32, 48, 64, 128, 256 }) {
        const std::string label = std::to_string(size);
        if (ImGui::SmallButton(label.c_str())) {
            width = height = size;
        }
        ImGui::SameLine();
    }
    ImGui::NewLine();
    spec.width = static_cast<uint32_t>(std::clamp(width, 1, static_cast<int>(kMaxCanvasDimension)));
    spec.height = static_cast<uint32_t>(std::clamp(height, 1, static_cast<int>(kMaxCanvasDimension)));
    const bool fits = static_cast<uint64_t>(spec.width) * spec.height <= kMaxCanvasPixels;

    ImGui::Dummy(ImVec2(0.f, 4.f));
    const char* backgrounds[] = { "Transparent", "White", "Black", "The palette's first colour" };
    ImGui::SetNextItemWidth(220.f);
    ImGui::Combo("background", &spec.background, backgrounds, 4);

    const std::vector<PalettePreset>& presets = palettePresets();
    const char* current = spec.preset < 0 ? "Starter"
                                          : presets[static_cast<size_t>(spec.preset)].name.c_str();
    ImGui::SetNextItemWidth(220.f);
    if (ImGui::BeginCombo("palette", current)) {
        if (ImGui::Selectable("Starter", spec.preset < 0)) {
            spec.preset = -1;
        }
        for (size_t i = 0; i < presets.size(); ++i) {
            if (ImGui::Selectable(presets[i].name.c_str(), spec.preset == static_cast<int>(i))) {
                spec.preset = static_cast<int>(i);
            }
        }
        ImGui::EndCombo();
    }
    if (!fits) {
        ImGui::TextColored(theme::palette().danger, "Larger than a canvas Fast works on.");
    }
    ImGui::Dummy(ImVec2(0.f, 6.f));
    ImGui::BeginDisabled(!fits);
    if (ImGui::Button("Create", ImVec2(110.f, 0.f))) {
        editor.newDocumentOpen = false;
        ImGui::CloseCurrentPopup();
        requestAction(editor, canvas, window, PendingAction::NewDocument);
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(90.f, 0.f))) {
        editor.newDocumentOpen = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

// What was found waiting from a session that did not end normally.
//
// The choice is deliberately not "recover or not". It is: open this, or throw
// it away -- and throwing away is one click further, behind naming what is
// being lost. Work that survived a crash has already been through enough.
void drawRecoveryPrompt(Editor& editor, CanvasView& canvas) {
    if (!editor.askingToRecover || editor.recovered.empty()) {
        return;
    }
    if (!ImGui::IsPopupOpen("Unfinished work")) {
        ImGui::OpenPopup("Unfinished work");
    }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(),
                            ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal("Unfinished work", nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    ImGui::TextWrapped("Sprit's'fast closed without saving last time. These "
                       "copies were kept:");
    ImGui::Dummy(ImVec2(0.f, 4.f));

    for (size_t i = 0; i < editor.recovered.size(); ++i) {
        const RecoveredWork& work = editor.recovered[i];
        ImGui::PushID(static_cast<int>(i));
        if (ImGui::Button("Open")) {
            // Through openPath, so the view state and everything else in the
            // package comes back. It opens as the *copy*, which is then
            // marked as changed -- the person saves it where they want it,
            // and the original on disk is untouched until they do.
            openPath(editor, canvas, work.path);
            // It is not that file: the recovery copy is about to be deleted,
            // and Save must ask where it really goes rather than writing back
            // into the settings folder.
            editor.doc.forgetPath();
            editor.doc.markModified();
            if (!work.originalPath.empty()) {
                editor.say("Recovered. Save it back over " +
                           fileName(work.originalPath) + " when you are happy with it.");
            } else {
                editor.say("Recovered work that had never been saved.");
            }
            discardRecoveredWork(work);
            editor.recovered.erase(editor.recovered.begin() +
                                   static_cast<ptrdiff_t>(i));
            editor.askingToRecover = !editor.recovered.empty();
            if (!editor.askingToRecover) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::PopID();
            ImGui::EndPopup();
            return;
        }
        ImGui::SameLine();
        ImGui::TextUnformatted(work.name.c_str());
        if (!work.originalPath.empty() && ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", work.originalPath.c_str());
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Discard")) {
            discardRecoveredWork(work);
            editor.recovered.erase(editor.recovered.begin() +
                                   static_cast<ptrdiff_t>(i));
            editor.askingToRecover = !editor.recovered.empty();
            if (!editor.askingToRecover) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::PopID();
            ImGui::EndPopup();
            return;
        }
        ImGui::PopID();
    }

    ImGui::Dummy(ImVec2(0.f, 6.f));
    if (ImGui::Button("Later")) {
        // Kept, not deleted: closing the question is not the same as saying
        // the work is worthless, and it will be offered again next time.
        editor.askingToRecover = false;
        ImGui::CloseCurrentPopup();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Leave them where they are. You will be asked again "
                          "next time.");
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
    if (!ImGui::BeginPopupModal("Unsaved changes", nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    const std::string name = editor.doc.path().empty()
                           ? std::string("This document")
                           : fileName(editor.doc.path());
    ImGui::Text("%s has unsaved changes.", name.c_str());
    ImGui::Dummy(ImVec2(0.f, 6.f));

    if (ImGui::Button("Save", ImVec2(112.f, 0.f))) {
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
    if (ImGui::Button("Discard", ImVec2(112.f, 0.f))) {
        const PendingAction action = editor.files.pending;
        const std::string path = editor.files.pendingPath;
        editor.files.pending = PendingAction::None;
        editor.files.askingToSave = false;
        performAction(editor, canvas, window, action, path);
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(112.f, 0.f)) ||
        ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        editor.files.pending = PendingAction::None;
        editor.files.pendingPath.clear();
        editor.files.askingToSave = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

// The symmetry axes, as thin dashed lines across the canvas, so it is never a
// surprise that a stroke is being mirrored.
void drawSymmetryAxes(Editor& editor, const CanvasView& canvas, ImDrawList* draw,
                      ImVec2 origin, float zoom) {
    if (!editor.symmetryAcross && !editor.symmetryDown) {
        return;
    }
    const Symmetry symmetry = symmetryNow(editor);
    const float width = static_cast<float>(canvas.compiledWidth()) * zoom;
    const float height = static_cast<float>(canvas.compiledHeight()) * zoom;
    const ImU32 colour = IM_COL32(255, 170, 60, 170);
    const float dash = 6.f;
    if (symmetry.across) {
        const float x = origin.x + static_cast<float>(symmetry.axisX) * 0.5f * zoom;
        for (float y = 0.f; y < height; y += dash * 2.f) {
            draw->AddLine(ImVec2(x, origin.y + y),
                          ImVec2(x, origin.y + std::min(y + dash, height)), colour, 1.f);
        }
    }
    if (symmetry.down) {
        const float y = origin.y + static_cast<float>(symmetry.axisY) * 0.5f * zoom;
        for (float x = 0.f; x < width; x += dash * 2.f) {
            draw->AddLine(ImVec2(origin.x + x, y),
                          ImVec2(origin.x + std::min(x + dash, width), y), colour, 1.f);
        }
    }
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
        Reference made;
        std::string error;
        if (importReference(editor.doc, path, &made, &error)) {
            resyncReferences(editor, canvas);
            editor.activeReference = made.id;
            canvas.invalidate();
            editor.say("Imported " + made.name + " -- it travels with this file");
        } else {
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

// ------------------------------------------------------------------ input --

// Alt held turns a drag on the canvas into moving the selected reference
// rather than drawing. Alt because it is the one modifier no tool uses, and
// on the canvas because a reference is placed by eye against the artwork --
// the number fields in the panel are for when it has to be exact.
//
// Returns true when it took the drag, so the tools stand aside.
bool handleReferenceDrag(Editor& editor, CanvasView& canvas, bool overCanvas,
                         ls::Vec2i pixel) {
    const bool alt = ImGui::GetIO().KeyAlt;
    if (!editor.draggingReference && (!alt || !overCanvas)) {
        return false;
    }
    Reference* active = activeReference(editor);
    if (active == nullptr || active->locked || !active->visible) {
        if (alt && overCanvas && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            editor.say(active == nullptr ? "No reference to move"
                                         : "That reference is locked");
        }
        return false;
    }

    const ls::Vec2f here { static_cast<float>(pixel.x), static_cast<float>(pixel.y) };
    if (overCanvas && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        editor.draggingReference = true;
        editor.referenceGrabbed = { here.x - active->x, here.y - active->y };
        editor.doc.beginAction("Move reference");
        return true;
    }
    if (editor.draggingReference && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        active->x = here.x - editor.referenceGrabbed.x;
        active->y = here.y - editor.referenceGrabbed.y;
        updateReference(editor.doc, *active);
        canvas.invalidate();
        return true;
    }
    if (editor.draggingReference && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        editor.draggingReference = false;
        editor.doc.endAction();
        editor.say("Reference moved");
        return true;
    }
    return editor.draggingReference;
}

// What a pencil or a bucket paints into. Normally the ink -- the left
// button's, or the right one's -- which finds or makes that colour's element
// on the layer. But a dithered element picked in the element list is painted
// into as itself, so pixels drawn there take the dither rather than a flat
// colour: the one way to paint with a rule rather than a value.
bool beginPaint(Editor& editor, const PaintLayer& layer, bool back, InkStroke* out) {
    if (!back && editor.paintIntoElement && editor.activeElement.valid()) {
        for (const Element& element : elementsOf(editor.doc, layer.layer)) {
            if (element.fill != editor.activeElement || element.kind != ElementKind::Paint) {
                continue;
            }
            Ink solid;
            if (!inkOfElement(editor.doc, element.fill, &solid)) {
                PaintLayer target;
                target.layer = layer.layer;
                target.fill = element.fill;
                target.region = element.region;
                return beginElementStroke(editor.doc, target, out);
            }
        }
    }
    return beginInkStroke(editor.doc, layer.layer,
                          back ? backgroundInk(editor) : foregroundInk(editor), out);
}

// The palette the active frame draws with, in the order its swatches show --
// the ramp Shading steps along.
std::vector<std::pair<ls::ColorRole, ls::Color>> paletteRamp(Editor& editor) {
    std::vector<std::pair<ls::ColorRole, ls::Color>> ramp;
    for (const PaletteEntry& entry :
         paletteEntries(editor.doc, paletteFor(editor.doc, editor.activeSprite()))) {
        ramp.push_back({ entry.role, entry.color });
    }
    return ramp;
}

// The last step of every freehand mark -- pencil, spray, eraser, contour:
// wrapped for tiled mode, mirrored for symmetry, kept inside the selection,
// and laid down through the ink mode.
void layDown(Editor& editor, CanvasView& canvas, std::vector<ls::Vec2i> run, bool tiled) {
    if (tiled) {
        for (ls::Vec2i& at : run) {
            at = canvas.wrap(at);
        }
    }
    run = mirrored(run, symmetryNow(editor));
    if (!editor.selection.empty()) {
        run.erase(std::remove_if(run.begin(), run.end(), [&](ls::Vec2i at) {
                      return !editor.selection.contains(at);
                  }),
                  run.end());
    }
    if (run.empty()) {
        return;
    }
    if (editor.inkStroke.erasing()) {
        strokeInk(editor.doc, editor.inkStroke, run);
    } else {
        strokeInkMode(editor.doc, editor.inkModeState, editor.inkStroke, run,
                      !editor.strokeWithBack);
    }
    canvas.invalidate();
}

// Whether the pencil is stamping a custom brush rather than its own.
bool usingCustomBrush(const Editor& editor) {
    return editor.tool == Tool::Pencil && editor.customBrushOn && !editor.customBrush.empty();
}

// A custom brush's colour, laid down the way layDown lays a stroke: wrapped,
// mirrored, kept inside the selection -- through its own ink stroke.
void layDownWith(Editor& editor, CanvasView& canvas, const InkStroke& stroke,
                 std::vector<ls::Vec2i> run, bool tiled) {
    if (tiled) {
        for (ls::Vec2i& at : run) {
            at = canvas.wrap(at);
        }
    }
    run = mirrored(run, symmetryNow(editor));
    if (!editor.selection.empty()) {
        run.erase(std::remove_if(run.begin(), run.end(), [&](ls::Vec2i at) {
                      return !editor.selection.contains(at);
                  }),
                  run.end());
    }
    if (!run.empty()) {
        strokeInk(editor.doc, stroke, run);
        canvas.invalidate();
    }
}

// The ink under the cursor: the slot a pixel was painted through when the
// drawing knows it, and otherwise the colour on screen -- matched to a slot
// if one is exactly that colour, so picking a palette colour off a shape or
// a dither still paints through the palette.
bool pickInk(Editor& editor, CanvasView& canvas, ls::Vec2i pixel, Ink* out) {
    if (inkAt(editor.doc, editor.sprite, pixel, out)) {
        return true;
    }
    const ls::Color* seen = canvas.colorAt(pixel);
    if (seen == nullptr || seen->a == 0) {
        return false;
    }
    out->colour = *seen;
    out->role = ls::kColorRoleNone;
    for (const PaletteEntry& entry :
         paletteEntries(editor.doc, paletteFor(editor.doc, editor.sprite))) {
        if (entry.color.r == seen->r && entry.color.g == seen->g &&
            entry.color.b == seen->b && entry.color.a == seen->a) {
            out->role = entry.role;
            break;
        }
    }
    return true;
}

void handleStroke(Editor& editor, CanvasView& canvas, bool overCanvas, ls::Vec2i pixel) {
    // Alt belongs to the selection tools -- it subtracts -- so with one in
    // hand it does not also grab a reference.
    if ((editor.draggingReference || !isSelectionTool(editor.tool)) &&
        handleReferenceDrag(editor, canvas, overCanvas, pixel)) {
        return;
    }
    // A shape's handles come before the tool: a press on one edits the
    // shape whatever the tool would have done there.
    if (handleFreeScale(editor, canvas, overCanvas)) {
        return;
    }
    if (handleShapeHandles(editor, canvas, overCanvas)) {
        return;
    }
    if (handleSliceInput(editor, canvas, overCanvas)) {
        return;
    }
    if (handleSelectionInput(editor, canvas, overCanvas, pixel)) {
        return;
    }
    if (handlePathInput(editor, canvas, overCanvas)) {
        return;
    }
    // The hand and the zoom tool are the canvas's own business.
    if (editor.tool == Tool::Hand || editor.tool == Tool::Zoom) {
        return;
    }
    PaintLayer* layer = editor.active();
    if (layer == nullptr) {
        return;
    }
    // A locked layer: the picker still reads, nothing writes. Said once per
    // press rather than per frame, or the status line would flicker.
    if (editor.tool != Tool::Picker && activeLayerLocked(editor) && overCanvas &&
        (ImGui::IsMouseClicked(ImGuiMouseButton_Left) ||
         ImGui::IsMouseClicked(ImGuiMouseButton_Right))) {
        editor.say("This layer is locked -- unlock it in the Layers panel");
        return;
    }
    if (editor.tool != Tool::Picker && activeLayerLocked(editor)) {
        return;
    }

    if (editor.tool == Tool::Picker) {
        const bool left = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
        const bool right = ImGui::IsMouseClicked(ImGuiMouseButton_Right);
        if (overCanvas && (left || right)) {
            Ink picked;
            if (!pickInk(editor, canvas, pixel, &picked)) {
                editor.say("Nothing to pick there");
                return;
            }
            if (left) { setForegroundInk(editor, picked); }
            else      { setBackgroundInk(editor, picked); }
            refreshInks(editor);
            editor.tool = editor.toolBeforePicker;
            editor.say(picked.usesSlot()
                ? "Picked slot " + std::to_string(picked.role) +
                  " -- painting with it follows the palette"
                : std::string("Picked the colour under the cursor"));
        }
        return;
    }

    // Shapes are created on press and driven while the mouse moves, so what is
    // on screen during the drag is the real object rather than a preview that
    // then has to be reproduced exactly.
    if (editor.tool == Tool::Rectangle || editor.tool == Tool::Ellipse ||
        editor.tool == Tool::Line) {
        const ShapeKind kind = editor.tool == Tool::Rectangle ? ShapeKind::Rectangle
                             : editor.tool == Tool::Ellipse   ? ShapeKind::Ellipse
                                                              : ShapeKind::Line;
        ls::Vec2f here { static_cast<float>(pixel.x), static_cast<float>(pixel.y) };
        if (editor.snapToGrid) {
            const ls::Vec2i point = nearestGridPoint(canvas.pointerExact(), editor.snapGrid);
            here = { static_cast<float>(point.x), static_cast<float>(point.y) };
        }

        if (overCanvas && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            ShapeParams params;
            params.from = here;
            params.to = { here.x + 1.f, here.y + 1.f };
            params.cornerRadius = editor.shapeCorner;
            params.outline = editor.shapeOutline && kind != ShapeKind::Line;
            params.thickness = params.outline ? editor.shapeOutlineWidth : 1.f;
            // Onto the active layer as one more element, unless asked for a
            // layer per shape. Either way the panel then shows the thing
            // under the cursor.
            const bool ontoLayer = !editor.shapesOnOwnLayer && !activeLayerLocked(editor);
            bool made = false;
            const Ink ink = foregroundInk(editor);
            if (ontoLayer) {
                made = addShapeElement(editor.doc, layer->layer, kind, params, ink,
                                       &editor.pendingShape);
            } else if (activeLayerLocked(editor) && !editor.shapesOnOwnLayer) {
                editor.say("This layer is locked -- unlock it, or draw shapes on their own layer");
            } else {
                // One history entry for the layer and its slot.
                editor.doc.beginAction(shapeKindName(kind));
                made = createShapeLayer(editor.doc, editor.sprite, kind, params,
                                        ink.colour, &editor.pendingShape);
                if (made && ink.usesSlot()) {
                    setLayerRole(editor.doc, editor.pendingShape.paint, ink.role);
                }
                editor.doc.endAction();
            }
            if (made) {
                editor.draggingShape = true;
                editor.shapeAnchor = here;
                if (ontoLayer) {
                    editor.activeElement = editor.pendingShape.paint.fill;
                } else {
                    selectLayer(editor, editor.pendingShape.paint.layer);
                    editor.activeElement = editor.pendingShape.paint.fill;
                }
                canvas.invalidate();
            }
        }

        if (editor.draggingShape && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            ShapeParams params;
            const ls::Vec2f anchor = editor.shapeAnchor;
            ls::Vec2f to = here;
            if (ImGui::GetIO().KeyShift) {
                const float dx = here.x - anchor.x;
                const float dy = here.y - anchor.y;
                if (kind == ShapeKind::Line) {
                    // Fifteen-degree steps: horizontal, vertical, the
                    // diagonals, and the slopes pixel art actually uses
                    // between them.
                    const float step = 3.14159265f / 12.f;
                    const float angle = std::round(std::atan2(dy, dx) / step) * step;
                    const float length = std::sqrt(dx * dx + dy * dy);
                    to = { anchor.x + std::round(std::cos(angle) * length),
                           anchor.y + std::round(std::sin(angle) * length) };
                } else {
                    const float side = std::max(std::fabs(dx), std::fabs(dy));
                    to = { anchor.x + (dx < 0.f ? -side : side),
                           anchor.y + (dy < 0.f ? -side : side) };
                }
            }
            if (kind == ShapeKind::Line || editor.snapToGrid) {
                // A line's two points are the drag itself; grid points are
                // corners already.
                params.from = anchor;
                params.to = to;
            } else {
                // The box holds both end pixels -- the one pressed on and the
                // one under the pointer -- as a marquee does, whichever way
                // the drag went.
                params.from = { std::min(anchor.x, to.x), std::min(anchor.y, to.y) };
                params.to = { std::max(anchor.x, to.x) + 1.f, std::max(anchor.y, to.y) + 1.f };
            }
            params.cornerRadius = editor.shapeCorner;
            updateShape(editor.doc, editor.pendingShape, params);
            canvas.invalidate();
        }

        if (editor.draggingShape && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            editor.draggingShape = false;
            editor.say(std::string(shapeKindName(kind)) +
                       " drawn, and still editable");
        }
        return;
    }

    if (editor.tool == Tool::Bucket) {
        const bool left = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
        const bool right = ImGui::IsMouseClicked(ImGuiMouseButton_Right);
        if (overCanvas && (left || right)) {
            editor.doc.beginAction("Fill");
            InkStroke stroke;
            bool filled = beginPaint(editor, *layer, right && !left, &stroke);
            if (filled) {
                // With symmetry on, each mirror of the click fills as well --
                // in the order given, so a fill that already covered a mirror
                // leaves nothing for it to do.
                bool any = false;
                for (ls::Vec2i seed : mirrored({ pixel }, symmetryNow(editor))) {
                    any = bucketFill(editor.doc, editor.sprite, stroke, seed, editor.bucket,
                                     editor.selection.empty() ? nullptr
                                                              : &editor.selection.mask) || any;
                }
                filled = any;
            }
            if (filled) {
                pruneEmptyInks(editor.doc, layer->layer, stroke.target.fill);
                editor.doc.endAction();
                resyncLayers(editor);
            } else {
                editor.doc.abandonAction();
            }
            canvas.invalidate();
            editor.say(filled ? "Filled" : "Nothing to fill there");
        }
        return;
    }

    // Text: a click says where; the words are typed in the window that opens.
    if (editor.tool == Tool::Text) {
        if (overCanvas && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
            !ImGui::IsKeyDown(ImGuiKey_Space) && !editor.textOpen) {
            editor.textOpen = true;
            editor.textAt = pixel;
        }
        return;
    }

    // Gradient: the press makes the element over the area -- the selection,
    // or what a fill would find -- and the drag drives its axis, so what is
    // on screen during the drag is the gradient itself.
    if (editor.tool == Tool::Gradient) {
        const ls::Vec2f here { static_cast<float>(canvas.pointerPixel().x) + 0.5f,
                               static_cast<float>(canvas.pointerPixel().y) + 0.5f };
        if (overCanvas && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
            !ImGui::IsKeyDown(ImGuiKey_Space)) {
            const std::vector<ls::Vec2i> area = editor.selection.empty()
                ? bucketArea(editor.doc, editor.sprite, pixel, editor.bucket)
                : pixelsOf(editor.selection.mask);
            DitherSettings settings;
            settings.pattern = editor.dither.pattern;
            settings.modulation = ls::DitherModulation::Linear;
            const Ink front = foregroundInk(editor);
            const Ink back = backgroundInk(editor);
            settings.from = front.colour;
            settings.fromRole = front.role;
            settings.to = back.colour;
            settings.toRole = back.role;
            settings.gradientStart = here;
            settings.gradientEnd = { here.x + 1.f, here.y };
            editor.doc.beginAction("Gradient");
            if (!area.empty() && addGradientElement(editor.doc, layer->layer, area, settings,
                                                    &editor.gradientElement)) {
                editor.drawingGradient = true;
                editor.gradientSettings = settings;
            } else {
                editor.doc.abandonAction();
                editor.say("Nothing to lay a gradient over there");
            }
        }
        if (editor.drawingGradient && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            if (here.x != editor.gradientSettings.gradientEnd.x ||
                here.y != editor.gradientSettings.gradientEnd.y) {
                editor.gradientSettings.gradientEnd = here;
                applyDitherSettings(editor.doc, editor.gradientElement,
                                    editor.gradientSettings);
            }
        }
        if (editor.drawingGradient && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            editor.drawingGradient = false;
            pruneEmptyInks(editor.doc, layer->layer, editor.gradientElement.fill);
            editor.doc.endAction();
            // The panel then shows the gradient, ready to be adjusted.
            editor.activeElement = editor.gradientElement.fill;
            editor.paintIntoElement = false;
            resyncLayers(editor);
            editor.say("A gradient that stays one -- adjust it in the Element panel");
        }
        return;
    }

    // Contour: the outline is drawn as the pointer goes, and on release what it
    // encloses -- outline included -- is painted in one go.
    if (editor.tool == Tool::Contour) {
        if (overCanvas && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
            !ImGui::IsKeyDown(ImGuiKey_Space)) {
            editor.drawingContour = true;
            editor.contourPoints.assign(1, pixel);
        }
        if (editor.drawingContour && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            const ls::Vec2i at = canvas.pointerPixel();
            const ls::Vec2i last = editor.contourPoints.back();
            if (at.x != last.x || at.y != last.y) {
                editor.contourPoints.push_back(at);
            }
        }
        if (editor.drawingContour && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            editor.drawingContour = false;
            auto size = editor.doc.engine().getCanvasSize(editor.doc.id());
            const std::vector<ls::Vec2i> inside = pixelsOf(clipToCanvas(
                lassoMask(editor.contourPoints),
                size.ok() ? static_cast<uint32_t>(size.value.x) : 0u,
                size.ok() ? static_cast<uint32_t>(size.value.y) : 0u));
            editor.contourPoints.clear();
            editor.doc.beginAction("Contour");
            editor.strokeWithBack = false;
            if (!inside.empty() && beginPaint(editor, *layer, false, &editor.inkStroke) &&
                beginInkMode(editor.doc, layer->layer, InkMode::Simple, Ink{}, {},
                             &editor.inkModeState)) {
                layDown(editor, canvas, inside, false);
                pruneEmptyInks(editor.doc, layer->layer, editor.inkStroke.target.fill);
                editor.doc.endAction();
                resyncLayers(editor);
            } else {
                editor.doc.abandonAction();
            }
            editor.inkStroke = InkStroke{};
        }
        return;
    }

    // In tiled mode the stroke follows the pointer as it really moved, off
    // the edge and all, and each pixel is brought back onto the canvas after
    // the path is drawn -- wrapping first would draw a line straight across
    // the canvas every time the pointer crossed a seam.
    const bool tiled = canvas.tiledMode() != TiledMode::None;
    if (tiled && overCanvas) {
        pixel = canvas.pointerPixel();
    }

    // A layer shown rotated is still drawn on straight. The pencil writes into
    // the region and the transform acts on it afterwards, so the point under
    // the cursor has to be carried back into the layer's own space.
    if (overCanvas) {
        ls::Vec2f mapped;
        if (mapCanvasPointToLayer(editor.doc, layer->layer,
                                  {static_cast<float>(pixel.x),
                                   static_cast<float>(pixel.y)}, &mapped)) {
            pixel = { static_cast<int32_t>(std::floor(mapped.x + 0.5f)),
                      static_cast<int32_t>(std::floor(mapped.y + 0.5f)) };
        } else {
            editor.say("This transform cannot be drawn through");
            overCanvas = false;
        }
    }

    // A whole drag is one history entry, so the bracket opens on press and
    // closes on release rather than per sample. Which element gains the
    // pixels, and which lose them, is settled here once: the ink's own
    // element, made at the top of the layer the first time a colour is used.
    const bool pressedLeft = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    const bool pressedRight = ImGui::IsMouseClicked(ImGuiMouseButton_Right);
    if (overCanvas && !editor.stroking && (pressedLeft || pressedRight) &&
        !ImGui::IsKeyDown(ImGuiKey_Space)) {
        const bool back = pressedRight && !pressedLeft;
        const bool erasing = editor.tool == Tool::Eraser;
        editor.doc.beginAction(editor.tool == Tool::Pencil ? "Pencil"
                               : editor.tool == Tool::Spray ? "Spray" : "Eraser");
        // Replace turns the other button's colour into this one's.
        const Ink other = back ? foregroundInk(editor) : backgroundInk(editor);
        const bool ready =
            (erasing ? beginEraseStroke(editor.doc, layer->layer, &editor.inkStroke)
                     : beginPaint(editor, *layer, back, &editor.inkStroke)) &&
            beginInkMode(editor.doc, layer->layer, erasing ? InkMode::Simple : editor.inkMode,
                         other, paletteRamp(editor), &editor.inkModeState);
        if (!ready) {
            editor.doc.abandonAction();
            editor.say("Nothing to draw on here");
            return;
        }
        editor.stroking = true;
        editor.strokeWithBack = back;
        editor.stabiliser.reset(canvas.pointerExact());
        editor.brushStrokes.clear();
        if (usingCustomBrush(editor)) {
            // A stroke per colour of the brush, all made first so that each
            // one's list of the layer's other colours includes the rest.
            std::vector<ls::OperationId> targets;
            for (const PixelClip::Piece& piece : editor.customBrush.pieces) {
                Ink ink = editor.customBrushOwnColours ? piece.ink
                        : (back ? backgroundInk(editor) : foregroundInk(editor));
                InkStroke made;
                if (beginInkStroke(editor.doc, layer->layer, ink, &made)) {
                    targets.push_back(made.target.fill);
                    editor.brushStrokes.push_back(made);
                }
            }
            for (InkStroke& made : editor.brushStrokes) {
                beginElementStroke(editor.doc, made.target, &made);
            }
        }
        // Shift+click: a straight line from where the last stroke ended.
        editor.lastPixel = ImGui::GetIO().KeyShift && editor.lastStrokeEnd.x >= 0
                               ? editor.lastStrokeEnd : pixel;
        editor.pixelPerfect.reset();
    }

    const ImGuiMouseButton button = editor.strokeWithBack ? ImGuiMouseButton_Right
                                                          : ImGuiMouseButton_Left;
    // With the stabiliser on, the stroke follows the point on the string
    // rather than the pointer itself -- only for the pencil and eraser, and
    // only where the layer is not transformed, since the point is in canvas
    // pixels.
    if (editor.stroking && editor.brush.stabiliser > 0 && editor.tool != Tool::Spray &&
        ImGui::IsMouseDown(button) && listTransforms(editor.doc, layer->layer).empty()) {
        const ls::Vec2f held = editor.stabiliser.follow(
            canvas.pointerExact(), static_cast<float>(editor.brush.stabiliser));
        pixel = { static_cast<int32_t>(std::floor(held.x)),
                  static_cast<int32_t>(std::floor(held.y)) };
        overCanvas = true;
    }
    if (editor.stroking && ImGui::IsMouseDown(button) && overCanvas) {
        // Interpolate: the mouse reports once a frame, not once a pixel. The
        // path is then laid down through the brush -- stamped at its size, or
        // at one pixel through the pixel-perfect filter, which holds each
        // point until the next says whether it was the corner of an L.
        const int size = editor.pen.down ? pressuredSize(editor.brush, editor.pen.pressure)
                                         : editor.brush.size;
        const bool perfect = size == 1 && editor.brush.pixelPerfect &&
                             editor.tool == Tool::Pencil;
        std::vector<ls::Vec2i> run;
        if (usingCustomBrush(editor)) {
            // The brush stamped at every pixel of the path, colour by colour.
            const std::vector<ls::Vec2i> path =
                editor.lastPixel.x < 0 ? std::vector<ls::Vec2i>{ pixel }
                                       : linePixels(editor.lastPixel, pixel);
            for (size_t n = 0; n < path.size(); ++n) {
                if (n == 0 && editor.lastPixel.x >= 0 && path.size() > 1) {
                    continue;              // stamped already, at the end of the last run
                }
                const std::vector<std::vector<ls::Vec2i>> stamp =
                    stampOf(editor.customBrush, path[n]);
                for (size_t k = 0; k < stamp.size() && k < editor.brushStrokes.size(); ++k) {
                    layDownWith(editor, canvas, editor.brushStrokes[k], stamp[k], tiled);
                }
            }
            editor.lastPixel = pixel;
        } else if (editor.tool == Tool::Spray) {
            // A burst every frame the button is held, moving or not, as a can
            // keeps spraying where it is pointed.
            run = sprayPixels(pixel, editor.sprayRadius, editor.sprayDensity,
                              ++editor.sprayBursts);
        } else if (perfect) {
            const std::vector<ls::Vec2i> path =
                editor.lastPixel.x < 0 ? std::vector<ls::Vec2i>{pixel}
                                       : linePixels(editor.lastPixel, pixel);
            for (ls::Vec2i point : path) {
                for (ls::Vec2i ready : editor.pixelPerfect.push(point)) {
                    run.push_back(ready);
                }
            }
        } else {
            run = editor.lastPixel.x < 0
                ? brushStamp(pixel, size, editor.brush.round)
                : strokePixels(editor.lastPixel, pixel, size, editor.brush.round);
        }
        if (!usingCustomBrush(editor)) {
            layDown(editor, canvas, std::move(run), tiled);
        }
        editor.lastPixel = pixel;
    }

    if (editor.stroking && ImGui::IsMouseReleased(button)) {
        // The filter's last point, held until now.
        std::vector<ls::Vec2i> rest = editor.pixelPerfect.finish();
        if (!rest.empty() && editor.tool == Tool::Pencil && !usingCustomBrush(editor)) {
            layDown(editor, canvas, std::move(rest), tiled);
        }
        editor.lastStrokeEnd = editor.lastPixel;
        // A colour painted out entirely, or erased away, leaves the element
        // list rather than lingering as an element that draws nothing.
        int pruned = pruneEmptyInks(editor.doc, editor.inkStroke.layer,
                                    editor.inkStroke.target.fill);
        if (!editor.brushStrokes.empty()) {
            pruned += pruneEmptyInks(editor.doc, editor.inkStroke.layer);
            editor.brushStrokes.clear();
        }
        // Shading lays colours down through strokes of its own; a slot it
        // stepped every pixel out of goes the same way.
        if (!editor.inkModeState.strokes.empty()) {
            pruned += pruneEmptyInks(editor.doc, editor.inkStroke.layer);
        }
        editor.doc.endAction();
        editor.stroking = false;
        editor.lastPixel = { -1, -1 };
        if (pruned > 0 || !editor.inkStroke.erasing()) {
            resyncLayers(editor);
        }
        editor.inkStroke = InkStroke{};
        editor.inkModeState = InkModeState{};
    }
}

void handleShortcuts(Editor& editor, CanvasView& canvas, SDL_Window* window) {
    ImGuiIO& io = ImGui::GetIO();

    // A modal question is on screen, a drag is in progress, or a text field has
    // the keyboard: none of them is a moment to act on a shortcut.
    if (editor.busy() || editor.files.askingToSave || io.WantTextInput ||
        !editor.rebinding.empty()) {
        return;
    }

    // The keys a selection or a float answers to come first; any other key
    // drops a float before doing what it does, so a shortcut never acts on a
    // document with pixels in the air.
    if (handlePathKeys(editor, canvas)) {
        return;
    }
    if (handleSliceKeys(editor)) {
        return;
    }
    if (handleSelectionKeys(editor)) {
        return;
    }
    if (editor.floating.active()) {
        // Keyboard keys only. ImGui names the mouse buttons and the wheel as
        // keys too, and counting them dropped a paste at the very press that
        // was meant to drag it, and a float at every turn of the wheel.
        bool other = false;
        for (int key = ImGuiKey_Keyboard_BEGIN; key < ImGuiKey_Keyboard_END; ++key) {
            const ImGuiKey named = static_cast<ImGuiKey>(key);
            if (ImGui::IsKeyPressed(named, false) &&
                !(named >= ImGuiKey_LeftCtrl && named <= ImGuiKey_RightSuper)) {
                other = true;
            }
        }
        if (other) {
            // Undo while a move is up takes the move back, rather than
            // dropping it and then undoing something older.
            if (commandPressed(editor.keys, "edit.undo")) {
                cancelFloating(editor);
                return;
            }
            settleFloating(editor);
        }
    }

    // Every shortcut is a named command in the keymap (app/keymap.h), which
    // the person can rebind in Preferences. A chord fires only with exactly
    // its modifiers, so Shift+] is the brush and ] alone is the zoom.
    const Keymap& keys = editor.keys;
    const auto fired = [&keys](const char* id) { return commandPressed(keys, id); };
    const auto pick = [&editor](Tool tool) {
        if (tool == Tool::Picker && editor.tool != Tool::Picker) {
            editor.toolBeforePicker = editor.tool;
        }
        editor.tool = tool;
    };

    // Tools.
    const struct { const char* id; Tool tool; } tools[] = {
        { "tool.pencil", Tool::Pencil }, { "tool.spray", Tool::Spray },
        { "tool.eraser", Tool::Eraser }, { "tool.bucket", Tool::Bucket },
        { "tool.gradient", Tool::Gradient }, { "tool.picker", Tool::Picker },
        { "tool.rectangle", Tool::Rectangle }, { "tool.ellipse", Tool::Ellipse },
        { "tool.line", Tool::Line }, { "tool.contour", Tool::Contour },
        { "tool.text", Tool::Text }, { "tool.select", Tool::Select },
        { "tool.select-ellipse", Tool::SelectEllipse }, { "tool.lasso", Tool::Lasso },
        { "tool.polygon-lasso", Tool::PolygonLasso }, { "tool.wand", Tool::Wand },
        { "tool.polygon", Tool::Polygon }, { "tool.curve", Tool::Curve },
        { "tool.slice", Tool::Slice },
        { "tool.move", Tool::Move }, { "tool.hand", Tool::Hand }, { "tool.zoom", Tool::Zoom },
    };
    for (const auto& entry : tools) {
        if (fired(entry.id)) {
            pick(entry.tool);
        }
    }

    // The brush and the colours.
    if (fired("brush.bigger")) {
        editor.brush.size = std::min(kMaxBrushSize, editor.brush.size + 1);
        editor.say("Brush " + std::to_string(editor.brush.size));
    }
    if (fired("brush.smaller")) {
        editor.brush.size = std::max(1, editor.brush.size - 1);
        editor.say("Brush " + std::to_string(editor.brush.size));
    }
    if (fired("colour.swap")) { swapInks(editor); }

    // Files.
    if (fired("file.new")) { editor.newDocumentOpen = true; }
    if (fired("file.open")) { requestAction(editor, canvas, window, PendingAction::OpenDialog); }
    if (fired("file.save")) { saveOrAsk(editor, canvas, window); }
    if (fired("file.save-as")) { showSaveAsDialog(editor.files, window, editor.doc); }
    if (fired("file.library")) {
        editor.libraryOpen = !editor.libraryOpen;
        editor.libraryStale = editor.libraryStale || editor.libraryOpen;
    }
    if (fired("file.quit")) { requestAction(editor, canvas, window, PendingAction::Quit); }
    if (fired("file.close")) { requestAction(editor, canvas, window, PendingAction::CloseTab); }
    if (tabCount(editor) > 1) {
        const size_t count = tabCount(editor);
        if (fired("view.next-tab")) {
            switchToTab(editor, canvas, (editor.activeTab + 1) % count);
        }
        if (fired("view.previous-tab")) {
            switchToTab(editor, canvas, (editor.activeTab + count - 1) % count);
        }
    }

    // Editing.
    const bool undo = fired("edit.undo");
    const bool redo = fired("edit.redo");
    if ((undo && editor.doc.undo()) || (redo && editor.doc.redo())) {
        resyncLayers(editor);
        resyncReferences(editor, canvas);
        refreshInks(editor);
        canvas.invalidate();
    }
    if (fired("edit.cut")) { cutSelectionPixels(editor); }
    if (fired("edit.copy")) { copyCommand(editor); }
    if (fired("edit.paste")) { pasteCommand(editor, canvas, false); }
    if (fired("edit.paste-layer")) { pasteCommand(editor, canvas, true); }
    if (fired("edit.brush")) { brushFromSelection(editor); }
    if (fired("edit.fill")) { fillSelection(editor, false); }
    if (fired("edit.stroke")) { fillSelection(editor, true); }
    if (fired("selection.flip-h")) { turnSelection(editor, FloatTurn::FlipHorizontal); }
    if (fired("selection.flip-v")) { turnSelection(editor, FloatTurn::FlipVertical); }
    if (fired("select.all")) { selectAll(editor); }
    if (fired("select.none")) {
        deselect(editor);
        editor.say("Deselected");
    }
    if (fired("select.invert")) { invertSelection(editor); }

    // The stack.
    if (fired("layer.duplicate")) { duplicateActiveLayer(editor, canvas); }
    if (fired("layer.merge")) { mergeActiveLayerDown(editor, canvas); }
    if (fired("layer.group")) { groupSelectedLayers(editor, canvas); }
    if (fired("layer.ungroup")) { ungroupActiveLayer(editor, canvas); }
    if (fired("layer.raise")) { raiseActiveLayer(editor, canvas); }
    if (fired("layer.lower")) { lowerActiveLayer(editor, canvas); }

    // The swap: the next palette, wrapping. One key for the thing the engine
    // exists for.
    if (fired("palette.swap")) {
        swapPalette(editor, canvas, nextPalette(editor.doc, documentPalette(editor.doc)));
    }

    // Animation. Enter plays, as in Aseprite; Space is the hand.
    if (fired("anim.play")) {
        editor.timeline.playing = !editor.timeline.playing;
        editor.timeline.startedAtMs = SDL_GetTicks();
        editor.timeline.visible = true;
    }
    if (fired("anim.previous")) {
        editor.timeline.playing = false;
        selectFrame(editor, editor.timeline.activeFrame - 1);
    }
    if (fired("anim.next")) {
        editor.timeline.playing = false;
        selectFrame(editor, editor.timeline.activeFrame + 1);
    }
    if (fired("anim.onion")) { editor.timeline.onion = !editor.timeline.onion; }
    if (fired("anim.duplicate")) {
        const int at = duplicateFrame(editor.doc, editor.timeline.activeFrame);
        if (at >= 0) {
            resyncFrames(editor);
            selectFrame(editor, at);
            editor.timeline.visible = true;
            editor.say("Duplicated frame");
        }
    }

    // The view.
    if (fired("view.timeline")) { editor.timeline.visible = !editor.timeline.visible; }
    if (fired("view.preview")) { editor.preview.visible = !editor.preview.visible; }
    if (fired("view.zoom-in")) { canvas.setZoom(canvas.zoom() + 1.f); }
    if (fired("view.zoom-out")) { canvas.setZoom(canvas.zoom() - 1.f); }
    if (fired("view.fit")) { canvas.requestFit(); }
    if (fired("view.snap")) {
        editor.snapToGrid = !editor.snapToGrid;
        // Snapping to a grid nobody can see is a puzzle, so it shows.
        if (editor.snapToGrid) {
            canvas.tileGrid().visible = true;
        }
        editor.say(editor.snapToGrid ? "Snapping to the tile grid" : "Not snapping");
    }
}

// ----------------------------------------------------------------- layout --

void drawWindow(Editor& editor, CanvasView& canvas, SDL_Window* window) {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const theme::Metrics& m = theme::metrics();

    const float statusHeight = ImGui::GetFrameHeight() + 10.f;
    const float toolbarWidth = 48.f;
    const float bodyHeight = viewport->WorkSize.y - statusHeight;
    const float left = viewport->WorkPos.x;
    const float top = viewport->WorkPos.y;

    constexpr ImGuiWindowFlags kPanel =
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus;

    // The toolbar: a narrow strip, no title, so the tools read as a group
    // rather than as the contents of a panel.
    ImGui::SetNextWindowPos({left, top});
    ImGui::SetNextWindowSize({toolbarWidth, bodyHeight});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(9.f, 9.f));
    // Tools sit close together: they are one group, and the default spacing
    // makes four buttons read as four unrelated things.
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.f, 4.f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, theme::palette().windowBackground);
    ImGui::Begin("##toolbar", nullptr, kPanel | ImGuiWindowFlags_NoTitleBar);
    drawToolbar(editor);
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);

    // The left column: the tool's options above, the palette below, each
    // scrolling on its own. The palette is reached for more often than any
    // tool option, so it is never the thing pushed off the bottom.
    const float toolShare = 0.52f;
    ImGui::SetNextWindowPos({left + toolbarWidth, top});
    ImGui::SetNextWindowSize({m.sidebarWidth, bodyHeight * toolShare});
    ImGui::Begin((std::string(tr("Tool")) + "###tool").c_str(), nullptr, kPanel);
    drawToolPanel(editor);
    ImGui::End();

    ImGui::SetNextWindowPos({left + toolbarWidth, top + bodyHeight * toolShare});
    ImGui::SetNextWindowSize({m.sidebarWidth, bodyHeight * (1.f - toolShare)});
    ImGui::Begin((std::string(tr("Palette")) + "###palette").c_str(), nullptr, kPanel);
    drawPalettePanel(editor, canvas, window);
    ImGui::End();

    const float rightX = left + viewport->WorkSize.x - m.sidebarWidth;
    // The right column. The element panel carries a layer's colours, its
    // shapes, its dither and its outline -- the most numerous controls of the
    // four -- so it takes the largest share of what is left after the stack.
    const float layersShare = 0.28f;
    const float shapeShare  = 0.40f;
    const float transformShare = 0.14f;
    ImGui::SetNextWindowPos({rightX, top});
    ImGui::SetNextWindowSize({m.sidebarWidth, bodyHeight * layersShare});
    ImGui::Begin((std::string(tr("Layers")) + "###layers").c_str(), nullptr, kPanel);
    drawLayerPanel(editor, canvas);
    ImGui::End();

    ImGui::SetNextWindowPos({rightX, top + bodyHeight * layersShare});
    ImGui::SetNextWindowSize({m.sidebarWidth, bodyHeight * shapeShare});
    ImGui::Begin((std::string(tr("Element")) + "###element").c_str(), nullptr, kPanel);
    drawShapePanel(editor, canvas);
    ImGui::End();

    ImGui::SetNextWindowPos({rightX, top + bodyHeight * (layersShare + shapeShare)});
    ImGui::SetNextWindowSize({m.sidebarWidth, bodyHeight * transformShare});
    ImGui::Begin((std::string(tr("Transform")) + "###transform").c_str(), nullptr, kPanel);
    drawTransformPanel(editor, canvas);
    ImGui::End();

    // References sit with the other things the document is made of rather
    // than with the tools: an imported picture is content, even though it is
    // content for the person rather than for the sprite.
    ImGui::SetNextWindowPos({rightX,
                             top + bodyHeight * (layersShare + shapeShare + transformShare)});
    ImGui::SetNextWindowSize({m.sidebarWidth,
                              bodyHeight * (1.f - layersShare - shapeShare - transformShare)});
    ImGui::Begin((std::string(tr("References")) + "###references").c_str(), nullptr, kPanel);
    drawReferencePanel(editor, canvas, window);
    ImGui::End();

    const float canvasX = left + toolbarWidth + m.sidebarWidth;
    const float canvasWidth = viewport->WorkSize.x - toolbarWidth - m.sidebarWidth * 2.f;
    // The strip takes its height out of the canvas rather than overlapping it:
    // an animator wants to see the frame and the strip at the same time, and a
    // timeline floating over the artwork hides the thing it is describing.
    const float timelineHeight = timelinePanelHeight(editor);
    ImGui::SetNextWindowPos({canvasX, top});
    ImGui::SetNextWindowSize({canvasWidth, bodyHeight - timelineHeight});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.f, 0.f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, theme::palette().canvasBackground);
    ImGui::Begin("##canvas", nullptr,
                 kPanel | ImGuiWindowFlags_NoTitleBar |
                 ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    drawTabBar(editor, canvas, window);
    ls::Vec2i hovered { -1, -1 };
    // While playing, the canvas shows the frame the clock says rather than the
    // frame being edited. Selection does not move with it -- stopping is what
    // changes which frame the tools act on.
    canvas.setHoverSize(editor.tool == Tool::Spray ? editor.sprayRadius * 2 + 1
                        : (editor.tool == Tool::Pencil || editor.tool == Tool::Eraser)
                            ? (editor.pen.down ? pressuredSize(editor.brush, editor.pen.pressure)
                                               : editor.brush.size)
                            : 1);
    canvas.setPanWithPrimary(editor.tool == Tool::Hand);
    canvas.setZoomOnClick(editor.tool == Tool::Zoom);
    const int showing = frameToShow(editor, SDL_GetTicks());
    const ls::SpriteId onScreen =
        (showing >= 0 && showing < static_cast<int>(editor.frames.size()))
            ? editor.frames[static_cast<size_t>(showing)].sprite
            : editor.sprite;
    const bool overCanvas = canvas.draw(
        editor.doc, onScreen, &hovered,
        [&editor, &canvas](ImDrawList* draw, ImVec2 origin, float zoom) {
            drawReferences(editor, canvas, draw, origin, zoom, true);
            drawOnionSkin(editor, canvas, draw, origin, zoom);
        },
        [&editor, &canvas](ImDrawList* draw, ImVec2 origin, float zoom) {
            drawReferences(editor, canvas, draw, origin, zoom, false);
            drawSelectionOverlay(editor, draw, origin, zoom);
            drawSymmetryAxes(editor, canvas, draw, origin, zoom);
            drawShapeOverlay(editor, canvas, draw, origin, zoom);
            drawFreeScaleOverlay(editor, canvas, draw, origin, zoom);
            drawSliceOverlay(editor, canvas, draw, origin, zoom);
            drawGuides(editor, canvas, draw, origin, zoom);
            if (editor.stroking && editor.brush.stabiliser > 0) {
                const ls::Vec2f a = editor.stabiliser.at();
                const ls::Vec2f b = canvas.pointerExact();
                draw->AddLine(ImVec2(origin.x + a.x * zoom, origin.y + a.y * zoom),
                              ImVec2(origin.x + b.x * zoom, origin.y + b.y * zoom),
                              IM_COL32(255, 255, 255, 120), 1.f);
            }
            if (editor.drawingGradient) {
                const ls::Vec2f a = editor.gradientSettings.gradientStart;
                const ls::Vec2f b = editor.gradientSettings.gradientEnd;
                const ImVec2 from(origin.x + a.x * zoom, origin.y + a.y * zoom);
                const ImVec2 to(origin.x + b.x * zoom, origin.y + b.y * zoom);
                draw->AddLine(from, to, IM_COL32(0, 0, 0, 200), 3.f);
                draw->AddLine(from, to, IM_COL32(255, 255, 255, 230), 1.f);
                draw->AddCircleFilled(from, 4.f, IM_COL32(255, 255, 255, 230));
                draw->AddCircle(to, 4.f, IM_COL32(255, 255, 255, 230));
            }
            if (editor.drawingContour && editor.contourPoints.size() > 1) {
                const ImU32 ink = ImGui::GetColorU32(ImVec4(editor.color[0], editor.color[1],
                                                            editor.color[2], 1.f));
                const auto centre = [&](ls::Vec2i p) {
                    return ImVec2(origin.x + (static_cast<float>(p.x) + 0.5f) * zoom,
                                  origin.y + (static_cast<float>(p.y) + 0.5f) * zoom);
                };
                for (size_t i = 1; i < editor.contourPoints.size(); ++i) {
                    draw->AddLine(centre(editor.contourPoints[i - 1]),
                                  centre(editor.contourPoints[i]), ink, 2.f);
                }
                draw->AddLine(centre(editor.contourPoints.back()),
                              centre(editor.contourPoints.front()), ink, 1.f);
            }
        });
    drawRulers(editor, canvas);
    editor.hovered = hovered;

    // The preview goes on top of the canvas and takes its clicks first, so a
    // stroke is not started by someone reaching for a backdrop swatch.
    drawPreviewOverlay(editor, canvas);
    // A press on the canvas makes the window's own move ID the active one for
    // the whole drag -- ImGui does that even for a window that cannot move --
    // so only an item other than the window itself being held means the
    // pointer belongs to something else. Counting the window's own ID stopped
    // every drag after its first frame: a click painted, a drag did not.
    const bool itemHeld = ImGui::IsAnyItemActive() &&
                          ImGui::GetActiveID() != ImGui::GetCurrentWindow()->MoveId;
    const bool overPreview = ImGui::IsAnyItemHovered() || itemHeld;

    // Drawing is refused while playing rather than silently landing on a frame
    // the person is not looking at.
    handleStroke(editor, canvas,
                 overCanvas && !overPreview && !editor.timeline.playing, hovered);
    // For the next frame's question: was a click there on the artwork, or
    // somewhere that should drop a float first?
    editor.canvasHovered = overCanvas && !overPreview;
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();

    if (editor.timeline.visible) {
        ImGui::SetNextWindowPos({canvasX, top + bodyHeight - timelineHeight});
        ImGui::SetNextWindowSize({canvasWidth, timelineHeight});
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.f, 6.f));
        ImGui::Begin("##timeline", nullptr, kPanel | ImGuiWindowFlags_NoTitleBar);
        drawTimelinePanel(editor, canvas);
        ImGui::End();
        ImGui::PopStyleVar();
    }

    ImGui::SetNextWindowPos({left, top + bodyHeight});
    ImGui::SetNextWindowSize({viewport->WorkSize.x, statusHeight});
    ImGui::PushStyleColor(ImGuiCol_WindowBg, theme::palette().windowBackground);
    ImGui::Begin("##status", nullptr, kPanel | ImGuiWindowFlags_NoTitleBar);
    drawStatusBar(editor, canvas);
    ImGui::End();
    ImGui::PopStyleColor();

    drawSheetPanel(editor, window);
    drawAnimationPanel(editor, window);
    drawCanvasSizePanel(editor, canvas);
    drawSpriteSizePanel(editor, canvas);
    drawModifyPanel(editor);
    drawAdjustPanel(editor, canvas);
    drawNewDocumentPanel(editor, canvas, window);
    drawTextPanel(editor, canvas);
    drawHistoryPanel(editor, canvas);
    drawPreferencesPanel(editor, canvas);
    drawLayerPropertiesPanel(editor, canvas);
    drawSlicesPanel(editor, canvas);
    drawSheetImportPanel(editor, canvas, window);
    drawLibraryPanel(editor, canvas, window);
    drawRecoveryPrompt(editor, canvas);
    drawUnsavedPrompt(editor, canvas, window);
}

// ------------------------------------------------------- running headless --
//
// A GUI is the part of a program usually tested by someone looking at it. These
// flags make it testable instead: --frames runs a fixed number and exits, --shot
// writes what was drawn, and --demo-stroke draws through the same calls the
// tools use. Under SDL's dummy video driver that runs with no display at all,
// which is what lets CI notice an editor that no longer starts.

struct Options {
    int         frames = 0;          // 0 = run until the user quits
    std::string screenshot;
    bool        demoStroke = false;
    bool        expectIdle = false;
    std::string sheetPath;
    bool        showSheetPanel = false;
    std::string select;                // --select x,y,w,h: a marquee, for a capture
    std::string tool;                  // --tool name: the tool in hand, for a capture
    bool        tiled = false;         // --tiled: tiled mode both ways and a tile grid
    bool        isometric = false;     // --isometric: an isometric tile grid
    bool        light = false;         // --light: the light theme, for captures
    bool        symmetry = false;      // --symmetry: both axes on
    bool        play = false;            // start playback, for a headless run
    bool        library = false;         // open the library window
    bool        preferences = false;     // open the preferences window
    bool        preferencesKeys = false; // ... on its Keys tab
    bool        layerProperties = false; // the active layer's properties, tagged
    bool        paths = false;           // --paths: a polygon and a curve, handles showing
    bool        tabs = false;            // --tabs: two more documents open beside the first
    bool        adjust = false;          // --adjust: the colour window, hue turned
    bool        shadow = false;          // --shadow: the figure casts a shadow
    bool        tween = false;           // --tween: a figure turned and moved across five frames
    // --drag X0,Y0,X1,Y1: a left-button drag between two canvas pixels, fed
    // through ImGui's own input queue over several frames -- the path a real
    // mouse takes -- so what a drag does can be captured and checked.
    float       drag[4] = { -1.f, -1.f, -1.f, -1.f };
    std::string script;                  // --script FILE: see ui_script.h
    int         expectDrawn = -1;        // --expect-drawn N: fail unless N pixels are drawn
    int         expectAtMost = -1;       // --expect-at-most N: fail if more than N are
    float       rectangle[4] = { -1.f, -1.f, -1.f, -1.f };  // --rectangle X0,Y0,X1,Y1: a shape to start with
    bool        slices = false;          // --slices: two slices, the window open
    bool        rotsprite = false;       // --rotsprite: one sprite turned two ways, side by side
    std::string language;                // --language CODE: the interface in that language
    float       zoom = 0.f;              // --zoom N: the zoom after the first fit
    // Copy (after --select) or paste through the real system clipboard at
    // start: a headless check of the clipboard both ways. Overwrites the
    // clipboard of whoever runs it, so only when asked by name.
    bool        systemCopy = false;
    bool        systemPaste = false;
    uint32_t    autosaveSeconds = 0;     // override the interval, for testing
    bool        selfTest = false;
    std::string openPath;
};

Options parseOptions(int argc, char** argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--frames" && i + 1 < argc) {
            options.frames = std::atoi(argv[++i]);
        } else if (arg == "--shot" && i + 1 < argc) {
            options.screenshot = argv[++i];
        } else if (arg == "--demo-stroke") {
            options.demoStroke = true;
        } else if (arg == "--sheet" && i + 1 < argc) {
            // Writes a sheet of the demo and exits with the rest of the run.
            // The interesting path is compiling every frame and composing them,
            // and it is worth CI walking it rather than only the unit tests.
            options.sheetPath = argv[++i];
        } else if (arg == "--autosave" && i + 1 < argc) {
            options.autosaveSeconds = static_cast<uint32_t>(std::atoi(argv[++i]));
        } else if (arg == "--library") {
            options.library = true;
        } else if (arg == "--preferences") {
            options.preferences = true;
        } else if (arg == "--zoom" && i + 1 < argc) {
            options.zoom = static_cast<float>(std::atof(argv[++i]));
        } else if (arg == "--language" && i + 1 < argc) {
            options.language = argv[++i];
        } else if (arg == "--rotsprite") {
            options.rotsprite = true;
        } else if (arg == "--slices") {
            options.slices = true;
        } else if (arg == "--shadow") {
            options.shadow = true;
        } else if (arg == "--tween") {
            options.tween = true;
        } else if (arg == "--script" && i + 1 < argc) {
            options.script = argv[++i];
        } else if (arg == "--expect-drawn" && i + 1 < argc) {
            options.expectDrawn = std::atoi(argv[++i]);
        } else if (arg == "--expect-at-most" && i + 1 < argc) {
            options.expectAtMost = std::atoi(argv[++i]);
        } else if (arg == "--rectangle" && i + 1 < argc) {
            const char* text = argv[++i];
            for (float& value : options.rectangle) {
                char* end = nullptr;
                value = std::strtof(text, &end);
                text = (*end == ',') ? end + 1 : end;
            }
        } else if (arg == "--drag" && i + 1 < argc) {
            const char* text = argv[++i];
            for (float& value : options.drag) {
                char* end = nullptr;
                value = std::strtof(text, &end);
                text = (*end == ',') ? end + 1 : end;
            }
        } else if (arg == "--adjust") {
            options.adjust = true;
        } else if (arg == "--tabs") {
            options.tabs = true;
        } else if (arg == "--paths") {
            options.paths = true;
        } else if (arg == "--layer-properties") {
            options.layerProperties = true;
        } else if (arg == "--system-copy") {
            options.systemCopy = true;
        } else if (arg == "--system-paste") {
            options.systemPaste = true;
        } else if (arg == "--preferences-keys") {
            options.preferences = true;
            options.preferencesKeys = true;
        } else if (arg == "--play") {
            options.play = true;
        } else if (arg == "--show-sheet-panel") {
            // Opens the export window so a headless capture can show it. Only
            // useful with --frames and --shot.
            options.showSheetPanel = true;
        } else if (arg == "--select" && i + 1 < argc) {
            // A rectangle selected at start, so a headless capture can show
            // the marching ants and the tool options that go with them.
            options.select = argv[++i];
        } else if (arg == "--tool" && i + 1 < argc) {
            options.tool = argv[++i];
        } else if (arg == "--light") {
            options.light = true;
        } else if (arg == "--isometric") {
            options.isometric = true;
        } else if (arg == "--tiled") {
            options.tiled = true;
        } else if (arg == "--symmetry") {
            options.symmetry = true;
        } else if (arg == "--expect-idle") {
            // Fails the run if the last frame compiled anything. Nothing is
            // changing by then, so a compile means something asked for a
            // picture it already had -- which is the whole performance design
            // quietly coming undone, and is invisible in a screenshot.
            options.expectIdle = true;
        } else if (arg == "--self-test") {
            options.selfTest = true;
        } else if (!arg.empty() && arg[0] != '-') {
            // A bare argument is a file to open, which is how a file manager
            // hands one over when someone double-clicks it.
            options.openPath = arg;
        }
    }
    return options;
}

void drawDemoContent(Editor& editor) {
    PaintLayer* layer = editor.active();
    if (layer == nullptr) {
        return;
    }
    editor.doc.beginAction("Demo");
    paintPixels(editor.doc, *layer, linePixels({4, 4}, {27, 27}));
    paintPixels(editor.doc, *layer, linePixels({27, 4}, {4, 27}));
    for (int32_t y = 12; y < 20; ++y) {
        paintPixels(editor.doc, *layer, linePixels({12, y}, {19, y}));
    }
    editor.doc.endAction();

    // Dithered and turned, because a flat square proves nothing about an engine
    // whose point is that neither of those costs anything.
    DitherSettings dither;
    dither.pattern = ls::DitherPatternKind::Bayer4;
    dither.from = ls::Color{40, 50, 110, 255};
    dither.to = ls::Color{240, 170, 90, 255};
    dither.modulation = ls::DitherModulation::Linear;
    dither.gradientStart = {6.f, 6.f};
    dither.gradientEnd = {26.f, 26.f};
    dither.anchor = ls::PatternAnchor::Local;
    // The two ends follow palette slots rather than holding literal colours,
    // so the dither recolours with the palette like everything else. That is
    // the promise the palette makes, and until ramps could name a role it was
    // false for exactly the kind of layer pixel art is mostly made of.
    ensurePalette(editor.doc, editor.sprite);
    dither.fromRole = addPaletteEntry(editor.doc, editor.sprite, dither.from);
    dither.toRole = addPaletteEntry(editor.doc, editor.sprite, dither.to);
    setPaletteLabel(editor.doc, dither.fromRole, "shade");
    setPaletteLabel(editor.doc, dither.toRole, "light");
    setLayerDithered(editor.doc, *layer, dither);
    editor.dither = dither;

    // Two shapes on the figure's own layer, so the Shape panel has an element
    // list to show and the belt is one layer, not three.
    {
        ShapeParams belt;
        belt.from = { 10.f, 22.f };
        belt.to = { 22.f, 24.f };
        ShapeLayer made;
        addShapeElement(editor.doc, layer->layer, ShapeKind::Rectangle, belt, &made);
        ShapeParams strap;
        strap.from = { 12.f, 20.f };
        strap.to = { 20.f, 26.f };
        addShapeElement(editor.doc, layer->layer, ShapeKind::Line, strap, &made);
    }

    editor.doc.beginAction("Demo rotate");
    addRotate(editor.doc, layer->layer, 24.f, {16.f, 16.f});
    editor.doc.endAction();
    // The demo shows the preview over a colour rather than the chequer, since
    // checking a sprite against a background it will really be seen on is the
    // reason the panel exists.
    editor.preview.transparent = false;
    editor.preview.color[0] = 0.36f;
    editor.preview.color[1] = 0.55f;
    editor.preview.color[2] = 0.78f;
    editor.preview.scale = 2;

    // A second layer, and one line round both of them.
    //
    // This is the outline worth showing, because it is the one a conventional
    // editor cannot draw at all: the line belongs to the figure rather than to
    // a layer, so there is no seam where the two parts meet, and it follows
    // whichever part moves.
    // A copy, not the pointer: pushing onto editor.layers below can reallocate
    // the vector `layer` points into, and everything after that would be
    // writing through a dangling pointer.
    const PaintLayer first = *layer;
    layer = nullptr;

    PaintLayer second;
    if (createPaintLayer(editor.doc, editor.sprite, "Layer 2",
                         ls::Color{120, 200, 255, 255}, &second)) {
        editor.doc.beginAction("Demo second part");
        paintPixels(editor.doc, second, linePixels({18, 10}, {26, 10}));
        paintPixels(editor.doc, second, linePixels({18, 11}, {26, 11}));
        paintPixels(editor.doc, second, linePixels({18, 12}, {26, 12}));
        // And a second colour on the same layer, through a palette slot: a
        // layer holds as many colours as it is painted with.
        Ink light;
        light.colour = dither.to;
        light.role = dither.toRole;
        InkStroke stroke;
        if (beginInkStroke(editor.doc, second.layer, light, &stroke)) {
            strokeInk(editor.doc, stroke, linePixels({19, 11}, {25, 11}));
        }
        editor.doc.endAction();
        editor.layers.push_back(second);

        // On the first layer rather than the second, to make the point: the
        // line belongs to the figure, not to the layer it happens to live on.
        OutlineSettings figure;
        figure.scope = OutlineScope::Sprite;
        figure.colour = ls::Color{12, 14, 20, 255};
        editor.doc.beginAction("Demo outline");
        setOutline(editor.doc, first, figure);
        editor.doc.endAction();
    }

    // Six frames of the same drawing at different angles.
    //
    // This is the demo worth having, because in any editor that resamples it
    // would be six degraded copies -- each frame stamped from the last, softer
    // than the one before. Here every frame owns the same authored pixels and
    // one rotation parameter, so frame six is exactly as sharp as frame one and
    // the file is not six times larger.
    resyncFrames(editor);
    for (int i = 1; i < 6; ++i) {
        const int at = duplicateFrame(editor.doc, i - 1);
        if (at < 0) {
            break;
        }
        resyncFrames(editor);
        selectFrame(editor, at);
        PaintLayer* frameLayer = editor.active();
        if (frameLayer == nullptr) {
            continue;
        }
        const std::vector<TransformEntry> stack =
            listTransforms(editor.doc, frameLayer->layer);
        if (!stack.empty()) {
            editor.doc.beginAction("Demo angle");
            setRotateAngle(editor.doc, stack.front().id,
                           24.f + static_cast<float>(i) * 12.f);
            editor.doc.endAction();
        }
        setFrameDuration(editor.doc, at, 80 + i * 10);
    }
    resyncFrames(editor);

    // A cycle that is not simply "every frame", because that is the case the
    // step row exists for: it leaves a frame out and plays another twice, which
    // no row of checkboxes over the strip could say.
    const int frameCount = static_cast<int>(editor.frames.size());
    if (addCycle(editor.doc, "swing", frameCount) == 0) {
        resyncFrames(editor);
        // every frame -> 0 1 2 3 4 2
        removeCycleStep(editor.doc, 0, 5, frameCount);
        resyncFrames(editor);
        addCycleStep(editor.doc, 0, 4, 2, frameCount);
        resyncFrames(editor);
        setCycleLoop(editor.doc, 0, LoopMode::PingPong, frameCount);
        resyncFrames(editor);
        selectCycle(editor, 0);
    }

    // The stack has something to show: the highlight at Screen, and a group
    // of the two layers so the panel has a fold to draw.
    {
        const std::vector<ls::LayerId> order = layerOrder(editor.doc, editor.sprite);
        if (order.size() >= 2) {
            setLayerBlend(editor.doc, order.back(), ls::BlendMode::Screen);
            setLayerOpacity(editor.doc, order.back(), 0.8f);
            groupLayers(editor.doc, { order[0], order[1] }, "figure");
        }
    }

    // A reference to draw from. Generated rather than read from disk so the
    // demo needs no files beside it, but it goes in through exactly the call
    // an imported photograph does.
    {
        ls::RasterBuffer picture = ls::makeRaster(24, 24);
        for (uint32_t y = 0; y < picture.height; ++y) {
            for (uint32_t x = 0; x < picture.width; ++x) {
                uint8_t* pixel = picture.row(y) + static_cast<size_t>(x) * 4u;
                const bool ring = (x + y) % 7 < 2;
                pixel[0] = static_cast<uint8_t>(90 + x * 6);
                pixel[1] = static_cast<uint8_t>(70 + y * 5);
                pixel[2] = 160;
                pixel[3] = ring ? 200 : 60;
            }
        }
        std::vector<uint8_t> png;
        std::string error;
        Reference made;
        if (encodeImageAsPng(picture, &png, &error) &&
            addReference(editor.doc, "study.png", png, &made, &error)) {
            made.opacity = 0.4f;
            made.behind = true;
            updateReference(editor.doc, made);
            editor.activeReference = made.id;
        }
    }

    // A second palette, the same as the first with the dither's two slots
    // turned to night, and a flash frame bound to a third. The quick row has
    // something to swap between, and the strip shows one frame sitting it out.
    {
        const ls::PaletteId day = documentPalette(editor.doc);
        renamePalette(editor.doc, day, "day");
        const ls::PaletteId night = addPalette(editor.doc, "night", day);
        if (night.valid()) {
            setPaletteEntry(editor.doc, night, dither.fromRole, ls::Color{20, 22, 48, 255});
            setPaletteEntry(editor.doc, night, dither.toRole, ls::Color{120, 130, 210, 255});
        }
        const ls::PaletteId flash = addPalette(editor.doc, "flash", day);
        const std::vector<Frame> frames = readFrames(editor.doc);
        if (flash.valid() && frames.size() > 3) {
            setPaletteEntry(editor.doc, flash, dither.fromRole, ls::Color{255, 255, 255, 255});
            setPaletteEntry(editor.doc, flash, dither.toRole, ls::Color{255, 255, 255, 255});
            bindFrame(editor.doc, frames[3].sprite, flash);
        }
    }

    selectFrame(editor, 0);
    editor.timeline.visible = true;
    editor.timeline.onion = true;

    editor.say("Demo content");
}

// The state management the interface does, driven without the interface.
// resyncLayers and the undo/open glue around it are not reachable from a
// fast_core test, because they are the window's own bookkeeping -- and all of
// it was wrong once.
int runSelfTest() {
    int failures = 0;
    const auto check = [&failures](bool condition, const char* what) {
        if (!condition) {
            std::printf("FAIL %s\n", what);
            ++failures;
        }
    };

    Editor editor;
    check(newDocument(editor, 16), "new document");
    check(editor.layers.size() == 1, "one layer to start");
    check(!paletteEntries(editor.doc).empty(), "a new document has a palette");

    editor.doc.beginAction("Pencil");
    paintPixels(editor.doc, *editor.active(), linePixels({2, 2}, {2, 9}));
    editor.doc.endAction();

    // Add a layer, then undo it. The panel must not be left holding a layer
    // that no longer exists -- this was the bug.
    PaintLayer added;
    check(createPaintLayer(editor.doc, editor.sprite, "Layer 2",
                           ls::Color{40, 80, 220, 255}, &added), "add layer");
    editor.layers.push_back(added);
    check(editor.layers.size() == 2, "two layers");

    check(editor.doc.undo(), "undo the added layer");
    resyncLayers(editor);
    check(editor.layers.size() == 1, "panel drops the undone layer");
    check(editor.activeLayer == 0, "active index stays in range");
    check(editor.active() != nullptr, "still something to draw on");

    check(editor.doc.canRedo(), "the undone layer can be redone");
    check(editor.doc.redo(), "redo");
    resyncLayers(editor);
    check(editor.layers.size() == 2, "the redone layer reappears");

    check(editor.doc.undo(), "undo it again");
    resyncLayers(editor);
    check(editor.layers.size() == 1, "and goes away again");

    // Drawing after an undo discards the redo branch, as every editor does.
    editor.doc.beginAction("Pencil");
    check(paintPixels(editor.doc, *editor.active(), {{7, 7}}), "draw after undo");
    editor.doc.endAction();
    check(!editor.doc.canRedo(), "a new action drops the redo branch");

    // The palette drives the layer, and an ink through the same slot reads
    // it back: the colour control shows what the slot now means.
    const ls::ColorRole role = 8;
    check(setLayerRole(editor.doc, *editor.active(), role), "use a palette slot");
    check(setPaletteEntry(editor.doc, role, ls::Color{12, 200, 90, 255}), "set the slot");
    editor.inkRole = role;
    refreshInks(editor);
    check(toColor(editor.color).g == 200, "the control shows the palette colour");
    editor.inkRole = ls::kColorRoleNone;

    check(setPaintColor(editor.doc, *editor.active(),
                        effectiveLayerColor(editor.doc, editor.sprite, *editor.active())),
          "keep what is on screen");
    check(setLayerRole(editor.doc, *editor.active(), ls::kColorRoleNone), "detach");
    check(layerRole(editor.doc, *editor.active()) == ls::kColorRoleNone, "detached");

    // ------------------------------------------------------------- inks --
    //
    // Two colours on one layer through the window's own path: the left ink,
    // then the right one, into the same layer, each its own element; X swaps
    // them; and a dithered element is painted into only when asked.
    {
        PaintLayer* layer = editor.active();
        check(layer != nullptr, "a layer for two inks");
        if (layer != nullptr) {
            const ls::LayerId target = layer->layer;
            const size_t before = elementsOf(editor.doc, target).size();
            setForegroundInk(editor, Ink{ ls::Color{ 250, 10, 10, 255 }, ls::kColorRoleNone });
            setBackgroundInk(editor, Ink{ ls::Color{ 10, 10, 250, 255 }, ls::kColorRoleNone });

            InkStroke stroke;
            editor.doc.beginAction("Pencil");
            check(beginPaint(editor, *layer, false, &stroke), "the left ink begins");
            check(strokeInk(editor.doc, stroke, {{ 20, 20 }}), "and paints");
            editor.doc.endAction();
            editor.doc.beginAction("Pencil");
            check(beginPaint(editor, *layer, true, &stroke), "the right ink begins");
            check(strokeInk(editor.doc, stroke, {{ 21, 20 }}), "and paints");
            editor.doc.endAction();
            resyncLayers(editor);

            check(elementsOf(editor.doc, target).size() == before + 2,
                  "two colours, two elements, one layer");
            check(elementsWithInk(editor.doc, target, foregroundInk(editor)).size() == 1,
                  "the left colour has its element");
            swapInks(editor);
            check(toColor(editor.color).b == 250 && toColor(editor.backColor).r == 250,
                  "X swaps the two");
            swapInks(editor);

            check(editor.doc.undo() && editor.doc.undo(), "two strokes, two undos");
            resyncLayers(editor);
            check(elementsOf(editor.doc, target).size() == before,
                  "and both colours' elements go with them");
        }
    }

    // -------------------------------------------------------- selection --
    //
    // The commands the menu, the tools and the keys share: a move is one
    // undo step however it was nudged; Escape puts it back; a shortcut drops
    // it; copy and paste carry pixels, not a layer, while something is
    // selected; select-all and invert are about the canvas.
    {
        PaintLayer* layer = editor.active();
        check(layer != nullptr, "a layer to select on");
        if (layer != nullptr) {
            const ls::LayerId target = layer->layer;
            setForegroundInk(editor, Ink{ ls::Color{ 9, 200, 9, 255 }, ls::kColorRoleNone });
            InkStroke stroke;
            editor.doc.beginAction("Pencil");
            check(beginPaint(editor, *layer, false, &stroke) &&
                  strokeInk(editor.doc, stroke, {{ 2, 2 }, { 3, 2 }}), "two pixels to move");
            editor.doc.endAction();
            resyncLayers(editor);
            const std::string before = editor.doc.undoLabel();

            editor.selection.mask = rectangleMask({ 2, 2 }, { 3, 2 });
            check(nudgeSelection(editor, { 1, 0 }), "nudge lifts and moves");
            check(nudgeSelection(editor, { 1, 0 }), "and again");
            check(editor.floating.active() && editor.floating.offset.x == 2,
                  "two nudges, one float, two pixels along");
            check(editor.selection.contains({ 4, 2 }) && !editor.selection.contains({ 2, 2 }),
                  "the ants follow");
            settleFloating(editor);
            check(!editor.floating.active(), "dropped");
            check(editor.doc.undoLabel() == "Move", "a run of nudges is one Move");
            check(editor.doc.undo(), "and one undo takes it back");
            resyncLayers(editor);
            check(editor.doc.undoLabel() == before, "to exactly before the move");

            // Escape: nothing happened.
            editor.selection.mask = rectangleMask({ 2, 2 }, { 3, 2 });
            check(nudgeSelection(editor, { 0, 3 }), "lift for escape");
            cancelFloating(editor);
            check(!editor.floating.active() && editor.doc.undoLabel() == before,
                  "Escape leaves no history entry");
            check(editor.selection.contains({ 2, 2 }), "and the selection is where it was");

            // A brush from the selection: the pixels, colour by colour, and
            // the pencil in hand to stamp them.
            editor.tool = Tool::Select;
            check(brushFromSelection(editor), "a brush from the selected pixels");
            check(editor.tool == Tool::Pencil && editor.customBrushOn &&
                  editor.customBrush.pieces.size() == 1 &&
                  ls::geom::pixelCount(editor.customBrush.mask) == 2, "two pixels, one colour");
            editor.customBrushOn = false;

            // Copy and paste carry pixels while something is selected.
            check(copySelectionPixels(editor) && editor.clipHoldsPixels, "copy pixels");
            check(pastePixels(editor) && editor.floating.active(), "paste floats them");
            check(editor.tool == Tool::Move, "with the move tool in hand");
            check(nudgeSelection(editor, { 0, 5 }), "place the paste");
            settleFloating(editor);
            check(editor.doc.undoLabel() == "Paste", "a paste is one Paste");
            check(editor.doc.undo(), "undo the paste");
            resyncLayers(editor);

            // Delete clears; select-all and invert are canvas-wide.
            editor.selection.mask = rectangleMask({ 2, 2 }, { 3, 2 });
            check(deleteSelectionPixels(editor), "delete");
            check(editor.doc.undoLabel() == "Delete", "one Delete");
            check(editor.doc.undo(), "undo the delete");
            resyncLayers(editor);
            selectAll(editor);
            check(ls::geom::pixelCount(editor.selection.mask) ==
                  ls::geom::pixelCount(canvasBounds(editor)), "select all is the canvas");
            invertSelection(editor);
            check(editor.selection.empty(), "the inverse of everything is nothing");
            editor.selection.mask = rectangleMask({ 0, 0 }, { 1, 1 });
            deselect(editor);
            check(editor.selection.empty(), "deselect");
            reselect(editor);
            check(ls::geom::pixelCount(editor.selection.mask) == 4, "reselect brings it back");
            deselect(editor);

            // A locked layer refuses to lift.
            setLayerLocked(editor.doc, target, true);
            editor.selection.mask = rectangleMask({ 2, 2 }, { 3, 2 });
            check(!nudgeSelection(editor, { 1, 0 }) && !editor.floating.active(),
                  "a locked layer's pixels stay put");
            setLayerLocked(editor.doc, target, false);
            deselect(editor);
            editor.tool = Tool::Pencil;

            check(editor.doc.undo(), "undo the two pixels");
            resyncLayers(editor);
        }
    }

    // ------------------------------------------------------------ frames --
    //
    // The window's own bookkeeping, none of which a fast_core test can reach:
    // that selecting a frame changes which layers the panel holds, and that a
    // tool then acts on the frame being looked at rather than on the first one.

    check(editor.frames.size() == 1, "a new document is one frame");
    check(editor.timeline.activeFrame == 0, "on the first frame");

    check(duplicateFrame(editor.doc, 0) == 1, "duplicate the frame");
    resyncFrames(editor);
    check(editor.frames.size() == 2, "the strip sees two frames");

    selectFrame(editor, 1);
    check(editor.timeline.activeFrame == 1, "the second frame is selected");
    check(editor.activeSprite() == editor.frames[1].sprite,
          "the active sprite is the selected frame");
    check(editor.active() != nullptr, "the second frame has a layer to draw on");

    // The one that matters: paint through the interface's own handles and only
    // the selected frame may change.
    {
        const auto pixelsOf = [&](ls::SpriteId sprite) {
            const ls::CompileProfile profile =
                compileProfile(ls::CompileProfileType::Export, 16, 16);
            auto compiled = editor.doc.engine().compileSprite(sprite, profile);
            return compiled.ok() ? compiled.value.raster.pixels
                                 : std::vector<uint8_t>{};
        };
        const std::vector<uint8_t> firstBefore = pixelsOf(editor.frames[0].sprite);

        editor.doc.beginAction("Pencil");
        check(paintPixels(editor.doc, *editor.active(), linePixels({11, 2}, {11, 13})),
              "draw on the second frame");
        editor.doc.endAction();

        check(pixelsOf(editor.frames[0].sprite) == firstBefore,
              "the first frame did not change");
        check(pixelsOf(editor.frames[1].sprite) != firstBefore,
              "the second frame did");
    }

    // Selecting back must give back the first frame's layers, not the second's.
    selectFrame(editor, 0);
    check(editor.activeSprite() == editor.frames[0].sprite, "back on the first frame");

    // Playing shows the frame the clock says, and does not move the selection.
    editor.timeline.playing = true;
    editor.timeline.startedAtMs = 0;
    check(frameToShow(editor, 0) == 0, "playback starts at the beginning");
    check(frameToShow(editor, static_cast<uint64_t>(kDefaultFrameMs) + 5) == 1,
          "and moves on when the frame's time is up");
    check(editor.timeline.activeFrame == 0, "playing does not change the selection");
    // It is a function of time, so the same point of a later pass is the same
    // picture. Two frames of the default hold, so the cycle is twice that.
    {
        const uint64_t cycle = static_cast<uint64_t>(kDefaultFrameMs) * 2;
        const uint64_t at = static_cast<uint64_t>(kDefaultFrameMs) + 5;
        check(frameToShow(editor, at) == 1, "the second frame at that point");
        check(frameToShow(editor, at + cycle * 10) == frameToShow(editor, at),
              "the same point of a later pass is the same frame");
    }
    editor.timeline.playing = false;

    // An undone frame must not leave the strip holding one that is gone.
    selectFrame(editor, 1);
    check(editor.doc.undo(), "undo the drawing on frame two");
    check(editor.doc.undo(), "undo the frame itself");
    resyncFrames(editor);
    check(editor.frames.size() == 1, "the strip drops the undone frame");
    check(editor.timeline.activeFrame == 0, "and the selection comes back in range");
    resyncLayers(editor);
    check(editor.active() != nullptr, "still something to draw on");

    // The last frame cannot be deleted, so the editor can never reach a state
    // with nothing to draw on.
    check(!deleteFrame(editor.doc, 0), "the last frame stays");

    // -------------------------------------------------- the seams between --
    //
    // Each feature above holds on its own. These are the places where two of
    // them meet in the window's bookkeeping, and each was a way to leave an
    // index pointing at something that had gone.

    // An empty frame is one the pencil can draw on at once.
    check(addEmptyFrame(editor, 0) == 1, "add an empty frame");
    resyncFrames(editor);
    selectFrame(editor, 1);
    check(editor.active() != nullptr, "an empty frame has a layer to draw on");
    if (editor.active() != nullptr) {
        check(paintPixels(editor.doc, *editor.active(), {{3, 3}}),
              "and the pencil reaches it");
        check(layerRole(editor.doc, *editor.active()) == ls::kColorRoleNone,
              "it starts as a plain colour");
    }
    check(editor.doc.undo(), "one undo removes frame and layer together");
    resyncLayers(editor);
    check(editor.frames.size() == 1, "the empty frame is gone");
    check(editor.doc.undo(), "and the next undo is the drawing, not the layer");
    resyncLayers(editor);
    check(editor.frames.size() == 1, "still one frame");
    check(editor.doc.redo(), "redo the drawing");
    resyncLayers(editor);

    // Playback across an undo that removes the frames being played.
    check(duplicateFrame(editor.doc, 0) == 1, "a frame to play");
    check(duplicateFrame(editor.doc, 1) == 2, "and another");
    resyncFrames(editor);
    editor.timeline.playing = true;
    editor.timeline.startedAtMs = 0;
    check(frameToShow(editor, static_cast<uint64_t>(kDefaultFrameMs) * 2 + 5) == 2,
          "playing the third frame");
    check(editor.doc.undo() && editor.doc.undo(), "undo both frames under playback");
    resyncLayers(editor);
    {
        const int shown = frameToShow(editor, static_cast<uint64_t>(kDefaultFrameMs) * 2 + 5);
        check(shown >= 0 && shown < static_cast<int>(editor.frames.size()),
              "playback lands on a frame that exists");
        check(editor.activeSprite() == editor.frames.front().sprite,
              "and the tools draw into one that exists");
    }
    editor.timeline.playing = false;

    // Deleting the frame being edited moves the editor to a frame that exists.
    check(duplicateFrame(editor.doc, 0) == 1, "a frame to delete");
    resyncFrames(editor);
    selectFrame(editor, 1);
    check(deleteFrame(editor.doc, 1), "delete the active frame");
    resyncLayers(editor);
    check(editor.timeline.activeFrame == 0, "the selection moves");
    check(editor.sprite == editor.frames.front().sprite, "the sprite handle follows");
    check(editor.activeSprite() == editor.sprite, "and both agree");
    check(editor.active() != nullptr, "with a layer to draw on");

    // Stepping between frames keeps the layer position when it can. A frame
    // with fewer layers clamps rather than pointing past the end.
    {
        PaintLayer second;
        check(createPaintLayer(editor.doc, editor.sprite, "Layer 2",
                               ls::Color{1, 2, 3, 255}, &second),
              "a second layer on the first frame");
    }
    resyncLayers(editor);
    check(editor.layers.size() == 2, "two layers on the first frame");
    editor.activeLayer = 1;
    check(addEmptyFrame(editor, 0) == 1, "an empty frame with one layer");
    resyncFrames(editor);
    selectFrame(editor, 1);
    check(editor.activeLayer >= 0 &&
          editor.activeLayer < static_cast<int>(editor.layers.size()),
          "the layer index is in range on a frame with fewer layers");
    check(editor.active() != nullptr, "and points at a layer");
    check(deleteFrame(editor.doc, 1), "drop that frame");
    resyncLayers(editor);

    // Loading a palette recolours the layer, and the ink painting through the
    // same slot with it.
    {
        PaintLayer* layer = editor.active();
        check(layer != nullptr, "a layer to colour");
        if (layer != nullptr) {
            editor.doc.beginAction("Use slot 0");
            check(setLayerRole(editor.doc, *layer, 0), "draw through slot 0");
            editor.doc.endAction();
            editor.inkRole = 0;
            PaletteFile file;
            std::string parseError;
            check(parsePalette("123456\n", &file, &parseError), "a one-colour file");
            int dropped = 0;
            check(applyPaletteFile(editor.doc, editor.sprite, file, &dropped),
                  "load it");
            resyncLayers(editor);
            refreshInks(editor);
            const ls::Color shown = toColor(editor.color);
            check(shown.r == 0x12 && shown.g == 0x34 && shown.b == 0x56,
                  "the colour control shows what the layer now draws");
            check(editor.doc.undo(), "undo the load");
            resyncLayers(editor);            // `layer` is stale from here on
            refreshInks(editor);
            editor.doc.beginAction("Detach");
            check(editor.active() != nullptr &&
                  setLayerRole(editor.doc, *editor.active(), ls::kColorRoleNone),
                  "detach");
            editor.doc.endAction();
            editor.inkRole = ls::kColorRoleNone;
        }
    }

    // The swap, through the window's own call: every frame recolours, a frame
    // with a palette of its own does not, and undo is one step.
    {
        CanvasView view(nullptr);
        const ls::PaletteId day = documentPalette(editor.doc);
        const ls::PaletteId night = addPalette(editor.doc, "night", day);
        check(night.valid(), "a second palette");
        PaintLayer* layer = editor.active();
        if (layer != nullptr && night.valid()) {
            editor.doc.beginAction("Use slot 0");
            setLayerRole(editor.doc, *layer, 0);
            editor.doc.endAction();
            editor.inkRole = 0;
            setPaletteEntry(editor.doc, night, 0, ls::Color{7, 8, 9, 255});
            swapPalette(editor, view, night);
            check(documentPalette(editor.doc) == night, "the document swapped");
            const ls::Color shown = toColor(editor.color);
            check(shown.r == 7 && shown.g == 8 && shown.b == 9,
                  "the colour control follows the swap");
            check(editor.doc.undo(), "one undo");
            resyncLayers(editor);
            refreshInks(editor);
            check(documentPalette(editor.doc) == day, "back to the first palette");
            swapPalette(editor, view, day);
            check(!editor.doc.canUndo() || editor.doc.undoLabel() != "Swap palette",
                  "swapping to the palette in use is not an action");
            editor.doc.beginAction("Detach");
            setLayerRole(editor.doc, *editor.active(), ls::kColorRoleNone);
            editor.doc.endAction();
            editor.inkRole = ls::kColorRoleNone;
        }
    }

    // The stack, driven the way the panel and the shortcuts drive it. Each
    // step leaves the active layer pointing at a layer that exists and the
    // selection inside the list.
    {
        CanvasView view(nullptr);
        const size_t base = editor.layers.size();
        const ls::LayerId first = editor.active()->layer;
        const int firstAt = indexOfLayer(editor.doc, editor.sprite, first);
        duplicateActiveLayer(editor, view);
        check(editor.layers.size() == base + 1, "a duplicate");
        const ls::LayerId copy = editor.active()->layer;
        check(copy != first, "and it is the active one");
        check(editor.selectedLayers == std::vector<ls::LayerId>({ copy }),
              "the selection is the copy");
        check(indexOfLayer(editor.doc, editor.sprite, copy) == firstAt + 1,
              "right above the original");

        lowerActiveLayer(editor, view);
        check(indexOfLayer(editor.doc, editor.sprite, copy) == firstAt, "lowered");
        check(editor.active()->layer == copy, "still active after the move");
        raiseActiveLayer(editor, view);
        check(indexOfLayer(editor.doc, editor.sprite, copy) == firstAt + 1, "raised");

        // Copy it, paste it into a new frame.
        copyActiveLayer(editor);
        check(editor.clipboard == copy, "copied");
        check(addEmptyFrame(editor, 0) == 1, "a frame to paste into");
        resyncFrames(editor);
        selectFrame(editor, 1);
        pasteLayerHere(editor, view);
        check(editor.layers.size() == 2, "pasted beside the empty frame's own layer");
        check(editor.activeSprite() == editor.frames[1].sprite, "in the second frame");
        check(editor.doc.engine().getLayerInfo(editor.active()->layer).value.sprite ==
              editor.frames[1].sprite, "and the pasted layer belongs to it");
        selectFrame(editor, 0);

        // Group both, then ungroup; the active layer survives each.
        selectLayer(editor, first);
        selectLayer(editor, copy, true);
        check(editor.selectedLayers.size() == 2, "two selected");
        groupSelectedLayers(editor, view);
        check(groupOf(editor.doc, first).valid() && groupOf(editor.doc, first) == groupOf(editor.doc, copy),
              "grouped together");
        check(editor.active() != nullptr && editor.active()->layer == copy, "active kept");
        check(editor.selectedLayers.size() == 2, "selection kept");
        ungroupActiveLayer(editor, view);
        check(!groupOf(editor.doc, copy).valid(), "ungrouped");
        check(editor.active()->layer == copy, "active kept through ungroup");

        // Clip, lock, and the pencil's respect for the lock.
        toggleActiveLayerClip(editor, view);
        {
            LayerProps props;
            readLayerProps(editor.doc, copy, &props);
            check(props.clipBase == first, "clipped to the layer below");
        }
        toggleActiveLayerClip(editor, view);
        toggleActiveLayerLock(editor);
        check(activeLayerLocked(editor), "locked");
        toggleActiveLayerLock(editor);
        check(!activeLayerLocked(editor), "unlocked");

        // Delete the copy; land on what is left. Undo brings it back selected
        // by handle, not by a stale index.
        deleteSelectedLayers(editor, view);
        check(editor.layers.size() == base, "deleted");
        check(editor.active()->layer == first, "landed on the original");
        check(editor.doc.undo(), "undo the delete");
        resyncLayers(editor);
        check(editor.layers.size() == base + 1, "back");
        check(editor.activeLayer < static_cast<int>(editor.layers.size()), "index in range");
        check(!editor.selectedLayers.empty(), "something selected");
        for (ls::LayerId id : editor.selectedLayers) {
            bool found = false;
            for (const PaintLayer& layer : editor.layers) { found = found || layer.layer == id; }
            check(found, "every selected layer exists");
        }

        // Back to one layer and one frame for what follows.
        selectLayer(editor, copy);
        deleteSelectedLayers(editor, view);
        check(deleteFrame(editor.doc, 1), "drop the paste frame");
        resyncLayers(editor);
        editor.clipboard = ls::LayerId{};
    }

    // References: imported, placed, undone, and gone when the document is.
    {
        CanvasView view(nullptr);
        ls::RasterBuffer picture = ls::makeRaster(10, 6);
        for (uint32_t y = 0; y < picture.height; ++y) {
            for (uint32_t x = 0; x < picture.width; ++x) {
                uint8_t* pixel = picture.row(y) + static_cast<size_t>(x) * 4u;
                pixel[0] = 200; pixel[3] = 255;
            }
        }
        std::vector<uint8_t> png;
        std::string pngError;
        check(encodeImageAsPng(picture, &png, &pngError), "encode a reference");

        Reference made;
        std::string error;
        check(addReference(editor.doc, "study.png", png, &made, &error),
              "import a reference");
        resyncReferences(editor, view);
        check(editor.references.size() == 1, "the panel sees it");
        check(editor.activeReference == made.id, "and it is selected");

        // Placed by the panel's own call, and read back.
        Reference* active = activeReference(editor);
        check(active != nullptr, "the selected reference resolves");
        if (active != nullptr) {
            Reference moved = *active;
            moved.x = 2.f;
            moved.opacity = 0.3f;
            editor.doc.beginAction("Place reference");
            check(updateReference(editor.doc, moved), "move it");
            editor.doc.endAction();
            resyncReferences(editor, view);
            check(activeReference(editor) != nullptr &&
                  activeReference(editor)->x == 2.f, "the move stuck");
        }

        // It changes no pixel of the artwork: the canvas draws it, the
        // compile does not know it exists.
        const ls::SpriteId sprite = editor.activeSprite();
        auto before = editor.doc.engine().compileSprite(sprite, ls::CompileProfile{});
        check(before.ok(), "the sprite still compiles with a reference in the file");

        check(editor.doc.undo(), "undo the move");
        resyncReferences(editor, view);
        check(editor.references.size() == 1, "still one reference");
        check(editor.doc.undo(), "undo the import");
        resyncReferences(editor, view);
        check(editor.references.empty(), "the reference went with it");
        check(editor.activeReference.empty(), "and nothing is selected");
        check(editor.doc.redo(), "redo the import");
        resyncReferences(editor, view);
        check(editor.references.size() == 1, "it came back");

        // A file that is not an image is refused, and stores nothing.
        Reference bad;
        error.clear();
        check(!addReference(editor.doc, "notes.txt", { 'n', 'o' }, &bad, &error),
              "a non-image is refused");
        check(!error.empty(), "and says why");
        check(editor.references.size() == 1, "nothing was stored");

        check(removeReference(editor.doc, editor.references[0]), "remove it");
        resyncReferences(editor, view);
        check(editor.references.empty(), "gone");
    }

    // Replacing the document forgets every interaction that was about the old
    // one. Each of these is an index that would otherwise be pressed into a
    // document it was never about.
    editor.renaming = 0;
    editor.timeline.renamingFrame = 0;
    editor.renamingSlot = 2;
    editor.confirmRemoveSlot = 2;
    editor.timeline.selectedStep = 3;
    editor.timeline.playing = true;
    editor.renamingPalette = documentPalette(editor.doc);
    check(newDocument(editor, 16), "new document mid-everything");
    check(!editor.renamingPalette.valid(), "no palette rename in flight");
    check(!editor.activeGroup.valid() && !editor.clipboard.valid(), "no group or clipboard");
    check(editor.references.empty() && editor.activeReference.empty(),
          "the new document has no references");
    check(editor.selectedLayers.size() == 1, "the new document's one layer is selected");
    check(listPalettes(editor.doc).size() == 1, "a new document has one palette");
    check(editor.renaming == -1, "no layer rename in flight");
    check(editor.timeline.renamingFrame == -1, "no frame rename in flight");
    check(editor.renamingSlot == ls::kColorRoleNone, "no slot rename in flight");
    check(editor.confirmRemoveSlot == ls::kColorRoleNone, "no removal pending");
    check(editor.timeline.selectedStep == 0, "no step selected");
    check(!editor.timeline.playing, "not playing");
    check(editor.timeline.activeCycle == -1, "no cycle selected");
    check(editor.frames.size() == 1 && editor.layers.size() == 1, "one frame, one layer");
    check(!editor.doc.canUndo(), "nothing to undo in a new document");

    // Put the first frame's drawing back for the sections that follow.
    editor.doc.beginAction("Pencil");
    paintPixels(editor.doc, *editor.active(), linePixels({2, 2}, {2, 9}));
    editor.doc.endAction();

    // ------------------------------------------------------------ cycles --
    //
    // The window's half of the cycle editor: that selecting one changes what
    // plays and what the canvas shows, and that a step is not a frame.

    check(duplicateFrame(editor.doc, 0) == 1, "a second frame to cycle over");
    check(duplicateFrame(editor.doc, 1) == 2, "and a third");
    resyncFrames(editor);
    check(editor.frames.size() == 3, "three frames");

    check(editor.timeline.activeCycle == -1, "no cycle to start");
    check(activeCycle(editor).frames.size() == 3,
          "with none selected, every frame plays");

    check(addCycle(editor.doc, "walk", 3) == 0, "make a cycle");
    resyncFrames(editor);
    selectCycle(editor, 0);
    check(editor.timeline.activeCycle == 0, "it is selected");
    check(editor.timeline.activeFrame == 0, "and it starts on its first picture");

    // 0 1 2 -> 0 2, then 0 2 2: a cycle that leaves a frame out and plays
    // another twice, which is the whole reason steps are a separate list.
    check(removeCycleStep(editor.doc, 0, 1, 3), "drop the middle step");
    resyncFrames(editor);
    check(addCycleStep(editor.doc, 0, 1, 2, 3) == 2, "and play the last twice");
    resyncFrames(editor);
    check(activeCycle(editor).frames == std::vector<int>({ 0, 2, 2 }),
          "the cycle is a sequence, not a set");

    // Playback follows the selected cycle rather than the frame list, so the
    // frame that was left out never shows.
    editor.timeline.playing = true;
    editor.timeline.startedAtMs = 0;
    {
        bool sawTheDroppedFrame = false;
        for (uint64_t t = 0; t < static_cast<uint64_t>(kDefaultFrameMs) * 3; t += 10) {
            if (frameToShow(editor, t) == 1) {
                sawTheDroppedFrame = true;
            }
        }
        check(!sawTheDroppedFrame, "a frame no step names never plays");
    }
    editor.timeline.playing = false;

    // Deleting the frame two steps named must clean up both of them.
    check(deleteFrame(editor.doc, 2), "delete the repeated frame");
    resyncFrames(editor);
    check(activeCycle(editor).frames == std::vector<int>({ 0 }),
          "both steps naming it are gone");

    // And a cycle can be got rid of, leaving every frame playing again.
    check(deleteCycle(editor.doc, 0, 2), "delete the cycle");
    resyncFrames(editor);
    selectCycle(editor, -1);
    check(editor.cycles.empty(), "it is gone");
    check(activeCycle(editor).frames.size() == 2, "every frame plays again");

    // ------------------------------------------------------------- sheets --
    //
    // What the window contributes to a sheet is deciding which cells go in it,
    // which is the half fast_core cannot know.

    check(addCycle(editor.doc, "walk", 2) == 0, "a cycle to export");
    resyncFrames(editor);
    selectCycle(editor, 0);
    check(addCycleStep(editor.doc, 0, 1, 0, 2) == 2, "that plays a frame twice");
    resyncFrames(editor);

    editor.sheetFromCycle = true;
    check(sheetSteps(editor) == std::vector<int>({ 0, 1, 0 }),
          "a sheet of the cycle is its steps, repeats and all");

    editor.sheetFromCycle = false;
    check(sheetSteps(editor) == std::vector<int>({ 0, 1 }),
          "a sheet of every frame is every frame once");

    // And the whole way out, through the same call the dialog makes.
    {
        const std::string sheetPath = "ui_selftest_sheet.png";
        editor.sheetFromCycle = true;
        editor.sheet.scale = 2;
        editor.sheet.layout = SheetLayout::Row;
        std::string sheetError;
        check(exportSheetToPng(editor.doc, editor.frames, sheetSteps(editor),
                               editor.cycles, sheetPath, editor.sheet, &sheetError),
              "write a sheet");
        check(fileExists(sheetPath), "the sheet is there");
        check(fileExists("ui_selftest_sheet.json"), "and its description beside it");
        deleteFile(sheetPath);
        deleteFile("ui_selftest_sheet.json");
        editor.sheet = SheetSettings{};
    }

    check(deleteCycle(editor.doc, 0, 2), "tidy the cycle away");
    resyncFrames(editor);
    selectCycle(editor, -1);

    while (editor.frames.size() > 1) {
        check(deleteFrame(editor.doc, static_cast<int>(editor.frames.size()) - 1),
              "back to one frame");
        resyncFrames(editor);
    }
    resyncLayers(editor);

    // ------------------------------------------------------ file handling --
    //
    // This touches the user's real recent list, so it is put back afterwards. A
    // test that leaves someone's settings changed is a bad test however green.
    RecentFiles theirs;
    theirs.load();
    editor.files.recent.clear();

    CanvasView canvas(nullptr);      // nothing here draws; it carries zoom and pan

    std::string error;
    const std::string path = "ui_selftest.lsprite";

    check(windowTitle(editor.doc).find('*') != std::string::npos,
          "an edited document is marked modified");
    canvas.setZoom(19.f);
    canvas.setPan(-33.f, 21.f);
    check(saveTo(editor, canvas, path), "save");
    check(!editor.doc.modified(), "saving clears the modified mark");
    check(editor.files.recent.entries().size() == 1, "the save is remembered");
    check(windowTitle(editor.doc).find("ui_selftest") != std::string::npos,
          "the title names the file");

    Editor reopened;
    // An ink naming a slot the opened palette does not have is let go on
    // open rather than kept pointing at nothing.
    reopened.inkRole = 250;
    canvas.resetView();
    check(canvas.zoom() == 8.f, "the view is reset before loading");

    openPath(reopened, canvas, path);
    check(!reopened.layers.empty(), "the reopened file has layers");
    check(canvas.zoom() == 19.f, "zoom comes back");
    check(canvas.panX() == -33.f, "pan x comes back");
    check(canvas.panY() == 21.f, "pan y comes back");
    check(!paletteEntries(reopened.doc).empty(), "the palette came back");

    check(reopened.active() != nullptr, "there is an active layer");
    check(reopened.inkRole == ls::kColorRoleNone,
          "an ink naming a slot the file lacks is let go");

    // A hostile view has to be clamped rather than trusted.
    reopened.doc.setUiState("{\"zoom\":1e6,\"panX\":-9e9,\"activeLayer\":9999}");
    check(reopened.doc.save(path, &error), "save a hostile view");
    openPath(reopened, canvas, path);
    check(canvas.zoom() <= 64.f, "an absurd zoom is clamped");
    check(canvas.panX() >= -20000.f, "an absurd pan is clamped");
    check(reopened.activeLayer < static_cast<int>(reopened.layers.size()),
          "an out-of-range layer index is clamped");

    check(reopened.doc.open(path, &error), "open again");
    check(reopened.doc.engine().documents().size() == 1, "one document held");

    theirs.save();
    deleteFile(path);

    // File > New as the window drives it: a size that is not square, a
    // background of its own at the bottom, a preset palette, and the drawing
    // layer above the background, selected, with nothing to undo.
    {
        Editor fresh;
        FileState::NewDocument spec;
        spec.width = 12;
        spec.height = 5;
        spec.background = 1;
        spec.preset = 0;
        check(newDocument(fresh, spec), "a new document from the window's choices");
        auto size = fresh.doc.engine().getCanvasSize(fresh.doc.id());
        check(size.ok() && size.value.x == 12 && size.value.y == 5, "its own width and height");
        check(fresh.layers.size() == 2, "a background and a layer to draw on");
        check(fresh.active() != nullptr && fresh.activeLayer == 1, "the drawing layer is active");
        const ls::RasterBuffer shown = [&] {
            auto compiled = fresh.doc.engine().compileSprite(
                fresh.sprite, compileProfile(ls::CompileProfileType::Export, 12, 5));
            return compiled.ok() ? compiled.value.raster : ls::RasterBuffer{};
        }();
        check(!shown.empty() && ls::readPixel(shown, 11, 4).r == 255, "the background is white");
        check(paletteEntries(fresh.doc).size() == palettePresets()[0].colours.size(),
              "the preset is the palette");
        check(!fresh.doc.canUndo() && !fresh.doc.modified(), "a new document has nothing to undo");
    }

    // Rotating a selection freely: its pixels on a layer of their own with a
    // RotSprite rotation, the original layer without them, one undo step.
    {
        Editor turned;
        check(newDocument(turned, 16), "a document to turn");
        turned.doc.beginAction("Pencil");
        paintPixels(turned.doc, *turned.active(), linePixels({ 2, 2 }, { 9, 2 }));
        turned.doc.endAction();
        const size_t before = turned.layers.size();
        turned.selection.mask = rectangleMask({ 2, 2 }, { 9, 2 });
        check(rotateSelectionFreely(turned), "rotate the selection freely");
        check(turned.layers.size() == before + 1, "onto a layer of its own");
        const std::vector<TransformEntry> spin = listTransforms(turned.doc, turned.active()->layer);
        check(spin.size() == 1 && spin.front().sampling == ls::SamplingPolicy::RotSprite,
              "with a RotSprite rotation");
        check(turned.doc.undo(), "one undo");
        resyncLayers(turned);
        check(turned.layers.size() == before, "puts it back");
    }

    // Tracks through the editor: a layer added in one frame is in the other,
    // and merging down merges in both, one undo step each.
    {
        Editor tracked;
        CanvasView view(nullptr);
        check(newDocument(tracked, 8), "a document for tracks");
        check(tracksOn(tracked.doc), "a new document keeps its frames' layers in step");
        check(addEmptyFrame(tracked, 0) == 1, "a second frame");
        resyncFrames(tracked);
        const ls::SpriteId one = tracked.frames[0].sprite;
        const ls::SpriteId two = tracked.frames[1].sprite;
        check(layerOrder(tracked.doc, one).size() == layerOrder(tracked.doc, two).size(),
              "a new frame has every layer");
        PaintLayer cape;
        tracked.doc.beginAction("Add layer");
        check(createPaintLayer(tracked.doc, one, "cape", ls::Color{ 90, 200, 90, 255 }, &cape),
              "a layer in the first frame");
        tracked.doc.endAction();
        const size_t count = layerOrder(tracked.doc, one).size();
        check(layerOrder(tracked.doc, two).size() == count, "is in the second too");
        check(!trackKey(tracked.doc, cape.layer).empty() &&
              layerOfTrack(tracked.doc, two, trackKey(tracked.doc, cape.layer)).valid(),
              "as the same track");
        resyncLayers(tracked);
        selectLayer(tracked, cape.layer);
        mergeActiveLayerDown(tracked, view);
        check(layerOrder(tracked.doc, one).size() == count - 1 &&
              layerOrder(tracked.doc, two).size() == count - 1, "merging down merges in both");
        check(tracked.doc.undo() && layerOrder(tracked.doc, two).size() == count,
              "and one undo takes both back");
    }

    // Edit > Fill and Stroke: the selection painted in the current colour,
    // and a stroke only round its edge; each one undo step.
    {
        Editor filled;
        check(newDocument(filled, 16), "a document to fill");
        filled.selection.mask = rectangleMask({ 2, 2 }, { 9, 9 });
        filled.color[0] = 0.9f; filled.color[1] = 0.1f; filled.color[2] = 0.1f; filled.color[3] = 1.f;
        filled.brush.size = 2;
        check(fillSelection(filled, true), "stroke the selection");
        const auto redAt = [&](int x, int y) {
            ls::RasterBuffer picture;
            if (!compileForExport(filled.doc, filled.sprite, 1, &picture, nullptr)) {
                return false;
            }
            const uint8_t* px = picture.row(static_cast<uint32_t>(y)) + x * 4;
            return px[0] > 200 && px[1] < 60 && px[3] > 200;
        };
        check(redAt(2, 2) && redAt(3, 5) && !redAt(4, 4) && !redAt(5, 5), "a band two wide inside the edge");
        check(fillSelection(filled, false) && redAt(5, 5) && !redAt(10, 10), "filling paints the inside");
        check(filled.doc.undo() && !redAt(5, 5) && redAt(2, 2), "one undo each");
    }

    // Tweens through the editor: an offset keyed at 0 and 8 puts the middle
    // frame's pixel halfway, and one undo takes it back.
    {
        Editor tweened;
        check(newDocument(tweened, 16), "a document to tween");
        PaintLayer ball;
        tweened.doc.beginAction("Ball");
        check(createPaintLayer(tweened.doc, tweened.sprite, "ball", ls::Color{ 220, 40, 40, 255 }, &ball),
              "a layer to move");
        paintPixels(tweened.doc, ball, {{ 2, 2 }});
        tweened.doc.endAction();
        check(duplicateFrame(tweened.doc, 0) == 1 && duplicateFrame(tweened.doc, 1) == 2,
              "three frames");
        resyncFrames(tweened);
        const std::string key = trackKey(tweened.doc, ball.layer);
        const ls::LayerId first = layerOfTrack(tweened.doc, tweened.frames[0].sprite, key);
        const ls::LayerId last = layerOfTrack(tweened.doc, tweened.frames[2].sprite, key);
        tweened.doc.beginAction("Keys");
        addOffset(tweened.doc, first, { 0.f, 0.f });
        addOffset(tweened.doc, last, { 8.f, 0.f });
        tweened.doc.endAction();
        std::vector<ls::SpriteId> run;
        for (const Frame& f : tweened.frames) { run.push_back(f.sprite); }
        tweened.doc.beginAction("Tween");
        check(tweenTransforms(tweened.doc, run, key, TweenEasing::Linear, nullptr), "tween the run");
        tweened.doc.endAction();
        const auto redAt = [&](int x, int y) {
            ls::RasterBuffer picture;
            if (!compileForExport(tweened.doc, tweened.frames[1].sprite, 1, &picture, nullptr)) {
                return false;
            }
            const uint8_t* px = picture.row(static_cast<uint32_t>(y)) + x * 4;
            return px[0] > 150 && px[1] < 100 && px[3] > 200;
        };
        check(redAt(6, 2) && !redAt(2, 2), "the middle frame's pixel is halfway");
        check(tweened.doc.undo() && redAt(2, 2), "and one undo puts it back");
    }

    // Tabs: a second document opens beside the first rather than over it; each
    // keeps its own pixels, layers, history and selection across a switch;
    // closing one shows its neighbour; a blank untouched document is replaced
    // rather than kept.
    {
        Editor tabbed;
        CanvasView view(nullptr);
        check(newDocument(tabbed, 16), "a first document");
        initTabs(tabbed);
        check(documentIsBlank(tabbed), "an untouched document is blank");
        tabbed.doc.beginAction("Pencil");
        paintPixels(tabbed.doc, *tabbed.active(), {{ 3, 3 }});
        tabbed.doc.endAction();
        tabbed.selection.mask = rectangleMask({ 1, 1 }, { 4, 4 });
        check(!documentIsBlank(tabbed), "drawn on, it is not");

        openTab(tabbed, view);
        check(newDocument(tabbed, 8), "a second document in its own tab");
        check(tabCount(tabbed) == 2 && tabbed.activeTab == 1, "two tabs, the new one showing");
        check(tabbed.selection.empty() && !tabbed.doc.canUndo(), "with nothing of the first's");
        auto size = tabbed.doc.engine().getCanvasSize(tabbed.doc.id());
        check(size.ok() && size.value.x == 8, "its own canvas");

        switchToTab(tabbed, view, 0);
        size = tabbed.doc.engine().getCanvasSize(tabbed.doc.id());
        check(tabbed.activeTab == 0 && size.ok() && size.value.x == 16, "back to the first");
        check(!tabbed.selection.empty() && tabbed.doc.canUndo(), "its selection and history kept");
        check(tabbed.active() != nullptr && tabbed.doc.modified(), "and its layers and changes");
        check(tabModified(tabbed, 0) && !tabModified(tabbed, 1), "which tab has unsaved work");
        check(modifiedOtherTab(tabbed) < 0, "no other tab has");

        closeActiveTab(tabbed, view);
        size = tabbed.doc.engine().getCanvasSize(tabbed.doc.id());
        check(tabCount(tabbed) == 1 && size.ok() && size.value.x == 8, "closing shows the other");
        closeActiveTab(tabbed, view);
        check(tabCount(tabbed) == 1 && tabbed.active() != nullptr,
              "closing the last leaves a new blank one");
    }

    // A polygon and a curve placed point by point: each lands as an element
    // of its kind, is the active shape with a handle per point, and finishing
    // with too few points places nothing.
    {
        Editor paths;
        CanvasView view(nullptr);
        check(newDocument(paths, 16), "a document for paths");
        paths.tool = Tool::Polygon;
        paths.placingPath = true;
        paths.pathPoints = { { 2.5f, 2.5f }, { 12.5f, 2.5f }, { 7.5f, 12.5f } };
        paths.pathHandles.assign(3, { 0.f, 0.f });
        check(finishPath(paths, view, true), "a polygon is placed");
        check(!paths.placingPath && paths.pathPoints.empty(), "and the tool is ready again");
        ShapeLayer placed;
        check(activeShape(paths, &placed) && placed.kind == ShapeKind::Polygon,
              "the polygon is the active shape");
        ShapeParams read;
        check(readShapeParams(paths.doc, placed, &read) &&
              shapeHandles(placed.kind, read).size() == 3, "with a handle per corner");

        paths.tool = Tool::Curve;
        paths.placingPath = true;
        paths.pathPoints = { { 2.5f, 14.5f }, { 8.5f, 4.5f }, { 14.5f, 14.5f } };
        paths.pathHandles = { { 0.f, 0.f }, { 3.f, 0.f }, { 0.f, 0.f } };
        check(finishPath(paths, view, false), "a curve is placed");
        check(activeShape(paths, &placed) && placed.kind == ShapeKind::Curve,
              "the curve is the active shape");
        check(readShapeParams(paths.doc, placed, &read) && read.points.size() == 7 &&
              shapeHandles(placed.kind, read).size() == 7, "three anchors and four controls");
        {
            size_t shapes = 0;
            for (const Element& element : elementsOf(paths.doc, paths.active()->layer)) {
                shapes += element.isGeometry() ? 1u : 0u;
            }
            check(shapes == 2, "both on the one layer");
        }

        paths.placingPath = true;
        paths.pathPoints = { { 1.5f, 1.5f } };
        paths.pathHandles = { { 0.f, 0.f } };
        check(!finishPath(paths, view, false) && !paths.placingPath,
              "one point is not a curve, and is let go");
    }

    // A run of frames selected in the strip: it is the run, it plays as a
    // loop of its own when no cycle is chosen, and speed scales the clock.
    {
        Editor anim;
        check(newDocument(anim, 8), "a document to animate");
        for (int i = 0; i < 3; ++i) {
            check(addEmptyFrame(anim, i) == i + 1, "an empty frame");
        }
        resyncFrames(anim);
        anim.timeline.activeFrame = 3;
        anim.timeline.rangeAnchor = 1;
        int first = 0;
        int last = 0;
        check(frameRange(anim, &first, &last) && first == 1 && last == 3, "the run is 1 to 3");
        check(activeCycle(anim).frames == std::vector<int>({ 1, 2, 3 }), "the run plays alone");
        anim.timeline.playing = true;
        anim.timeline.startedAtMs = 1000;
        anim.timeline.speed = 2.f;
        // Holds of 100 ms at double speed: 100 ms in is the run's third step.
        check(frameToShow(anim, 1100) == 3, "speed scales the clock, not the holds");
        anim.timeline.rangeAnchor = 3;
        check(!frameRange(anim, &first, &last), "one frame is not a run");
    }

    if (failures == 0) {
        std::printf("ui_selftest: all checks passed\n");
        return 0;
    }
    std::printf("ui_selftest: %d check(s) failed\n", failures);
    return 1;
}

} // namespace

int main(int argc, char** argv) {
    // --export makes this a batch run: open, write, exit, with no window --
    // what a build script wants. See app/batch.h for the options.
    {
        const std::vector<std::string> args(argv + 1, argv + argc);
        BatchJob job;
        std::string error;
        if (parseBatch(args, &job, &error)) {
            std::string message;
            const int code = runBatch(job, &message);
            std::printf("%s\n", message.c_str());
            return code;
        }
        if (!error.empty()) {
            std::printf("%s\n", error.c_str());
            return 2;
        }
    }

    const Options options = parseOptions(argc, argv);
    if (options.selfTest) {
        return runSelfTest();
    }

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::printf("SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow("Sprit's'fast", 1440, 900,
                                          SDL_WINDOW_RESIZABLE |
                                          SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (window == nullptr) {
        std::printf("SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
    if (renderer == nullptr) {
        std::printf("SDL_CreateRenderer failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    SDL_SetRenderVSync(renderer, 1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    // Every window is positioned explicitly each frame, so ImGui's saved layout
    // would never be read -- it would only drop an imgui.ini into whatever
    // directory the editor happened to be started from.
    ImGui::GetIO().IniFilename = nullptr;

    theme::loadFonts(SDL_GetWindowDisplayScale(window));
    theme::apply();

    ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer3_Init(renderer);

    Editor editor;
    editor.files.dialog.init();
    editor.files.recent.load();

    CanvasView canvas(renderer);
    if (!newDocument(editor, 32)) {
        std::printf("could not create the first document\n");
        return 1;
    }
    canvas.requestFit();
    initTabs(editor);
    attachTracks(editor);
    if (!options.openPath.empty()) {
        openPath(editor, canvas, options.openPath);
    }
    if (options.demoStroke) {
        drawDemoContent(editor);
        canvas.invalidate();
    }
    // Whatever built the document -- new, opened, or the demo -- the panel
    // and the canvas read the references from it here rather than each
    // caller remembering to.
    resyncReferences(editor, canvas);
    editor.sheetPanelOpen = options.showSheetPanel;
    if (!options.select.empty()) {
        // x,y,w,h -- four numbers and three commas, or nothing is selected.
        int values[4] = {};
        const char* at = options.select.c_str();
        int read = 0;
        for (; read < 4; ++read) {
            char* end = nullptr;
            values[read] = static_cast<int>(std::strtol(at, &end, 10));
            if (end == at) {
                break;
            }
            at = (*end == ',') ? end + 1 : end;
        }
        if (read == 4 && values[2] > 0 && values[3] > 0) {
            editor.selection.mask = rectangleMask(
                { values[0], values[1] },
                { values[0] + values[2] - 1, values[1] + values[3] - 1 });
        }
    }
    if (options.light) {
        editor.prefs.lightTheme = true;
        theme::setLight(true);
        canvasDefaultsFor(true, &editor.prefs);
        applyViewPreferences(editor.prefs, canvas);
    }
    if (options.isometric) {
        canvas.tileGrid().visible = true;
        canvas.tileGrid().isometric = true;
        canvas.tileGrid().width = 16;
        canvas.tileGrid().height = 8;
    }
    if (options.tiled) {
        canvas.setTiledMode(TiledMode::Both);
        canvas.tileGrid().visible = true;
        canvas.tileGrid().width = 8;
        canvas.tileGrid().height = 8;
    }
    editor.symmetryAcross = editor.symmetryAcross || options.symmetry;
    editor.symmetryDown = editor.symmetryDown || options.symmetry;
    if (!options.tool.empty()) {
        toolFromName(options.tool, &editor.tool);
    }
    // The safety net, before anything can be drawn and lost. Autosave is
    // simply off when there is nowhere to write, rather than writing
    // somewhere the person would not think to look.
    // A headless run keeps to the defaults, so what it captures or checks is
    // the same on every machine, whatever keys the person there has set.
    const bool headless = options.selfTest || options.frames > 0 || !options.script.empty();
    if (!headless) {
        loadSettings(editor);
    }
    editor.systemClipboard = !headless;
    initSystemClipboard(window);
    editor.preferencesOpen = options.preferences;
    editor.preferencesShowKeys = options.preferencesKeys;
    if (!editor.recovery.begin()) {
        editor.autosaveOn = false;
    } else {
        editor.autosaveOn = editor.prefs.autosaveOn;
        editor.recovery.setIntervalSeconds(editor.prefs.autosaveSeconds);
    }
    if (editor.prefs.lightTheme) {
        theme::setLight(true);
    }
    applyViewPreferences(editor.prefs, canvas);
    editor.doc.setHistoryLimit(editor.prefs.historyLimit);
    if (options.autosaveSeconds > 0) {
        editor.recovery.setIntervalSeconds(options.autosaveSeconds);
    }
    editor.recovered = findRecoveredWork();
    editor.askingToRecover = !editor.recovered.empty();

    if (options.library) {
        editor.libraryOpen = true;
        editor.libraryStale = true;
    }
    editor.libraryFolders.load();
    // Nothing chosen yet: the folder this document is in is the obvious
    // project, and costs the person no decision.
    if (editor.libraryFolders.project.empty() && !editor.doc.path().empty()) {
        editor.libraryFolders.project = directoryOf(editor.doc.path());
    }
    if (!options.language.empty()) {
        applyLanguage(options.language);
    }
    if (options.rotsprite && editor.active() != nullptr) {
        // A small figure drawn twice: the left turned by coverage, the right
        // by RotSprite, both 30 degrees.
        const char* rows[] = {
            "...XXXX...",
            "..XooooX..",
            ".XooXXooX.",
            ".XoX..XoX.",
            ".XoX..XoX.",
            ".XooXXooX.",
            "..XooooX..",
            "...XXXX...",
        };
        for (int copy = 0; copy < 2; ++copy) {
            PaintLayer made;
            editor.doc.beginAction("Figure");
            createPaintLayer(editor.doc, editor.sprite, copy == 0 ? "coverage" : "rotsprite",
                             ls::Color{ 30, 30, 40, 255 }, &made);
            const int ox = copy == 0 ? 3 : 18;
            std::vector<ls::Vec2i> dark;
            std::vector<ls::Vec2i> light;
            for (int y = 0; y < 8; ++y) {
                for (int x = 0; x < 10; ++x) {
                    const char c = rows[y][x];
                    if (c == 'X') { dark.push_back({ ox + x, 12 + y }); }
                    if (c == 'o') { light.push_back({ ox + x, 12 + y }); }
                }
            }
            paintPixels(editor.doc, made, dark);
            Ink amber;
            amber.colour = { 240, 170, 60, 255 };
            InkStroke stroke;
            if (beginInkStroke(editor.doc, made.layer, amber, &stroke)) {
                strokeInk(editor.doc, stroke, light);
            }
            addRotate(editor.doc, made.layer, 30.f,
                      { static_cast<float>(ox) + 5.f, 16.f },
                      copy == 0 ? ls::SamplingPolicy::Coverage : ls::SamplingPolicy::RotSprite);
            editor.doc.endAction();
        }
        resyncLayers(editor);
    }
    if (options.rectangle[0] >= 0.f && editor.active() != nullptr) {
        ShapeParams params;
        params.from = { options.rectangle[0], options.rectangle[1] };
        params.to = { options.rectangle[2], options.rectangle[3] };
        ShapeLayer made;
        editor.doc.beginAction("Rectangle");
        addShapeElement(editor.doc, editor.active()->layer, ShapeKind::Rectangle, params,
                        foregroundInk(editor), &made);
        editor.doc.endAction();
        resyncLayers(editor);
    }
    if (options.tween && editor.active() != nullptr) {
        PaintLayer made;
        editor.doc.beginAction("Figure");
        createPaintLayer(editor.doc, editor.sprite, "arrow", ls::Color{ 240, 170, 60, 255 }, &made);
        std::vector<ls::Vec2i> body;
        for (int y = 12; y < 18; ++y) {
            for (int x = 3; x < 7; ++x) { body.push_back({ x, y }); }
        }
        for (int x = 7; x < 11; ++x) { body.push_back({ x, 14 }); body.push_back({ x, 15 }); }
        paintPixels(editor.doc, made, body);
        editor.doc.endAction();
        for (int i = 0; i < 4; ++i) {
            duplicateFrame(editor.doc, i);
        }
        resyncFrames(editor);
        const std::string key = trackKey(editor.doc, made.layer);
        const ls::LayerId from = layerOfTrack(editor.doc, editor.frames.front().sprite, key);
        const ls::LayerId to = layerOfTrack(editor.doc, editor.frames.back().sprite, key);
        editor.doc.beginAction("Keys");
        addRotate(editor.doc, from, 0.f, { 6.f, 15.f }, ls::SamplingPolicy::RotSprite);
        addOffset(editor.doc, from, { 0.f, 0.f });
        addRotate(editor.doc, to, 180.f, { 6.f, 15.f }, ls::SamplingPolicy::RotSprite);
        addOffset(editor.doc, to, { 18.f, 0.f });
        editor.doc.endAction();
        std::vector<ls::SpriteId> run;
        for (const Frame& f : editor.frames) { run.push_back(f.sprite); }
        editor.doc.beginAction("Tween");
        tweenTransforms(editor.doc, run, key, TweenEasing::EaseInOut, nullptr);
        editor.doc.endAction();
        resyncFrames(editor);
        editor.timeline.rangeAnchor = 0;
        selectFrame(editor, 2);
        editor.timeline.visible = true;
        editor.timeline.onion = true;
        resyncLayers(editor);
    }
    if (options.slices) {
        Slice panel;
        panel.name = "panel";
        panel.bounds = { { 3, 3 }, { 15, 12 } };
        panel.nine = true;
        panel.centre = { { 3, 3 }, { 9, 6 } };
        Slice hit;
        hit.name = "hitbox";
        hit.bounds = { { 12, 14 }, { 22, 30 } };
        hit.hasPivot = true;
        hit.pivot = { 5, 16 };
        hit.colour = { 230, 90, 60, 255 };
        writeSlices(editor.doc, { panel, hit });
        writeGuides(editor.doc, { { true, 16 }, { false, 20 } });
        editor.tool = Tool::Slice;
        editor.activeSlice = 0;
        editor.slicesOpen = true;
    }
    if (options.shadow && editor.active() != nullptr) {
        ShadowSettings settings;
        settings.scope = OutlineScope::Sprite;
        settings.dx = 2;
        settings.dy = 2;
        settings.opacity = 0.6f;
        editor.doc.beginAction("Add shadow");
        setShadow(editor.doc, *editor.active(), settings);
        editor.doc.endAction();
    }
    if (options.adjust && editor.active() != nullptr) {
        editor.adjustDialog = Editor::AdjustDialog{};
        editor.adjustDialog.open = true;
        editor.adjustDialog.scope = 2;
        editor.adjustDialog.slots = true;
        editor.adjustDialog.adjust.hue = 150.f;
        editor.adjustDialog.adjust.contrast = 0.2f;
        editor.doc.beginAction("Adjust colours");
    }
    if (options.tabs) {
        // Two more, the second of them changed, and the middle one showing.
        for (uint32_t size : { 24u, 48u }) {
            openTab(editor, canvas);
            newDocument(editor, size);
            if (size == 48u) {
                editor.doc.beginAction("Pencil");
                paintPixels(editor.doc, *editor.active(), linePixels({ 4, 4 }, { 40, 30 }));
                editor.doc.endAction();
            }
        }
        switchToTab(editor, canvas, 1);
    }
    if (options.paths && editor.active() != nullptr) {
        editor.tool = Tool::Polygon;
        editor.placingPath = true;
        editor.pathPoints = { { 3.5f, 20.5f }, { 12.5f, 6.5f }, { 22.5f, 12.5f }, { 16.5f, 27.5f } };
        editor.pathHandles.assign(4, { 0.f, 0.f });
        finishPath(editor, canvas, true);
        editor.tool = Tool::Curve;
        editor.placingPath = true;
        editor.pathPoints = { { 4.5f, 4.5f }, { 16.5f, 16.5f }, { 28.5f, 4.5f } };
        editor.pathHandles = { { 6.f, 0.f }, { 6.f, 6.f }, { 0.f, 6.f } };
        editor.color[0] = 0.1f;
        editor.color[1] = 0.1f;
        editor.color[2] = 0.15f;
        finishPath(editor, canvas, false);
        editor.tool = Tool::Move;
    }
    if (options.layerProperties && editor.active() != nullptr) {
        // Tagged and annotated, so a capture shows the stripe and the notes.
        const ls::Color orange{ 232, 146, 52, 255 };
        setLayerTag(editor.doc, editor.active()->layer, &orange);
        setLayerNotes(editor.doc, editor.active()->layer, "Outline pass still to do.");
        editor.propertiesLayer = editor.active()->layer;
    }
    if (options.play) {
        editor.timeline.playing = true;
        editor.timeline.startedAtMs = SDL_GetTicks();
    }
    if (options.systemCopy || options.systemPaste) {
        editor.systemClipboard = true;
        // Paste first, so both together are a round trip: what came in goes
        // back out through the selection the paste leaves.
        if (options.systemPaste) {
            pasteCommand(editor, canvas, false);
            if (editor.floating.active()) {
                settleFloating(editor);
            }
            resyncLayers(editor);
            std::printf("clipboard: %s\n", editor.status.c_str());
        }
        if (options.systemCopy) {
            copyCommand(editor);
            std::printf("clipboard: %s\n", editor.status.c_str());
        }
    }
    if (!options.sheetPath.empty()) {
        std::string sheetError;
        SheetSettings settings;
        settings.scale = 2;
        if (exportSheetToPng(editor.doc, editor.frames, sheetSteps(editor),
                             editor.cycles, options.sheetPath, settings,
                             &sheetError)) {
            std::printf("sheet: wrote %s, %d cells\n", options.sheetPath.c_str(),
                        static_cast<int>(sheetSteps(editor).size()));
        } else {
            std::printf("sheet: %s\n", sheetError.c_str());
            return 1;
        }
    }

    UiScript script;
    if (!options.script.empty()) {
        std::string scriptError;
        if (!script.load(options.script, &scriptError)) {
            std::printf("script: %s\n", scriptError.c_str());
            return 1;
        }
    }

    bool running = true;
    bool idleBroken = false;
    int frame = 0;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL3_ProcessEvent(&event);

            if (event.type == SDL_EVENT_QUIT) {
                // The close button asks the same question the menu does rather
                // than throwing the work away.
                requestAction(editor, canvas, window, PendingAction::Quit);
            }
            // Another program put something on the clipboard: the next paste
            // looks there first. (Windows reads the clipboard's own counter.)
            if (event.type == SDL_EVENT_CLIPBOARD_UPDATE) {
                noteClipboardEvent(event.clipboard.owner);
            }
            if (event.type == SDL_EVENT_DROP_FILE && event.drop.data != nullptr) {
                requestAction(editor, canvas, window, PendingAction::OpenPath,
                              event.drop.data);
            }

            // A stylus. SDL sends mouse events for it too, which is what
            // draws; these carry what the mouse path cannot -- pressure, and
            // which end is touching. The eraser end erases for as long as it
            // is down, then hands the tool back.
            if (event.type == SDL_EVENT_PEN_PROXIMITY_IN) {
                editor.pen.seen = true;
            }
            if (event.type == SDL_EVENT_PEN_AXIS &&
                event.paxis.axis == SDL_PEN_AXIS_PRESSURE) {
                editor.pen.seen = true;
                editor.pen.pressure = event.paxis.value;
            }
            if (event.type == SDL_EVENT_PEN_DOWN || event.type == SDL_EVENT_PEN_UP) {
                editor.pen.seen = true;
                editor.pen.down = event.ptouch.down;
                editor.pen.eraser = event.ptouch.eraser;
                if (event.ptouch.down && event.ptouch.eraser && !editor.eraserTipHeld &&
                    (editor.tool == Tool::Pencil || editor.tool == Tool::Eraser)) {
                    editor.toolBeforeEraserTip = editor.tool;
                    editor.tool = Tool::Eraser;
                    editor.eraserTipHeld = true;
                } else if (!event.ptouch.down && editor.eraserTipHeld) {
                    editor.tool = editor.toolBeforeEraserTip;
                    editor.eraserTipHeld = false;
                }
            }
        }

        processDialogResult(editor, canvas, window);
        if (editor.quitRequested) {
            running = false;
        }

        // The title carries the file name and whether there is unsaved work.
        // Only set when it changes: this runs every frame.
        const std::string title = windowTitle(editor.doc);
        if (title != editor.lastTitle) {
            SDL_SetWindowTitle(window, title.c_str());
            editor.lastTitle = title;
        }

        // Autosave. Once a frame is far more often than it writes: the
        // session decides, and it refuses while a drag is open so a copy is
        // never taken of a half-committed stroke.
        if (editor.autosaveOn) {
            const uint64_t seconds = SDL_GetTicks() / 1000ull;
            if (editor.recovery.tick(editor.doc, seconds, editor.busy())) {
                editor.say("Recovery copy written");
            }
        }

        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        script.feed(editor, canvas);
        if (options.drag[0] >= 0.f) {
            // Settle, press, move in steps, release -- after the backend has
            // had its say, so these are the last word on the pointer.
            constexpr int kPress = 5;
            constexpr int kSteps = 12;
            ImGuiIO& io = ImGui::GetIO();
            // The middle of a canvas pixel, where the view last drew it.
            const auto at = [&](float t) {
                const ImVec2 origin = canvas.artworkOrigin();
                const float x = options.drag[0] + (options.drag[2] - options.drag[0]) * t;
                const float y = options.drag[1] + (options.drag[3] - options.drag[1]) * t;
                return ImVec2(origin.x + (x + 0.5f) * canvas.zoom(),
                              origin.y + (y + 0.5f) * canvas.zoom());
            };
            if (frame == kPress - 1) {
                io.AddMousePosEvent(at(0.f).x, at(0.f).y);
            } else if (frame == kPress) {
                io.AddMousePosEvent(at(0.f).x, at(0.f).y);
                io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
            } else if (frame > kPress && frame <= kPress + kSteps) {
                const ImVec2 p = at(static_cast<float>(frame - kPress) / static_cast<float>(kSteps));
                io.AddMousePosEvent(p.x, p.y);
            } else if (frame == kPress + kSteps + 1) {
                io.AddMousePosEvent(at(1.f).x, at(1.f).y);
                io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
            }
        }
        ImGui::NewFrame();
        canvas.frames().beginFrame();
        // Frames come and go through several doors -- the strip's delete, an
        // undo of a duplicate, a redo of a delete -- and each would have to
        // remember to drop the texture. Checked here instead: more textures
        // than frames means some belong to sprites that no longer exist.
        if (canvas.frames().heldTextures() > editor.frames.size()) {
            auto info = editor.doc.engine().getDocumentInfo(editor.doc.id());
            if (info.ok()) {
                canvas.frames().retainOnly(info.value.sprites);
            }
        }

        // A click anywhere off the artwork -- a panel, a menu, the strip --
        // drops a float before whatever was clicked acts, so no other action
        // ever lands inside the move's history entry.
        if (editor.floating.active() && !editor.draggingFloat && !editor.canvasHovered &&
            (ImGui::IsMouseClicked(ImGuiMouseButton_Left) ||
             ImGui::IsMouseClicked(ImGuiMouseButton_Right))) {
            settleFloating(editor);
        }
        drawMenuBar(editor, canvas, window);
        handleShortcuts(editor, canvas, window);
        {
            const TileGrid& tiles = canvas.tileGrid();
            editor.snapGrid = { tiles.width, tiles.height, tiles.offsetX, tiles.offsetY, {}, {} };
            if (editor.guidesShown) {
                for (const Guide& guide : readGuides(editor.doc)) {
                    (guide.vertical ? editor.snapGrid.linesX : editor.snapGrid.linesY)
                        .push_back(guide.at);
                }
            }
        }
        drawWindow(editor, canvas, window);

        ImGui::Render();
        const ImVec4 bg = theme::palette().windowBackground;
        SDL_SetRenderDrawColor(renderer,
                               static_cast<Uint8>(bg.x * 255),
                               static_cast<Uint8>(bg.y * 255),
                               static_cast<Uint8>(bg.z * 255), 255);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);

        // Taken before the present, while the frame is still readable back off
        // the render target.
        if (!options.screenshot.empty() && options.frames > 0 &&
            frame == options.frames - 1) {
            if (SDL_Surface* shot = SDL_RenderReadPixels(renderer, nullptr)) {
                if (!SDL_SaveBMP(shot, options.screenshot.c_str())) {
                    std::printf("could not write %s: %s\n",
                                options.screenshot.c_str(), SDL_GetError());
                } else {
                    std::printf("wrote %s\n", options.screenshot.c_str());
                }
                SDL_DestroySurface(shot);
            } else {
                std::printf("could not read the frame back: %s\n", SDL_GetError());
            }
        }

        if (const std::string asked = script.takeShot(); !asked.empty()) {
            if (SDL_Surface* shot = SDL_RenderReadPixels(renderer, nullptr)) {
                SDL_SaveBMP(shot, asked.c_str());
                SDL_DestroySurface(shot);
            }
        }

        SDL_RenderPresent(renderer);
        if (script.finished()) {
            idleBroken = idleBroken || script.failures() > 0;
            running = false;
        }

        // The fit happens on the first frame; a zoom asked for comes after it.
        if (frame == 0 && options.zoom > 0.f) {
            canvas.setZoom(options.zoom);
        }
        if (options.frames > 0 && ++frame >= options.frames) {
            // What the last settled frame cost. Nothing changed for the whole
            // run after the demo was built, so this must be zero -- if it is
            // not, something is compiling a picture it already has, and this
            // is where CI notices.
            std::printf("frames: %zu textures held, %d compile(s) in the last "
                        "frame, %.2f ms for the last one\n",
                        canvas.frames().heldTextures(),
                        canvas.frames().compilesThisFrame(),
                        canvas.frames().lastCompileMs());
            // Whether the safety net is actually armed. A person cannot see
            // autosave working, and neither can CI, so it says so.
            std::printf("autosave: %s, every %us, copy %s\n",
                        editor.autosaveOn ? (editor.recovery.active() ? "on" : "no folder")
                                          : "off",
                        editor.recovery.intervalSeconds(),
                        editor.recovery.haveCopy() ? "written" : "not yet due");
            if (options.expectDrawn >= 0) {
                // What the drag left: every pixel of the frame with any alpha.
                ls::RasterBuffer picture;
                int drawn = 0;
                if (compileForExport(editor.doc, editor.sprite, 1, &picture, nullptr)) {
                    for (size_t i = 3; i < picture.pixels.size(); i += 4) {
                        drawn += picture.pixels[i] != 0 ? 1 : 0;
                    }
                }
                std::printf("drawn: %d pixel(s)\n", drawn);
                if (options.expectAtMost >= 0 && drawn > options.expectAtMost) {
                    std::printf("FAIL expected at most %d drawn pixels -- the eraser should "
                                "have taken some\n", options.expectAtMost);
                    idleBroken = true;
                }
                if (drawn < options.expectDrawn) {
                    std::printf("FAIL expected at least %d drawn pixels -- a drag across "
                                "the canvas should paint all the way\n", options.expectDrawn);
                    idleBroken = true;
                }
            }
            if (options.expectIdle && canvas.frames().compilesThisFrame() != 0) {
                std::printf("FAIL a settled editor compiled %d time(s); every "
                            "frame on screen should already have a texture\n",
                            canvas.frames().compilesThisFrame());
                idleBroken = true;
            }
            running = false;
        }
    }

    // Closing normally is the proof that nothing was lost, so the copies go --
    // every tab's. Anything that stops the program without reaching this line
    // -- a crash, a kill, the power -- leaves them, which is the signal wanted.
    clearAllRecovery(editor);

    editor.files.dialog.destroy();

    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return idleBroken ? 1 : 0;
}
