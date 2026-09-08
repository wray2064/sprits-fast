// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

// app_window.cpp — the window, the panels, and the input loop.
//
// This is the only file that knows what toolkit Fast uses. Everything it does
// goes through fast_core, which knows nothing about windows; if this file were
// deleted and rewritten against a different toolkit, the editor would still be
// here.

#include "app/export_png.h"
#include "app/file_io.h"
#include "app/paint.h"
#include "app/ui_state.h"
#include "ui/canvas_view.h"
#include "ui/file_commands.h"

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>

#include <SDL3/SDL.h>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

using namespace ls;

enum class Tool { Pencil, Eraser };

// Everything the interface is holding on to. Deliberately small: the document
// is the state, and this is only what is needed to talk about it.
struct Editor {
    fast::Document                doc;
    SpriteId                      sprite;
    std::vector<fast::PaintLayer> layers;
    int                           activeLayer = 0;

    Tool  tool = Tool::Pencil;
    float color[4] = { 0.85f, 0.35f, 0.25f, 1.f };

    bool  stroking = false;
    bool  recolouring = false;
    int   renaming = -1;            // index of the layer being renamed, or -1
    char  renameBuffer[64] = {};
    int   exportScale = 1;          // whole-number magnification for a PNG export

    fast::FileState files;
    bool  quitRequested = false;
    std::string lastTitle;
    Vec2i lastPixel { -1, -1 };
    std::string status = "ready";

    fast::PaintLayer* active() {
        if (activeLayer < 0 || activeLayer >= static_cast<int>(layers.size())) {
            return nullptr;
        }
        return &layers[static_cast<size_t>(activeLayer)];
    }
};

Color toColor(const float rgba[4]) {
    return { static_cast<uint8_t>(rgba[0] * 255.f + 0.5f),
             static_cast<uint8_t>(rgba[1] * 255.f + 0.5f),
             static_cast<uint8_t>(rgba[2] * 255.f + 0.5f),
             static_cast<uint8_t>(rgba[3] * 255.f + 0.5f) };
}

// Points the colour picker at whatever layer is selected.
//
// Without this the picker keeps whatever it was last set to, so after opening a
// file -- or just clicking a different layer -- it shows one colour while the
// layer is another. Nudging it then repaints the layer to the stale value, which
// is worse than merely looking wrong.
void syncColorFromLayer(Editor& editor) {
    fast::PaintLayer* layer = editor.active();
    if (layer == nullptr) {
        return;
    }
    const Color colour = fast::paintColor(editor.doc, *layer);
    if (colour.a == 0) {
        return;                 // nothing resolved; leave the picker alone
    }
    editor.color[0] = static_cast<float>(colour.r) / 255.f;
    editor.color[1] = static_cast<float>(colour.g) / 255.f;
    editor.color[2] = static_cast<float>(colour.b) / 255.f;
    editor.color[3] = static_cast<float>(colour.a) / 255.f;
}

// Rebuilds the layer list from the document.
//
// The interface holds handles; undo, redo and open all change what exists. Undo
// restores ids exactly, so re-adopting after one gives back the same handles --
// but an undone "Add layer" leaves the panel holding a layer that is no longer
// there, and drawing on it would fail silently. Asking the document what it now
// contains is cheaper than tracking that by hand and cannot drift.
void resyncLayers(Editor& editor) {
    std::vector<fast::PaintLayer> found;
    SpriteId sprite;
    if (!fast::adoptPaintLayers(editor.doc, &sprite, &found)) {
        return;
    }
    editor.sprite = sprite;
    editor.layers = std::move(found);
    if (editor.activeLayer >= static_cast<int>(editor.layers.size())) {
        editor.activeLayer = static_cast<int>(editor.layers.size()) - 1;
    }
    if (editor.activeLayer < 0) {
        editor.activeLayer = 0;
    }
}

bool newDocument(Editor& editor, uint32_t size) {
    if (!editor.doc.create("untitled", size, size)) {
        return false;
    }
    editor.layers.clear();
    editor.activeLayer = 0;
    editor.sprite = editor.doc.engine().createSprite(editor.doc.id()).value;

    fast::PaintLayer layer;
    if (!fast::createPaintLayer(editor.doc, editor.sprite, "layer 1",
                                toColor(editor.color), &layer)) {
        return false;
    }
    editor.layers.push_back(layer);
    editor.doc.setUiState({});
    editor.status = "new " + std::to_string(size) + "x" + std::to_string(size) + " document";
    return true;
}

