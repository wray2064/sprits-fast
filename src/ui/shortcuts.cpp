// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

// shortcuts.cpp -- what the keys do.
// See app_internal.h for how the window's files divide.

#include "ui/app_internal.h"

#include "ui/keys.h"
#include "ui/shape_tools.h"
#include "ui/help.h"
#include "ui/slice_tool.h"
#include "ui/tabs.h"
#include "ui/selection_tools.h"
#include "ui/timeline_drawer.h"
#include "ui/user_guide.h"

#include <imgui_internal.h>
#include <imgui_impl_sdlrenderer3.h>




namespace fast {

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
    // A command chosen in Command search is carried out here, once, as if its
    // key had been pressed -- so it does exactly what the key does.
    struct ForgetCommand {
        std::string& command;
        ~ForgetCommand() { command.clear(); }
    } forgetCommand{ editor.runCommand };
    const auto fired = [&keys, &editor](const char* id) {
        return commandPressed(keys, id) || editor.runCommand == id;
    };
    if (fired("app.search")) {
        openCommandSearch(editor);
    }
    if (fired("app.guide")) {
        openUserGuide(editor);
    }
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
    if (fired("file.export-again") && editor.lastExport.kind != DialogResult::Kind::None) {
        exportAgain(editor);
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
            showTimeline(editor);
            editor.say("Duplicated frame");
        }
    }

    // The view.
    if (fired("view.timeline")) { toggleTimeline(editor); }
    if (fired("view.preview")) { editor.preview.visible = !editor.preview.visible; }
    if (fired("view.references")) {
        editor.panels.references = !editor.panels.references;
        ImGui::MarkIniSettingsDirty();
    }
    if (fired("view.reset-layout")) {
        editor.panels = Editor::Panels{};
        editor.panels.resetLayout = true;
        ImGui::MarkIniSettingsDirty();
    }
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


} // namespace fast
