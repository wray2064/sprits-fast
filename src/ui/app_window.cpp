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
#include "app/shape.h"
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
        if (ImGui::IsKeyPressed(ImGuiKey_LeftBracket, true)) {
            canvas.setZoom(canvas.zoom() - 1.f);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_RightBracket, true)) {
            canvas.setZoom(canvas.zoom() + 1.f);
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
    drawPalettePanel(editor, canvas);
    ImGui::End();

    const float rightX = left + viewport->WorkSize.x - m.sidebarWidth;
    ImGui::SetNextWindowPos({rightX, top});
    ImGui::SetNextWindowSize({m.sidebarWidth, bodyHeight * 0.42f});
    ImGui::Begin("Layers", nullptr, kPanel);
    drawLayerPanel(editor, canvas);
    ImGui::End();

    ImGui::SetNextWindowPos({rightX, top + bodyHeight * 0.42f});
    ImGui::SetNextWindowSize({m.sidebarWidth, bodyHeight * 0.29f});
    ImGui::Begin("Shape", nullptr, kPanel);
    drawShapePanel(editor, canvas);
    ImGui::End();

    ImGui::SetNextWindowPos({rightX, top + bodyHeight * 0.71f});
    ImGui::SetNextWindowSize({m.sidebarWidth, bodyHeight * 0.29f});
    ImGui::Begin("Transform", nullptr, kPanel);
    drawTransformPanel(editor, canvas);
    ImGui::End();

    const float canvasX = left + toolbarWidth + m.sidebarWidth;
    const float canvasWidth = viewport->WorkSize.x - toolbarWidth - m.sidebarWidth * 2.f;
    ImGui::SetNextWindowPos({canvasX, top});
    ImGui::SetNextWindowSize({canvasWidth, bodyHeight});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.f, 0.f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, theme::palette().canvasBackground);
    ImGui::Begin("##canvas", nullptr,
                 kPanel | ImGuiWindowFlags_NoTitleBar |
                 ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ls::Vec2i hovered { -1, -1 };
    const bool overCanvas = canvas.draw(editor.doc, editor.sprite, &hovered);
    editor.hovered = hovered;

    // The preview goes on top of the canvas and takes its clicks first, so a
    // stroke is not started by someone reaching for a backdrop swatch.
    drawPreviewOverlay(editor, canvas);
    const bool overPreview = ImGui::IsAnyItemHovered() || ImGui::IsAnyItemActive();

    handleStroke(editor, canvas, overCanvas && !overPreview, hovered);
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();

    ImGui::SetNextWindowPos({left, top + bodyHeight});
    ImGui::SetNextWindowSize({viewport->WorkSize.x, statusHeight});
    ImGui::PushStyleColor(ImGuiCol_WindowBg, theme::palette().windowBackground);
    ImGui::Begin("##status", nullptr, kPanel | ImGuiWindowFlags_NoTitleBar);
    drawStatusBar(editor, canvas);
    ImGui::End();
    ImGui::PopStyleColor();

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

    bool running = true;
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
    return 0;
}