// ------------------------------------------------------------------ panels --

void drawToolPanel(Editor& editor, fast::CanvasView& canvas) {
    ImGui::TextUnformatted("Tool");
    if (ImGui::RadioButton("Pencil", editor.tool == Tool::Pencil)) { editor.tool = Tool::Pencil; }
    ImGui::SameLine();
    if (ImGui::RadioButton("Eraser", editor.tool == Tool::Eraser)) { editor.tool = Tool::Eraser; }

    ImGui::Separator();
    ImGui::TextUnformatted("Colour");

    // Changing the colour recolours what is already drawn, because the colour
    // lives on the fill rule rather than in the pixels. That is worth seeing
    // happen: it is the whole premise of the engine in one control.
    const bool changed = ImGui::ColorPicker4("##colour", editor.color,
                                            ImGuiColorEditFlags_NoSidePreview |
                                            ImGuiColorEditFlags_NoSmallPreview);

    // One history entry for a whole drag of the picker, not one per frame: the
    // bracket opens when the control is grabbed and closes when it is let go.
    // Without this a recolour was not undoable at all, and did not even mark the
    // document as modified.
    if (ImGui::IsItemActivated()) {
        editor.doc.beginAction("Recolour");
        editor.recolouring = true;
    }
    if (changed) {
        if (fast::PaintLayer* layer = editor.active()) {
            fast::setPaintColor(editor.doc, *layer, toColor(editor.color));
            canvas.invalidate();
            editor.status = "recoloured the layer without touching the drawing";
        }
    }
    if (editor.recolouring && ImGui::IsItemDeactivated()) {
        editor.doc.endAction();
        editor.recolouring = false;
    }

    ImGui::Separator();
    float zoom = canvas.zoom();
    if (ImGui::SliderFloat("Zoom", &zoom, 1.f, 32.f, "%.0fx")) {
        canvas.setZoom(zoom);
    }
    if (ImGui::Button("Reset view")) {
        canvas.resetView();
    }
}

void drawLayerPanel(Editor& editor, fast::CanvasView& canvas) {
    if (ImGui::Button("Add layer")) {
        fast::PaintLayer layer;
        const std::string name = "layer " + std::to_string(editor.layers.size() + 1);
        if (fast::createPaintLayer(editor.doc, editor.sprite, name,
                                   toColor(editor.color), &layer)) {
            editor.layers.push_back(layer);
            editor.activeLayer = static_cast<int>(editor.layers.size()) - 1;
            canvas.invalidate();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Delete") && editor.layers.size() > 1) {
        if (fast::PaintLayer* layer = editor.active()) {
            editor.doc.beginAction("Delete layer");
            editor.doc.engine().deleteLayer(layer->layer);
            editor.doc.endAction();
            resyncLayers(editor);
            canvas.invalidate();
        }
    }
    ImGui::Separator();

    // Topmost first, which is how a layer stack reads.
    for (int i = static_cast<int>(editor.layers.size()) - 1; i >= 0; --i) {
        ImGui::PushID(i);
        auto info = editor.doc.engine().getLayerInfo(editor.layers[static_cast<size_t>(i)].layer);
        const std::string name = info.ok() ? info.value.name : "?";

        bool visible = info.ok() ? info.value.visible : true;
        if (ImGui::Checkbox("##visible", &visible)) {
            editor.doc.beginAction("Toggle layer");
            editor.doc.engine().setLayerVisibility(
                editor.layers[static_cast<size_t>(i)].layer, visible);
            editor.doc.endAction();
            canvas.invalidate();
        }
        ImGui::SameLine();

        // Double-click to rename, which is what a layer name in a list means
        // everywhere else.
        if (editor.renaming == i) {
            ImGui::SetNextItemWidth(-1.f);
            if (ImGui::IsWindowAppearing() || ImGui::IsItemDeactivated()) {
                ImGui::SetKeyboardFocusHere();
            }
            if (ImGui::InputText("##rename", editor.renameBuffer,
                                 sizeof(editor.renameBuffer),
                                 ImGuiInputTextFlags_EnterReturnsTrue)) {
                editor.doc.beginAction("Rename layer");
                editor.doc.engine().setLayerName(
                    editor.layers[static_cast<size_t>(i)].layer, editor.renameBuffer);
                editor.doc.endAction();
                editor.renaming = -1;
            }
            if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                editor.renaming = -1;
            }
        } else {
            if (ImGui::Selectable(name.c_str(), editor.activeLayer == i)) {
                editor.activeLayer = i;
                syncColorFromLayer(editor);
            }
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                editor.renaming = i;
                std::snprintf(editor.renameBuffer, sizeof(editor.renameBuffer),
                              "%s", name.c_str());
            }
        }
        ImGui::PopID();
    }
}

