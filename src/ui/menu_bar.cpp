// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

// menu_bar.cpp -- the menu bar.
// See app_internal.h for how the window's files divide.

#include "ui/app_internal.h"

#include "app/guides.h"
#include "app/tracks.h"
#include "app/file_io.h"
#include "ui/keys.h"
#include "ui/layout.h"
#include "app/i18n.h"
#include "ui/os_clipboard.h"
#include "ui/help.h"
#include "ui/start_screen.h"
#include "ui/panels.h"
#include "ui/timeline_drawer.h"

#include <imgui_internal.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace fast {

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
                ImGui::TextDisabled("%s", tr("No settings folder to write to."));
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
        if (ImGui::MenuItem(tr("Start screen"))) {
            openStartScreen(editor);
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
            ImGui::SetTooltip("%s", tr("Write the document somewhere else, and carry on in the "
                              "file you are in -- its name and its unsaved state stay "
                              "as they are."));
        }
        if (ImGui::BeginMenu(tr("Export PNG"))) {
            ImGui::MenuItem(tr("Indexed, the palette as its table"), nullptr, &editor.exportIndexed);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", tr("A colour table and one byte a pixel, with the palette's "
                                  "slots in order as the\nfirst entries -- what a game that "
                                  "swaps palettes at run time wants."));
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
        if (ImGui::MenuItem(tr("Export again"), keysLabel(editor.keys, "file.export-again").c_str(),
                            false, editor.lastExport.kind != DialogResult::Kind::None)) {
            exportAgain(editor);
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("%s", editor.lastExport.kind == DialogResult::Kind::None
                ? tr("Export once first; this then repeats it, to the same file,\n"
                     "with the settings as they are now.")
                : editor.lastExport.path.c_str());
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
            ImGui::SetTooltip("%s", tr("Any angle, pixel-art style (RotSprite): the selection goes "
                              "onto a layer of its own with a rotation the Transform panel "
                              "turns, and can turn again whenever you like."));
        }
        if (ImGui::MenuItem(tr("Scale freely"), nullptr, false, movable)) {
            scaleSelectionFreely(editor);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", tr("The selection onto a layer of its own with handles to size "
                              "it by, and move it by. The drawing is never resampled: any "
                              "size can go back to the original exactly."));
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu(tr("Sprite"))) {
        if (ImGui::MenuItem(tr("New tilemap layer..."))) {
            editor.tilemapDialog.open = true;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", tr("A layer drawn as a grid of tiles: each tile drawn once, "
                              "shown wherever it is placed."));
        }
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
            ImGui::SetTooltip("%s", tr("Crop to the smallest rectangle holding everything any "
                              "frame draws."));
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
                ImGui::SetTooltip("%s", tr("On: layers run through the whole animation, as in "
                                  "Aseprite -- each frame holds its own pixels of each. "
                                  "Turning it on for an older file matches layers by name "
                                  "and loses nothing."));
            }
        }
        if (ImGui::MenuItem(tr("Adjust colours..."), nullptr, false, editor.active() != nullptr)) {
            editor.adjustDialog = Editor::AdjustDialog{};
            editor.adjustDialog.open = true;
            editor.doc.beginAction("Adjust colours");
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", tr("Hue, saturation, lightness, brightness, contrast, invert -- "
                              "of the elements' own colours and, if asked, the palette "
                              "slots they paint through. Nothing is turned into pixels."));
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
            ImGui::SetTooltip("%s", tr("Scale everything to any size, nearest neighbour: every "
                              "frame, every layer, shapes as shapes."));
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
            ImGui::TextDisabled("%s", tr("Keeps one pixel of each block: detail is lost."));
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
            ImGui::SetTooltip("%s", tr("Marquees cover whole tiles, shapes put their corners on "
                              "tile lines, and a moved selection steps from tile to tile. "
                              "The tile grid's size and offset are below."));
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
                ImGui::SetTooltip("%s", tr("Diamonds for isometric tiles: width across, height "
                                  "down, 2:1 by default. Snapping keeps to the square grid."));
            }
            ImGui::SetNextItemWidth(120.f);
            ImGui::InputInt(tr("width"), &tiles.width);
            ImGui::SetNextItemWidth(120.f);
            ImGui::InputInt(tr("height"), &tiles.height);
            ImGui::SetNextItemWidth(120.f);
            ImGui::InputInt(tr("offset x"), &tiles.offsetX);
            ImGui::SetNextItemWidth(120.f);
            ImGui::InputInt(tr("offset y"), &tiles.offsetY);
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
            ImGui::SetTooltip("%s", tr("The canvas drawn again beside itself, so a tile's "
                              "seams show while it is drawn -- and a stroke off one "
                              "edge comes back on the other."));
        }
        ImGui::Separator();
        ImGui::MenuItem(tr("Symmetry across"), nullptr, &editor.symmetryAcross);
        ImGui::MenuItem(tr("Symmetry down"), nullptr, &editor.symmetryDown);
        ImGui::MenuItem(tr("Preview"), "P", &editor.preview.visible);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu(tr("Window"))) {
        const auto toggle = [&editor](const char* name, bool* open, const char* command) {
            const std::string keys = command != nullptr ? keysLabel(editor.keys, command)
                                                        : std::string();
            if (ImGui::MenuItem(tr(name), keys.empty() ? nullptr : keys.c_str(), open)) {
                ImGui::MarkIniSettingsDirty();
            }
        };
        toggle("Tool", &editor.panels.tool, nullptr);
        toggle("Palette", &editor.panels.palette, nullptr);
        toggle("Layers", &editor.panels.layers, nullptr);
        toggle("Elements", &editor.panels.elements, nullptr);
        toggle("Properties", &editor.panels.properties, nullptr);
        toggle("Transform", &editor.panels.transform, nullptr);
        toggle("Puppet", &editor.panels.puppet, nullptr);
        {
            // Shown from here, a Timeline that is a drawer is opened: it was asked for.
            const bool wasShown = editor.timeline.visible;
            toggle("Timeline", &editor.timeline.visible, "view.timeline");
            if (!wasShown && editor.timeline.visible && !editor.timeline.pinned) {
                editor.timeline.drawerOpen = true;
            }
            if (ImGui::MenuItem(tr("Pin the timeline"), nullptr, editor.timeline.pinned)) {
                pinTimeline(editor, !editor.timeline.pinned);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", tr("Docked under the canvas, or a drawer that opens over "
                                           "it when wanted and tucks away."));
            }
        }
        ImGui::Separator();
        toggle("References", &editor.panels.references, "view.references");
        ImGui::MenuItem(tr("Preview"), "P", &editor.preview.visible);
        ImGui::MenuItem(tr("Interface kit"), nullptr, &editor.kitOpen);
        ImGui::Separator();
        // Where everything is: three arrangements the program builds, and the
        // person's own, kept under a name.
        if (ImGui::BeginMenu(tr("Layouts"))) {
            for (LayoutPreset preset : { LayoutPreset::Drawing, LayoutPreset::Animation,
                                         LayoutPreset::Compact }) {
                if (ImGui::MenuItem(tr(presetName(preset)), nullptr,
                                    editor.panels.preset == static_cast<int>(preset))) {
                    useLayoutPreset(editor, preset);
                }
            }
            const std::string folder = layoutsFolderFor(editor);
            const std::vector<SavedLayout> saved = savedLayouts(folder);
            if (!saved.empty()) {
                ImGui::Separator();
                for (const SavedLayout& layout : saved) {
                    if (ImGui::MenuItem(layout.name.c_str())) {
                        useSavedLayout(editor, folder, layout.id);
                    }
                }
            }
            ImGui::Separator();
            if (ImGui::MenuItem(tr("Save current layout..."))) {
                editor.saveLayoutOpen = true;
                editor.layoutName[0] = '\0';
                editor.layoutMessage.clear();
            }
            if (!saved.empty() && ImGui::BeginMenu(tr("Delete a saved layout"))) {
                for (const SavedLayout& layout : saved) {
                    if (ImGui::MenuItem(layout.name.c_str())) {
                        deleteSavedLayout(folder, layout.id);
                    }
                }
                ImGui::EndMenu();
            }
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem(tr("Reset layout"))) {
            editor.panels = Editor::Panels{};
            editor.panels.resetLayout = true;
            editor.timeline.pinned = false;     // the default is a drawer
            editor.timeline.drawerOpen = false;
            editor.timeline.drawerHot = false;
            ImGui::MarkIniSettingsDirty();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", tr("Every panel open and back where it started. A panel "
                              "is moved by dragging its tab: to the side of another "
                              "to dock beside it, onto it to share its place, or "
                              "out over the canvas to float."));
        }
        ImGui::EndMenu();
    }
    if (currentExtension() != nullptr && currentExtension()->menus) {
        currentExtension()->menus(editor, canvas);
    }
    drawHelpMenu(editor);
    ImGui::EndMainMenuBar();
}


} // namespace fast
