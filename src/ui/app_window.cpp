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

#include "app/animation.h"
#include "app/export_png.h"
#include "app/file_io.h"
#include "app/palette_io.h"
#include "app/shape.h"
#include "app/sheet.h"
#include "app/transform.h"
#include "app/ui_state.h"
#include "ui/editor.h"
#include "ui/panels.h"
#include "ui/theme.h"

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>

#include <SDL3/SDL.h>

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
void openPath(Editor& editor, CanvasView& canvas, const std::string& path) {
    std::string error;
    if (!editor.doc.open(path, &error)) {
        editor.say("Could not open " + fileName(path) + ": " + error);
        editor.files.recent.remove(path);
        editor.files.recent.save();
        return;
    }
    editor.activeLayer = 0;
    forgetInteraction(editor);
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
    syncColorFromLayer(editor);
    canvas.invalidate();
    editor.files.recent.add(path);
    editor.files.recent.save();
    editor.say("Opened " + fileName(path) + ", " +
               std::to_string(editor.layers.size()) + " layer(s)");
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

    std::string error;
    const std::string target = withExtension(path, kFileExtension);
    if (!editor.doc.save(target, &error)) {
        editor.say("Save failed: " + error);
        return false;
    }
    editor.files.recent.add(target);
    editor.files.recent.save();
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

void performAction(Editor& editor, CanvasView& canvas, SDL_Window* window,
                   PendingAction action, const std::string& path) {
    switch (action) {
        case PendingAction::NewDocument:
            newDocument(editor, editor.files.pendingNewSize);
            canvas.frames().clear();          // the old document's textures
            // Fit rather than a fixed zoom: a 128 canvas at 8x does not fit the
            // window, and starting half off-screen is a poor first impression.
            canvas.requestFit();
            canvas.invalidate();
            break;
        case PendingAction::OpenDialog:
            showOpenDialog(editor.files, window, editor.doc);
            break;
        case PendingAction::OpenPath:
            openPath(editor, canvas, path);
            break;
        case PendingAction::Quit:
            editor.quitRequested = true;
            break;
        case PendingAction::None:
            break;
    }
}

// Anything that would discard the document goes through here. With nothing to
// lose it happens at once; otherwise the question is asked and the action waits.
void requestAction(Editor& editor, CanvasView& canvas, SDL_Window* window,
                   PendingAction action, const std::string& path = {}) {
    if (!editor.doc.modified()) {
        performAction(editor, canvas, window, action, path);
        return;
    }
    editor.files.pending = action;
    editor.files.pendingPath = path;
    editor.files.askingToSave = true;
}

// ------------------------------------------------------------------- menu --

void drawMenuBar(Editor& editor, CanvasView& canvas, SDL_Window* window) {
    if (!ImGui::BeginMainMenuBar()) {
        return;
    }
    if (ImGui::BeginMenu("File")) {
        if (ImGui::BeginMenu("New")) {
            const uint32_t sizes[] = { 16, 32, 64, 128 };
            for (uint32_t size : sizes) {
                const std::string label = std::to_string(size) + " x " + std::to_string(size);
                if (ImGui::MenuItem(label.c_str())) {
                    editor.files.pendingNewSize = size;
                    requestAction(editor, canvas, window, PendingAction::NewDocument);
                }
            }
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem("Open...", "Ctrl+O")) {
            requestAction(editor, canvas, window, PendingAction::OpenDialog);
        }
        if (ImGui::BeginMenu("Open recent", !editor.files.recent.empty())) {
            for (const std::string& entry : editor.files.recent.entries()) {
                if (ImGui::MenuItem(fileName(entry).c_str())) {
                    requestAction(editor, canvas, window, PendingAction::OpenPath, entry);
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s", entry.c_str());
                }
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Clear")) {
                editor.files.recent.clear();
                editor.files.recent.save();
            }
            ImGui::EndMenu();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Save", "Ctrl+S")) {
            saveOrAsk(editor, canvas, window);
        }
        if (ImGui::MenuItem("Save as...", "Ctrl+Shift+S")) {
            showSaveAsDialog(editor.files, window, editor.doc);
        }
        if (ImGui::BeginMenu("Export PNG")) {
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
        if (ImGui::MenuItem("Export sheet...", nullptr, false,
                            !editor.frames.empty())) {
            editor.sheetPanelOpen = true;
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Quit", "Ctrl+Q")) {
            requestAction(editor, canvas, window, PendingAction::Quit);
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Edit")) {
        const std::string undo = editor.doc.canUndo()
            ? "Undo " + editor.doc.undoLabel() : std::string("Undo");
        const std::string redo = editor.doc.canRedo()
            ? "Redo " + editor.doc.redoLabel() : std::string("Redo");
        if (ImGui::MenuItem(undo.c_str(), "Ctrl+Z", false, editor.doc.canUndo())) {
            editor.doc.undo();
            resyncLayers(editor);
            syncColorFromLayer(editor);
            canvas.invalidate();
        }
        if (ImGui::MenuItem(redo.c_str(), "Ctrl+Shift+Z", false, editor.doc.canRedo())) {
            editor.doc.redo();
            resyncLayers(editor);
            syncColorFromLayer(editor);
            canvas.invalidate();
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("View")) {
        if (ImGui::MenuItem("Zoom in", "Ctrl+=")) { canvas.setZoom(canvas.zoom() + 1.f); }
        if (ImGui::MenuItem("Zoom out", "Ctrl+-")) { canvas.setZoom(canvas.zoom() - 1.f); }
        if (ImGui::MenuItem("Fit to window", "Ctrl+0")) { canvas.requestFit(); }
        if (ImGui::MenuItem("Reset view")) { canvas.resetView(); }
        ImGui::Separator();
        bool grid = canvas.gridVisible();
        if (ImGui::MenuItem("Pixel grid", nullptr, &grid)) { canvas.setGridVisible(grid); }
        ImGui::MenuItem("Preview", "P", &editor.preview.visible);
        ImGui::EndMenu();
    }
    ImGui::EndMainMenuBar();
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
        openPath(editor, canvas, path);
        return;
    }

    if (kind == DialogResult::Kind::ExportPng) {
        ExportSettings settings;
        settings.scale = static_cast<uint32_t>(editor.exportScale);
        std::string error;
        if (exportSpriteToPng(editor.doc, editor.sprite, path, settings, &error)) {
            editor.say("Exported " + fileName(path) + " at " +
                       std::to_string(editor.exportScale) + "x");
        } else {
            editor.say("Export failed: " + error);
        }
        return;
    }

    if (kind == DialogResult::Kind::ImportPalette) {
        int dropped = 0;
        std::string error;
        if (importPaletteFile(editor.doc, editor.sprite, path, &dropped, &error)) {
            resyncLayers(editor);
            syncColorFromLayer(editor);
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

void handleStroke(Editor& editor, CanvasView& canvas, bool overCanvas, ls::Vec2i pixel) {
    PaintLayer* layer = editor.active();
    if (layer == nullptr) {
        return;
    }

    if (editor.tool == Tool::Picker) {
        if (overCanvas && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            if (const ls::Color* picked = canvas.colorAt(pixel)) {
                fromColor(*picked, editor.color);
                if (picked->a != 0) {
                    setPaintColor(editor.doc, *layer, *picked);
                }
                editor.tool = editor.toolBeforePicker;
                editor.say("Picked the colour under the cursor");
            }
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
        const ls::Vec2f here { static_cast<float>(pixel.x),
                               static_cast<float>(pixel.y) };

        if (overCanvas && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            ShapeParams params;
            params.from = here;
            params.to = { here.x + 1.f, here.y + 1.f };
            params.cornerRadius = editor.shapeCorner;
            if (createShapeLayer(editor.doc, editor.sprite, kind, params,
                                 toColor(editor.color), &editor.pendingShape)) {
                editor.draggingShape = true;
                editor.shapeAnchor = here;
                resyncLayers(editor);
                // Select the shape that was just made, so the panel is showing
                // the thing under the cursor.
                editor.activeLayer = static_cast<int>(editor.layers.size()) - 1;
                canvas.invalidate();
            }
        }

        if (editor.draggingShape && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            ShapeParams params;
            params.from = editor.shapeAnchor;
            params.to = here;
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
        if (overCanvas && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            editor.doc.beginAction("Fill");
            const bool filled = bucketFill(editor.doc, editor.sprite, *layer,
                                           pixel, editor.bucket);
            editor.doc.endAction();
            canvas.invalidate();
            editor.say(filled ? "Filled" : "Nothing to fill there");
        }
        return;
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
    // closes on release rather than per sample.
    if (overCanvas && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        editor.doc.beginAction(editor.tool == Tool::Pencil ? "Pencil" : "Eraser");
        editor.stroking = true;
        editor.lastPixel = pixel;
    }

    if (editor.stroking && ImGui::IsMouseDown(ImGuiMouseButton_Left) && overCanvas) {
        // Interpolate: the mouse reports once a frame, not once a pixel.
        const std::vector<ls::Vec2i> run =
            editor.lastPixel.x < 0 ? std::vector<ls::Vec2i>{pixel}
                                   : linePixels(editor.lastPixel, pixel);
        if (editor.tool == Tool::Pencil) {
            paintPixels(editor.doc, *layer, run);
        } else {
            erasePixels(editor.doc, *layer, run);
        }
        editor.lastPixel = pixel;
        canvas.invalidate();
    }

    if (editor.stroking && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        editor.doc.endAction();
        editor.stroking = false;
        editor.lastPixel = { -1, -1 };
    }
}

void handleShortcuts(Editor& editor, CanvasView& canvas, SDL_Window* window) {
    ImGuiIO& io = ImGui::GetIO();

    // A modal question is on screen, a drag is in progress, or a text field has
    // the keyboard: none of them is a moment to act on a shortcut.
    if (editor.busy() || editor.files.askingToSave || io.WantTextInput) {
        return;
    }

    if (!io.KeyCtrl) {
        // Tool shortcuts, the letters every editor uses.
        if (ImGui::IsKeyPressed(ImGuiKey_B, false)) { editor.tool = Tool::Pencil; }
        if (ImGui::IsKeyPressed(ImGuiKey_E, false)) { editor.tool = Tool::Eraser; }
        if (ImGui::IsKeyPressed(ImGuiKey_G, false)) { editor.tool = Tool::Bucket; }
        if (ImGui::IsKeyPressed(ImGuiKey_I, false)) {
            editor.toolBeforePicker = editor.tool;
            editor.tool = Tool::Picker;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_R, false)) { editor.tool = Tool::Rectangle; }
        if (ImGui::IsKeyPressed(ImGuiKey_U, false)) { editor.tool = Tool::Ellipse; }
        if (ImGui::IsKeyPressed(ImGuiKey_L, false)) { editor.tool = Tool::Line; }
        if (ImGui::IsKeyPressed(ImGuiKey_P, false)) {
            editor.preview.visible = !editor.preview.visible;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_T, false)) {
            editor.timeline.visible = !editor.timeline.visible;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Space, false)) {
            editor.timeline.playing = !editor.timeline.playing;
            editor.timeline.startedAtMs = SDL_GetTicks();
            editor.timeline.visible = true;
        }
        // Comma and full stop step frames -- the keys every animation tool
        // uses, and the ones already under the fingers on a keyboard.
        if (ImGui::IsKeyPressed(ImGuiKey_Comma, true)) {
            editor.timeline.playing = false;
            selectFrame(editor, editor.timeline.activeFrame - 1);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Period, true)) {
            editor.timeline.playing = false;
            selectFrame(editor, editor.timeline.activeFrame + 1);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_O, false)) {
            editor.timeline.onion = !editor.timeline.onion;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_LeftBracket, true)) {
            canvas.setZoom(canvas.zoom() - 1.f);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_RightBracket, true)) {
            canvas.setZoom(canvas.zoom() + 1.f);
        }
        return;
    }

    if (ImGui::IsKeyPressed(ImGuiKey_D, false) && io.KeyShift) {
        const int at = duplicateFrame(editor.doc, editor.timeline.activeFrame);
        if (at >= 0) {
            resyncFrames(editor);
            selectFrame(editor, at);
            editor.timeline.visible = true;
            editor.say("Duplicated frame");
        }
        return;
    }

    if (ImGui::IsKeyPressed(ImGuiKey_Z, false)) {
        const bool moved = io.KeyShift ? editor.doc.redo() : editor.doc.undo();
        if (moved) {
            resyncLayers(editor);
            syncColorFromLayer(editor);
            canvas.invalidate();
        }
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Y, false) && editor.doc.redo()) {
        resyncLayers(editor);
        syncColorFromLayer(editor);
        canvas.invalidate();
    }
    if (ImGui::IsKeyPressed(ImGuiKey_S, false)) {
        if (io.KeyShift) {
            showSaveAsDialog(editor.files, window, editor.doc);
        } else {
            saveOrAsk(editor, canvas, window);
        }
    }
    if (ImGui::IsKeyPressed(ImGuiKey_O, false)) {
        requestAction(editor, canvas, window, PendingAction::OpenDialog);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Q, false)) {
        requestAction(editor, canvas, window, PendingAction::Quit);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Equal, true))  { canvas.setZoom(canvas.zoom() + 1.f); }
    if (ImGui::IsKeyPressed(ImGuiKey_Minus, true))  { canvas.setZoom(canvas.zoom() - 1.f); }
    if (ImGui::IsKeyPressed(ImGuiKey_0, false))     { canvas.requestFit(); }
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

    ImGui::SetNextWindowPos({left + toolbarWidth, top});
    ImGui::SetNextWindowSize({m.sidebarWidth, bodyHeight});
    ImGui::Begin("Tool", nullptr, kPanel);
    drawToolPanel(editor, canvas);
    ImGui::Dummy(ImVec2(0.f, m.sectionGap));
    ImGui::SeparatorText("Palette");
    drawPalettePanel(editor, canvas, window);
    ImGui::End();

    const float rightX = left + viewport->WorkSize.x - m.sidebarWidth;
    // The right column. Shape carries the outline controls, which are the most
    // numerous of the three, so it takes the larger share of what is left after
    // the layer stack.
    const float layersShare = 0.36f;
    const float shapeShare  = 0.36f;
    ImGui::SetNextWindowPos({rightX, top});
    ImGui::SetNextWindowSize({m.sidebarWidth, bodyHeight * layersShare});
    ImGui::Begin("Layers", nullptr, kPanel);
    drawLayerPanel(editor, canvas);
    ImGui::End();

    ImGui::SetNextWindowPos({rightX, top + bodyHeight * layersShare});
    ImGui::SetNextWindowSize({m.sidebarWidth, bodyHeight * shapeShare});
    ImGui::Begin("Shape", nullptr, kPanel);
    drawShapePanel(editor, canvas);
    ImGui::End();

    ImGui::SetNextWindowPos({rightX, top + bodyHeight * (layersShare + shapeShare)});
    ImGui::SetNextWindowSize({m.sidebarWidth,
                              bodyHeight * (1.f - layersShare - shapeShare)});
    ImGui::Begin("Transform", nullptr, kPanel);
    drawTransformPanel(editor, canvas);
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
    ls::Vec2i hovered { -1, -1 };
    // While playing, the canvas shows the frame the clock says rather than the
    // frame being edited. Selection does not move with it -- stopping is what
    // changes which frame the tools act on.
    const int showing = frameToShow(editor, SDL_GetTicks());
    const ls::SpriteId onScreen =
        (showing >= 0 && showing < static_cast<int>(editor.frames.size()))
            ? editor.frames[static_cast<size_t>(showing)].sprite
            : editor.sprite;
    const bool overCanvas = canvas.draw(
        editor.doc, onScreen, &hovered,
        [&editor, &canvas](ImDrawList* draw, ImVec2 origin, float zoom) {
            drawOnionSkin(editor, canvas, draw, origin, zoom);
        });
    editor.hovered = hovered;

    // The preview goes on top of the canvas and takes its clicks first, so a
    // stroke is not started by someone reaching for a backdrop swatch.
    drawPreviewOverlay(editor, canvas);
    const bool overPreview = ImGui::IsAnyItemHovered() || ImGui::IsAnyItemActive();

    // Drawing is refused while playing rather than silently landing on a frame
    // the person is not looking at.
    handleStroke(editor, canvas,
                 overCanvas && !overPreview && !editor.timeline.playing, hovered);
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
        } else if (arg == "--show-sheet-panel") {
            // Opens the export window so a headless capture can show it. Only
            // useful with --frames and --shot.
            options.showSheetPanel = true;
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

    // The palette drives the layer, and the interface reads it back.
    const ls::ColorRole role = 8;
    check(setLayerRole(editor.doc, *editor.active(), role), "use a palette slot");
    check(setPaletteEntry(editor.doc, role, ls::Color{12, 200, 90, 255}), "set the slot");
    syncColorFromLayer(editor);
    check(toColor(editor.color).g == 200, "the control shows the palette colour");

    check(setPaintColor(editor.doc, *editor.active(),
                        effectiveLayerColor(editor.doc, editor.sprite, *editor.active())),
          "keep what is on screen");
    check(setLayerRole(editor.doc, *editor.active(), ls::kColorRoleNone), "detach");
    check(layerRole(editor.doc, *editor.active()) == ls::kColorRoleNone, "detached");

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
            ls::CompileProfile profile;
            profile.type = ls::CompileProfileType::Export;
            profile.outputWidth = 16;
            profile.outputHeight = 16;
            profile.palette = ls::PalettePolicy::Unconstrained;
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

    // Loading a palette recolours the layer the colour control is showing.
    {
        PaintLayer* layer = editor.active();
        check(layer != nullptr, "a layer to colour");
        if (layer != nullptr) {
            editor.doc.beginAction("Use slot 0");
            check(setLayerRole(editor.doc, *layer, 0), "draw through slot 0");
            editor.doc.endAction();
            PaletteFile file;
            std::string parseError;
            check(parsePalette("123456\n", &file, &parseError), "a one-colour file");
            int dropped = 0;
            check(applyPaletteFile(editor.doc, editor.sprite, file, &dropped),
                  "load it");
            resyncLayers(editor);
            syncColorFromLayer(editor);
            const ls::Color shown = toColor(editor.color);
            check(shown.r == 0x12 && shown.g == 0x34 && shown.b == 0x56,
                  "the colour control shows what the layer now draws");
            check(editor.doc.undo(), "undo the load");
            resyncLayers(editor);            // `layer` is stale from here on
            syncColorFromLayer(editor);
            editor.doc.beginAction("Detach");
            check(editor.active() != nullptr &&
                  setLayerRole(editor.doc, *editor.active(), ls::kColorRoleNone),
                  "detach");
            editor.doc.endAction();
        }
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
    check(newDocument(editor, 16), "new document mid-everything");
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
    canvas.resetView();
    check(canvas.zoom() == 8.f, "the view is reset before loading");

    openPath(reopened, canvas, path);
    check(!reopened.layers.empty(), "the reopened file has layers");
    check(canvas.zoom() == 19.f, "zoom comes back");
    check(canvas.panX() == -33.f, "pan x comes back");
    check(canvas.panY() == 21.f, "pan y comes back");
    check(!paletteEntries(reopened.doc).empty(), "the palette came back");

    check(reopened.active() != nullptr, "there is an active layer");
    if (reopened.active() != nullptr) {
        const ls::Color onDisk =
            effectiveLayerColor(reopened.doc, reopened.sprite, *reopened.active());
        const ls::Color inControl = toColor(reopened.color);
        check(onDisk.r == inControl.r && onDisk.g == inControl.g &&
              onDisk.b == inControl.b, "the control matches the loaded layer");
    }

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

    if (failures == 0) {
        std::printf("ui_selftest: all checks passed\n");
        return 0;
    }
    std::printf("ui_selftest: %d check(s) failed\n", failures);
    return 1;
}

} // namespace

int main(int argc, char** argv) {
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
    if (!options.openPath.empty()) {
        openPath(editor, canvas, options.openPath);
    }
    if (options.demoStroke) {
        drawDemoContent(editor);
        canvas.invalidate();
    }
    editor.sheetPanelOpen = options.showSheetPanel;
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
            if (event.type == SDL_EVENT_DROP_FILE && event.drop.data != nullptr) {
                requestAction(editor, canvas, window, PendingAction::OpenPath,
                              event.drop.data);
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

        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
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

        drawMenuBar(editor, canvas, window);
        handleShortcuts(editor, canvas, window);
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

        SDL_RenderPresent(renderer);

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
            if (options.expectIdle && canvas.frames().compilesThisFrame() != 0) {
                std::printf("FAIL a settled editor compiled %d time(s); every "
                            "frame on screen should already have a texture\n",
                            canvas.frames().compilesThisFrame());
                idleBroken = true;
            }
            running = false;
        }
    }

    editor.files.dialog.destroy();

    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return idleBroken ? 1 : 0;
}