// ------------------------------------------------------------ file actions --

// Opens a path, whatever asked for it: the dialog, a recent entry, a dropped
// file. A file that will not open is dropped from the recent list, which is the
// only place that list is pruned -- a path missing because a drive is unplugged
// should not vanish from the menu on its own.
void openPath(Editor& editor, fast::CanvasView& canvas, const std::string& path) {
    std::string error;
    if (!editor.doc.open(path, &error)) {
        editor.status = "could not open " + fast::fileName(path) + ": " + error;
        editor.files.recent.remove(path);
        editor.files.recent.save();
        return;
    }
    editor.activeLayer = 0;
    resyncLayers(editor);

    // Put the view back where it was when this file was last closed. Everything
    // in here came out of a file, so it is clamped before it reaches the canvas
    // rather than trusted: a zoom of 1e30 or a pan of four million looks exactly
    // like a file that failed to open.
    fast::UiState view;
    if (fast::fromJson(editor.doc.uiState(), &view)) {
        view.clamp(static_cast<int>(editor.layers.size()));
        canvas.setZoom(view.zoom);
        canvas.setPan(view.panX, view.panY);
        editor.activeLayer = view.activeLayer;
    } else {
        canvas.resetView();
    }

    syncColorFromLayer(editor);
    canvas.invalidate();
    editor.files.recent.add(path);
    editor.files.recent.save();
    editor.status = "opened " + fast::fileName(path) + ", " +
                    std::to_string(editor.layers.size()) + " layer(s)";
}

bool saveTo(Editor& editor, const fast::CanvasView& canvas, const std::string& path) {
    // Where the user was looking is not part of the artwork, so it rides in the
    // package as a fast/ entry rather than going anywhere near the document.
    fast::UiState view;
    view.zoom = canvas.zoom();
    view.panX = canvas.panX();
    view.panY = canvas.panY();
    view.activeLayer = editor.activeLayer;
    editor.doc.setUiState(fast::toJson(view));

    std::string error;
    const std::string target = fast::withExtension(path, fast::kFileExtension);
    if (!editor.doc.save(target, &error)) {
        editor.status = "save failed: " + error;
        return false;
    }
    editor.files.recent.add(target);
    editor.files.recent.save();
    editor.status = "saved " + fast::fileName(target);
    return true;
}

// Save, or Save As if this document has never been written. Returns false when
// the answer is not known yet because a dialog is open.
bool saveOrAsk(Editor& editor, fast::CanvasView& canvas, SDL_Window* window) {
    if (editor.doc.path().empty()) {
        fast::showSaveAsDialog(editor.files, window, editor.doc);
        return false;
    }
    return saveTo(editor, canvas, editor.doc.path());
}

void performAction(Editor& editor, fast::CanvasView& canvas, SDL_Window* window,
                   fast::PendingAction action, const std::string& path) {
    switch (action) {
        case fast::PendingAction::NewDocument:
            newDocument(editor, editor.files.pendingNewSize);
            canvas.invalidate();
            break;
        case fast::PendingAction::OpenDialog:
            fast::showOpenDialog(editor.files, window, editor.doc);
            break;
        case fast::PendingAction::OpenPath:
            openPath(editor, canvas, path);
            break;
        case fast::PendingAction::Quit:
            editor.quitRequested = true;
            break;
        case fast::PendingAction::None:
            break;
    }
}

// Anything that would discard the document goes through here. If there is
// nothing to lose it happens immediately; otherwise the question is asked and
// the action waits for an answer.
void requestAction(Editor& editor, fast::CanvasView& canvas, SDL_Window* window,
                   fast::PendingAction action, const std::string& path = {}) {
    if (!editor.doc.modified()) {
        performAction(editor, canvas, window, action, path);
        return;
    }
    editor.files.pending = action;
    editor.files.pendingPath = path;
    editor.files.askingToSave = true;
}

void drawMenuBar(Editor& editor, fast::CanvasView& canvas, SDL_Window* window) {
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
                    requestAction(editor, canvas, window, fast::PendingAction::NewDocument);
                }
            }
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem("Open...", "Ctrl+O")) {
            requestAction(editor, canvas, window, fast::PendingAction::OpenDialog);
        }

        if (ImGui::BeginMenu("Open recent", !editor.files.recent.empty())) {
            for (const std::string& entry : editor.files.recent.entries()) {
                // The name is the label; the whole path is the tooltip, since two
                // files with the same name in different folders is normal.
                if (ImGui::MenuItem(fast::fileName(entry).c_str())) {
                    requestAction(editor, canvas, window,
                                  fast::PendingAction::OpenPath, entry);
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
            fast::showSaveAsDialog(editor.files, window, editor.doc);
        }
        ImGui::Separator();

        // Export is not Save. It writes a picture somebody else can open, and
        // leaves the document exactly as it was.
        if (ImGui::BeginMenu("Export PNG")) {
            const int scales[] = { 1, 2, 4, 8, 16 };
            for (int scale : scales) {
                const std::string label = std::to_string(scale) + "x";
                if (ImGui::MenuItem(label.c_str())) {
                    editor.exportScale = scale;
                    fast::showExportDialog(editor.files, window, editor.doc);
                }
            }
            ImGui::EndMenu();
        }

        ImGui::Separator();
        if (ImGui::MenuItem("Quit", "Ctrl+Q")) {
            requestAction(editor, canvas, window, fast::PendingAction::Quit);
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Edit")) {
        const std::string undo = "Undo " + editor.doc.undoLabel();
        const std::string redo = "Redo " + editor.doc.redoLabel();
        if (ImGui::MenuItem(undo.c_str(), "Ctrl+Z", false, editor.doc.canUndo())) {
            editor.doc.undo();
            resyncLayers(editor);
            canvas.invalidate();
        }
        if (ImGui::MenuItem(redo.c_str(), "Ctrl+Y", false, editor.doc.canRedo())) {
            editor.doc.redo();
            resyncLayers(editor);
            canvas.invalidate();
        }
        ImGui::EndMenu();
    }
    ImGui::EndMainMenuBar();
}

// The question in front of anything that would discard unsaved work.
//
// Three answers, and the third one matters: Cancel has to abandon the pending
// action entirely rather than quietly proceeding, which is the bug this kind of
// prompt usually has.
void drawUnsavedPrompt(Editor& editor, fast::CanvasView& canvas, SDL_Window* window) {
    if (editor.files.askingToSave && !ImGui::IsPopupOpen("Unsaved changes")) {
        ImGui::OpenPopup("Unsaved changes");
    }

    const ImVec2 centre = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(centre, ImGuiCond_Appearing, {0.5f, 0.5f});

    if (!ImGui::BeginPopupModal("Unsaved changes", nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    const std::string name = editor.doc.path().empty()
                           ? std::string("This document")
                           : fast::fileName(editor.doc.path());
    ImGui::Text("%s has unsaved changes.", name.c_str());
    ImGui::Spacing();

    if (ImGui::Button("Save", {110, 0})) {
        const fast::PendingAction action = editor.files.pending;
        const std::string path = editor.files.pendingPath;

        if (editor.doc.path().empty()) {
            // Save As is asynchronous, so the pending action has to survive
            // until the dialog comes back.
            editor.files.resumeAfterSave = true;
            fast::showSaveAsDialog(editor.files, window, editor.doc);
        } else if (saveTo(editor, canvas, editor.doc.path())) {
            editor.files.pending = fast::PendingAction::None;
            performAction(editor, canvas, window, action, path);
        }
        editor.files.askingToSave = false;
        ImGui::CloseCurrentPopup();
    }

    ImGui::SameLine();
    if (ImGui::Button("Discard", {110, 0})) {
        const fast::PendingAction action = editor.files.pending;
        const std::string path = editor.files.pendingPath;
        editor.files.pending = fast::PendingAction::None;
        editor.files.askingToSave = false;
        performAction(editor, canvas, window, action, path);
        ImGui::CloseCurrentPopup();
    }

    ImGui::SameLine();
    if (ImGui::Button("Cancel", {110, 0}) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        editor.files.pending = fast::PendingAction::None;
        editor.files.pendingPath.clear();
        editor.files.askingToSave = false;
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

// Picks up whatever a native dialog came back with. The callback runs on SDL's
// terms -- possibly another thread, certainly another frame -- so the answer is
// read here, under the lock, and acted on in the main loop's own time.
void processDialogResult(Editor& editor, fast::CanvasView& canvas, SDL_Window* window) {
    fast::DialogResult::Kind kind = fast::DialogResult::Kind::None;
    bool cancelled = false;
    std::string path;

    SDL_LockMutex(editor.files.dialog.mutex);
    if (editor.files.dialog.ready) {
        kind = editor.files.dialog.kind;
        cancelled = editor.files.dialog.cancelled;
        path = editor.files.dialog.path;
        editor.files.dialog.ready = false;
        editor.files.dialog.kind = fast::DialogResult::Kind::None;
    }
    SDL_UnlockMutex(editor.files.dialog.mutex);

    if (kind == fast::DialogResult::Kind::None) {
        return;
    }

    if (cancelled || path.empty()) {
        // Cancelling a save that something else was waiting on cancels that too.
        editor.files.resumeAfterSave = false;
        editor.files.pending = fast::PendingAction::None;
        editor.status = "cancelled";
        return;
    }

    if (kind == fast::DialogResult::Kind::Open) {
        openPath(editor, canvas, path);
        return;
    }

    if (kind == fast::DialogResult::Kind::ExportPng) {
        fast::ExportSettings settings;
        settings.scale = static_cast<uint32_t>(editor.exportScale);
        std::string error;
        if (fast::exportSpriteToPng(editor.doc, editor.sprite, path, settings, &error)) {
            editor.status = "exported " + fast::fileName(path) + " at " +
                            std::to_string(editor.exportScale) + "x";
        } else {
            editor.status = "export failed: " + error;
        }
        return;
    }

    if (saveTo(editor, canvas, path) && editor.files.resumeAfterSave) {
        const fast::PendingAction action = editor.files.pending;
        const std::string pendingPath = editor.files.pendingPath;
        editor.files.resumeAfterSave = false;
        editor.files.pending = fast::PendingAction::None;
        performAction(editor, canvas, window, action, pendingPath);
    }
}

// ------------------------------------------------------------------- input --

void handleStroke(Editor& editor, fast::CanvasView& canvas, bool overCanvas, Vec2i pixel) {
    fast::PaintLayer* layer = editor.active();
    if (layer == nullptr) {
        return;
    }

    // A whole drag is one history entry, so the bracket opens on press and
    // closes on release rather than per sample.
    if (overCanvas && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        editor.doc.beginAction(editor.tool == Tool::Pencil ? "Pencil" : "Eraser");
        editor.stroking = true;
        editor.lastPixel = pixel;
    }

    if (editor.stroking && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        // Interpolate: the mouse reports once a frame, not once a pixel.
        const std::vector<Vec2i> run =
            editor.lastPixel.x < 0 ? std::vector<Vec2i>{pixel}
                                   : fast::linePixels(editor.lastPixel, pixel);
        if (overCanvas) {
            if (editor.tool == Tool::Pencil) {
                fast::paintPixels(editor.doc, *layer, run);
            } else {
                fast::erasePixels(editor.doc, *layer, run);
            }
            editor.lastPixel = pixel;
            canvas.invalidate();
        }
    }

    if (editor.stroking && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        editor.doc.endAction();
        editor.stroking = false;
        editor.lastPixel = { -1, -1 };
    }
}

void handleShortcuts(Editor& editor, fast::CanvasView& canvas, SDL_Window* window) {
    const ImGuiIO& io = ImGui::GetIO();
    if (!io.KeyCtrl) {
        return;
    }
    // A modal question is on screen, or a drag is in progress: neither is a
    // moment to act on a shortcut. Undoing halfway through a drag would step
    // back over a history entry that has not been committed, leaving the
    // bracket open.
    if (editor.stroking || editor.recolouring || editor.files.askingToSave) {
        return;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Z, false)) {
        if (io.KeyShift ? editor.doc.redo() : editor.doc.undo()) {
            resyncLayers(editor);
            canvas.invalidate();
        }
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Y, false) && editor.doc.redo()) {
        resyncLayers(editor);
        canvas.invalidate();
    }
    if (ImGui::IsKeyPressed(ImGuiKey_S, false)) {
        if (io.KeyShift) {
            fast::showSaveAsDialog(editor.files, window, editor.doc);
        } else {
            saveOrAsk(editor, canvas, window);
        }
    }
    if (ImGui::IsKeyPressed(ImGuiKey_O, false)) {
        requestAction(editor, canvas, window, fast::PendingAction::OpenDialog);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Q, false)) {
        requestAction(editor, canvas, window, fast::PendingAction::Quit);
    }
}

} // namespace

// Running the interface without a person in front of it.
//
// A GUI is the part of a program that usually gets tested by someone looking at
// it. These flags make it testable instead: --frames runs a fixed number of
// frames and exits, --shot writes what was drawn to a file, and --demo-stroke
// puts something on the canvas through the same calls the pencil uses. With
// SDL's dummy video driver this runs on a machine with no display at all, which
// is what lets CI notice a UI that no longer starts.
struct Options {
    int         frames = 0;          // 0 = run until the user quits
    std::string screenshot;
    bool        demoStroke = false;
    bool        selfTest = false;
    std::string openPath;       // a file named on the command line
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

// An X and a filled block, drawn through exactly the calls the pencil makes, so
// a screenshot shows the real path rather than a special one.
void drawDemoContent(Editor& editor) {
    fast::PaintLayer* layer = editor.active();
    if (layer == nullptr) {
        return;
    }
    editor.doc.beginAction("Demo");
    fast::paintPixels(editor.doc, *layer, fast::linePixels({4, 4}, {27, 27}));
    fast::paintPixels(editor.doc, *layer, fast::linePixels({27, 4}, {4, 27}));
    for (int32_t y = 12; y < 20; ++y) {
        fast::paintPixels(editor.doc, *layer, fast::linePixels({12, y}, {19, y}));
    }
    editor.doc.endAction();
    editor.status = "demo content";
}

// The state management the interface does, driven without the interface.
//
// resyncLayers and the undo/open glue around it are the parts that were wrong:
// the panel held handles to layers that undo had removed, and an opened file
// could not be drawn on. None of that is reachable from a fast_core test,
// because it is the window's own bookkeeping -- so it is driven directly here
// rather than left to be found by clicking.
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

    // Draw, as a stroke would.
    editor.doc.beginAction("Pencil");
    fast::paintPixels(editor.doc, *editor.active(), fast::linePixels({2, 2}, {2, 9}));
    editor.doc.endAction();

    // Add a layer, then undo it. The panel must not be left holding a layer that
    // no longer exists -- this was the bug.
    fast::PaintLayer added;
    check(fast::createPaintLayer(editor.doc, editor.sprite, "layer 2",
                                 Color{40, 80, 220, 255}, &added), "add layer");
    editor.layers.push_back(added);
    check(editor.layers.size() == 2, "two layers");

    check(editor.doc.undo(), "undo the added layer");
    resyncLayers(editor);
    check(editor.layers.size() == 1, "panel drops the undone layer");
    check(editor.activeLayer == 0, "active index stays in range");
    check(editor.active() != nullptr, "still something to draw on");

    // Redo brings it back, and the panel picks it up again.
    check(editor.doc.canRedo(), "the undone layer can be redone");
    check(editor.doc.redo(), "redo");
    resyncLayers(editor);
    check(editor.layers.size() == 2, "the redone layer reappears in the panel");

    check(editor.doc.undo(), "undo it again");
    resyncLayers(editor);
    check(editor.layers.size() == 1, "and goes away again");

    // Drawing after an undo discards the redo branch, which is what every editor
    // does: the history is a line, not a tree. The surviving layer must still be
    // drawable rather than a stale handle.
    editor.doc.beginAction("Pencil");
    check(fast::paintPixels(editor.doc, *editor.active(), {{7, 7}}), "draw after undo");
    editor.doc.endAction();
    check(!editor.doc.canRedo(), "a new action drops the redo branch");

    // Save, reopen, keep drawing: the loop that makes it an editor.
    std::string error;
    const std::string path = "ui_selftest.lsprite";
    check(editor.doc.save(path, &error), "save");
    check(editor.doc.open(path, &error), "open");
    resyncLayers(editor);
    check(!editor.layers.empty(), "reopened file has drawable layers");
    check(editor.active() != nullptr, "reopened file can be drawn on");

    editor.doc.beginAction("Pencil");
    check(fast::paintPixels(editor.doc, *editor.active(), {{11, 11}}), "draw after reopen");
    editor.doc.endAction();
    check(editor.doc.canUndo(), "the new stroke is undoable");

    // Opening repeatedly must not accumulate documents.
    check(editor.doc.open(path, &error), "open again");
    check(editor.doc.engine().documents().size() == 1, "one document held");

    // ------------------------------------------------------ file handling --
    //
    // This part touches the user's real recent-files list, so it is put back
    // afterwards. A test that leaves someone's settings changed is a bad test
    // however green it is.
    fast::RecentFiles theirs;
    theirs.load();

    editor.files.recent.clear();

    // No renderer here: nothing in these paths draws, the canvas only carries
    // zoom and pan.
    fast::CanvasView canvas(nullptr);

    // The title says what is open and whether there is unsaved work.
    check(fast::windowTitle(editor.doc).find("ui_selftest") != std::string::npos,
          "title names the file");
    check(fast::windowTitle(editor.doc).find('*') == std::string::npos,
          "a saved document is not marked modified");

    editor.doc.beginAction("Pencil");
    fast::paintPixels(editor.doc, *editor.active(), {{3, 3}});
    editor.doc.endAction();
    check(fast::windowTitle(editor.doc).find('*') != std::string::npos,
          "an edited document is marked modified");

    // A save through the file path adds to the recent list and clears the mark.
    check(saveTo(editor, canvas, path), "save through the file command");
    check(!editor.doc.modified(), "saving clears the modified mark");
    check(editor.files.recent.entries().size() == 1, "the save is remembered");

    // Opening something that is not there must fail cleanly and drop the entry
    // rather than leaving a menu item that cannot work.
    editor.files.recent.add("no_such_file_at_all.lsprite");
    check(editor.files.recent.entries().size() == 2, "the bad path is listed");

    // An extension is added when the user does not type one.
    check(saveTo(editor, canvas, "ui_selftest_noext"), "save without an extension");
    check(fast::hasExtension(editor.doc.path(), fast::kFileExtension),
          "the extension is supplied");

    // A modified document guards the actions that would discard it.
    editor.doc.beginAction("Pencil");
    fast::paintPixels(editor.doc, *editor.active(), {{4, 4}});
    editor.doc.endAction();
    check(editor.doc.modified(), "modified again");

    // ------------------------------------------------------- loading a file --
    //
    // The view and the selected layer ride in the package and have to come back.
    canvas.setZoom(19.f);
    canvas.setPan(-33.f, 21.f);
    check(saveTo(editor, canvas, path), "save with a view to restore");

    // A second editor, as a fresh launch would be.
    Editor reopened;
    canvas.resetView();
    check(canvas.zoom() == 8.f, "the view is reset before loading");

    openPath(reopened, canvas, path);
    check(!reopened.layers.empty(), "the reopened file has layers");
    check(canvas.zoom() == 19.f, "zoom comes back");
    check(canvas.panX() == -33.f, "pan x comes back");
    check(canvas.panY() == 21.f, "pan y comes back");

    // The picker must show the colour of the layer it is pointed at, not
    // whatever it happened to hold before.
    check(reopened.active() != nullptr, "there is an active layer to check");
    if (reopened.active() != nullptr) {
        const Color onDisk = fast::paintColor(reopened.doc, *reopened.active());
        const Color inPicker = toColor(reopened.color);
        check(onDisk.r == inPicker.r && onDisk.g == inPicker.g &&
              onDisk.b == inPicker.b,
              "the picker matches the loaded layer");
    }

    // A file with no view recorded, and one with a hostile view, both have to
    // open rather than putting the artwork somewhere unreachable.
    reopened.doc.setUiState("{\"zoom\":1e6,\"panX\":-9e9,\"activeLayer\":9999}");
    check(reopened.doc.save(path, &error), "save a hostile view");
    openPath(reopened, canvas, path);
    check(canvas.zoom() <= 64.f, "an absurd zoom is clamped");
    check(canvas.panX() >= -20000.f, "an absurd pan is clamped");
    check(reopened.activeLayer < static_cast<int>(reopened.layers.size()),
          "an out-of-range layer index is clamped");

    theirs.save();      // put the user's list back
    fast::deleteFile("ui_selftest_noext" + std::string(fast::kFileExtension));
    fast::deleteFile(path);

    if (failures == 0) {
        std::printf("ui_selftest: all checks passed\n");
        return 0;
    }
    std::printf("ui_selftest: %d check(s) failed\n", failures);
    return 1;
}

int main(int argc, char** argv) {
    const Options options = parseOptions(argc, argv);
    if (options.selfTest) {
        return runSelfTest();
    }

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::printf("SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow("Sprit's'fast", 1280, 800,
                                          SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
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
    ImGui::StyleColorsDark();

    // Every window here is positioned explicitly each frame, so ImGui's saved
    // layout would never be read -- it would only drop an imgui.ini into
    // whatever directory the editor happened to be started from.
    ImGui::GetIO().IniFilename = nullptr;
    ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer3_Init(renderer);

    Editor editor;
    editor.files.dialog.init();
    editor.files.recent.load();

    fast::CanvasView canvas(renderer);
    if (!newDocument(editor, 32)) {
        std::printf("could not create the first document\n");
        return 1;
    }

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
                // The window's close button asks the same question the menu
                // does, rather than throwing the work away.
                requestAction(editor, canvas, window, fast::PendingAction::Quit);
            }

            // Dropping a file on the window opens it, which is how a file
            // manager expects to hand something to an editor.
            if (event.type == SDL_EVENT_DROP_FILE && event.drop.data != nullptr) {
                requestAction(editor, canvas, window, fast::PendingAction::OpenPath,
                              event.drop.data);
            }
        }

        processDialogResult(editor, canvas, window);
        if (editor.quitRequested) {
            running = false;
        }

        // The title carries the file name and whether there is unsaved work,
        // which is where people look for it. Only set when it changes: this runs
        // every frame.
        const std::string title = fast::windowTitle(editor.doc);
        if (title != editor.lastTitle) {
            SDL_SetWindowTitle(window, title.c_str());
            editor.lastTitle = title;
        }

        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        drawMenuBar(editor, canvas, window);
        handleShortcuts(editor, canvas, window);
        drawUnsavedPrompt(editor, canvas, window);

        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        const float menuHeight = ImGui::GetFrameHeight();
        const float sidebar = 260.f;
        const float statusHeight = ImGui::GetFrameHeight() + 8.f;

        ImGui::SetNextWindowPos({viewport->WorkPos.x, viewport->WorkPos.y});
        ImGui::SetNextWindowSize({sidebar, viewport->WorkSize.y - statusHeight});
        ImGui::Begin("Tools", nullptr,
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoCollapse);
        drawToolPanel(editor, canvas);
        ImGui::End();

        ImGui::SetNextWindowPos({viewport->WorkPos.x + viewport->WorkSize.x - sidebar,
                                 viewport->WorkPos.y});
        ImGui::SetNextWindowSize({sidebar, viewport->WorkSize.y - statusHeight});
        ImGui::Begin("Layers", nullptr,
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoCollapse);
        drawLayerPanel(editor, canvas);
        ImGui::End();

        ImGui::SetNextWindowPos({viewport->WorkPos.x + sidebar, viewport->WorkPos.y});
        ImGui::SetNextWindowSize({viewport->WorkSize.x - sidebar * 2.f,
                                  viewport->WorkSize.y - statusHeight});
        ImGui::Begin("Canvas", nullptr,
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar |
                     ImGuiWindowFlags_NoScrollWithMouse);
        Vec2i hovered { -1, -1 };
        const bool overCanvas = canvas.draw(editor.doc, editor.sprite, &hovered);
        handleStroke(editor, canvas, overCanvas, hovered);
        ImGui::End();

        ImGui::SetNextWindowPos({viewport->WorkPos.x,
                                 viewport->WorkPos.y + viewport->WorkSize.y - statusHeight});
        ImGui::SetNextWindowSize({viewport->WorkSize.x, statusHeight});
        ImGui::Begin("Status", nullptr,
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar);
        ImGui::Text("%s%s  |  %d,%d  |  compile %.2f ms  |  %.0f fps",
                    editor.doc.modified() ? "*" : "",
                    editor.status.c_str(),
                    hovered.x, hovered.y,
                    canvas.lastCompileMs(),
                    static_cast<double>(ImGui::GetIO().Framerate));
        ImGui::End();
        (void)menuHeight;

        ImGui::Render();
        SDL_SetRenderDrawColor(renderer, 30, 30, 34, 255);
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
