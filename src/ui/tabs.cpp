// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

#include "ui/tabs.h"

#include "app/file_io.h"
#include "ui/file_commands.h"
#include "ui/selection_tools.h"

#include <SDL3/SDL.h>
#include <imgui.h>

#include <utility>

namespace fast {

namespace {

// Moves everything the editor holds about its document into `tab`, and what
// `tab` held into the editor. The tab's id and its view stay where they are.
void exchange(Editor& editor, DocumentTab& tab) {
    editor.doc.swap(tab.doc);
    std::swap(editor.sprite, tab.sprite);
    std::swap(editor.layers, tab.layers);
    std::swap(editor.activeLayer, tab.activeLayer);
    std::swap(editor.selection, tab.selection);
    std::swap(editor.selectedLayers, tab.selectedLayers);
    std::swap(editor.activeGroup, tab.activeGroup);
    std::swap(editor.collapsedGroups, tab.collapsedGroups);
    std::swap(editor.clipboard, tab.clipboard);
    std::swap(editor.timeline, tab.timeline);
    std::swap(editor.frames, tab.frames);
    std::swap(editor.cycles, tab.cycles);
    std::swap(editor.activeElement, tab.activeElement);
    std::swap(editor.references, tab.references);
    std::swap(editor.activeReference, tab.activeReference);
    std::swap(editor.preview, tab.preview);
    std::swap(editor.recovery, tab.recovery);
    std::swap(editor.symmetryAxisX, tab.symmetryAxisX);
    std::swap(editor.symmetryAxisY, tab.symmetryAxisY);
    std::swap(editor.inkRole, tab.inkRole);
    std::swap(editor.backRole, tab.backRole);
}

// What is in hand when the document changes under it: a float is dropped,
// a path being placed let go, and anything naming the old document's
// operations stops naming them.
void letGo(Editor& editor) {
    settleFloating(editor);
    editor.placingPath = false;
    editor.pullingHandle = false;
    editor.pathPoints.clear();
    editor.pathHandles.clear();
    editor.drawingPolygon = false;
    editor.lassoPoints.clear();
    editor.selectPreview.clear();
    editor.renaming = -1;
    editor.renamingGroup = ls::GroupId{};
    editor.renamingSlot = ls::kColorRoleNone;
    editor.confirmRemoveSlot = ls::kColorRoleNone;
    editor.renamingPalette = ls::PaletteId{};
    editor.propertiesLayer = ls::LayerId{};
    editor.paletteAdjust = Editor::PaletteAdjust{};
    editor.lastStrokeEnd = { -1, -1 };
    editor.pixelClip.from = ls::DocumentId{};
    for (PixelClip::Piece& piece : editor.customBrush.pieces) {
        piece.dither = ls::OperationId{};
    }
    if (editor.autosaveOn && editor.doc.modified()) {
        editor.recovery.writeNow(editor.doc, SDL_GetTicks() / 1000ull);
    }
}

// The document now in the editor, shown: caches emptied, lists re-read,
// the view it was left at.
void show(Editor& editor, CanvasView& canvas, const DocumentTab& view) {
    canvas.frames().clear();
    canvas.referenceTextures().clear();
    if (view.fitPending) {
        canvas.requestFit();
    } else {
        canvas.setZoom(view.zoom);
        canvas.setPan(view.panX, view.panY);
    }
    resyncLayers(editor);
    resyncFrames(editor);
    resyncReferences(editor, canvas);
    refreshInks(editor);
    canvas.invalidate();
}

void keepView(DocumentTab& tab, const CanvasView& canvas) {
    tab.zoom = canvas.zoom();
    tab.panX = canvas.panX();
    tab.panY = canvas.panY();
    tab.fitPending = false;
}

std::unique_ptr<DocumentTab> blankTab(Editor& editor) {
    auto tab = std::make_unique<DocumentTab>();
    tab->id = editor.nextTabId++;
    return tab;
}

} // namespace

void initTabs(Editor& editor) {
    editor.tabs.clear();
    editor.tabs.push_back(blankTab(editor));
    editor.activeTab = 0;
}

size_t tabCount(const Editor& editor) {
    return editor.tabs.size();
}

namespace {

const Document& documentOf(const Editor& editor, size_t index) {
    return index == editor.activeTab ? editor.doc : editor.tabs[index]->doc;
}

} // namespace

std::string tabName(const Editor& editor, size_t index) {
    if (index >= editor.tabs.size()) {
        return std::string();
    }
    const Document& doc = documentOf(editor, index);
    if (!doc.path().empty()) {
        return fileName(doc.path());
    }
    // Numbered, so two new documents are two names.
    const uint32_t id = editor.tabs[index]->id;
    return id <= 1 ? std::string("untitled") : "untitled " + std::to_string(id);
}

bool tabModified(const Editor& editor, size_t index) {
    return index < editor.tabs.size() && documentOf(editor, index).modified();
}

bool documentIsBlank(const Editor& editor) {
    return !editor.doc.modified() && editor.doc.path().empty() && !editor.doc.canUndo();
}

void switchToTab(Editor& editor, CanvasView& canvas, size_t index) {
    if (index == editor.activeTab || index >= editor.tabs.size() || editor.busy()) {
        return;
    }
    letGo(editor);
    DocumentTab& here = *editor.tabs[editor.activeTab];
    exchange(editor, here);
    keepView(here, canvas);
    DocumentTab& there = *editor.tabs[index];
    exchange(editor, there);
    editor.activeTab = index;
    show(editor, canvas, there);
}

void openTab(Editor& editor, CanvasView& canvas) {
    if (editor.tabs.empty()) {
        initTabs(editor);
    }
    letGo(editor);
    DocumentTab& here = *editor.tabs[editor.activeTab];
    exchange(editor, here);
    keepView(here, canvas);
    // The editor now holds what the slot held: nothing. The new slot beside
    // the old one stands for it while it is on screen.
    editor.tabs.insert(editor.tabs.begin() + static_cast<std::ptrdiff_t>(editor.activeTab) + 1,
                       blankTab(editor));
    ++editor.activeTab;
    forgetInteraction(editor);
    canvas.frames().clear();
    canvas.referenceTextures().clear();
    canvas.requestFit();
    // Its own safety net: a crash loses nothing in any tab.
    if (editor.recovery.begin()) {
        editor.recovery.setIntervalSeconds(editor.prefs.autosaveSeconds);
    }
}

void closeActiveTab(Editor& editor, CanvasView& canvas) {
    if (editor.tabs.empty()) {
        initTabs(editor);
    }
    editor.recovery.clear();
    if (editor.tabs.size() <= 1) {
        forgetInteraction(editor);
        newDocument(editor, editor.files.newDocument);
        canvas.frames().clear();
        canvas.referenceTextures().clear();
        canvas.requestFit();
        refreshInks(editor);
        return;
    }
    letGo(editor);
    editor.recovery.clear();                // letGo may have written one
    const size_t closing = editor.activeTab;
    const size_t next = closing + 1 < editor.tabs.size() ? closing + 1 : closing - 1;
    DocumentTab& neighbour = *editor.tabs[next];
    exchange(editor, neighbour);            // the neighbour on screen, the closed one parked
    DocumentTab view;
    view.zoom = neighbour.zoom;
    view.panX = neighbour.panX;
    view.panY = neighbour.panY;
    view.fitPending = neighbour.fitPending;
    // The neighbour's slot keeps its id and now stands for it on screen; the
    // closed document goes with the slot's old contents.
    const uint32_t id = neighbour.id;
    editor.tabs[next] = std::make_unique<DocumentTab>();
    editor.tabs[next]->id = id;
    editor.tabs.erase(editor.tabs.begin() + static_cast<std::ptrdiff_t>(closing));
    editor.activeTab = next > closing ? next - 1 : next;
    show(editor, canvas, view);
}

int modifiedOtherTab(const Editor& editor) {
    for (size_t i = 0; i < editor.tabs.size(); ++i) {
        if (i != editor.activeTab && editor.tabs[i]->doc.modified()) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void clearAllRecovery(Editor& editor) {
    editor.recovery.clear();
    for (const std::unique_ptr<DocumentTab>& tab : editor.tabs) {
        tab->recovery.clear();
    }
}

void drawTabBar(Editor& editor, CanvasView& canvas, SDL_Window* window) {
    if (editor.tabs.empty()) {
        return;
    }
    // Which tab is showing is the editor's to say, not ImGui's: the active
    // one is asserted every frame, and a click on another is a request.
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.f, 5.f));
    const ImGuiTabBarFlags flags = ImGuiTabBarFlags_FittingPolicyScroll |
                                   ImGuiTabBarFlags_NoTooltip;
    int chosen = -1;
    int closing = -1;
    if (ImGui::BeginTabBar("documents", flags)) {
        for (size_t i = 0; i < editor.tabs.size(); ++i) {
            const std::string label = tabName(editor, i) + "###tab" +
                                      std::to_string(editor.tabs[i]->id);
            bool open = true;
            ImGuiTabItemFlags itemFlags = tabModified(editor, i)
                                              ? ImGuiTabItemFlags_UnsavedDocument : 0;
            if (i == editor.activeTab) {
                itemFlags |= ImGuiTabItemFlags_SetSelected;
            }
            if (ImGui::BeginTabItem(label.c_str(), &open, itemFlags)) {
                ImGui::EndTabItem();
            }
            if (ImGui::IsItemHovered()) {
                const Document& doc = documentOf(editor, i);
                ImGui::SetTooltip("%s", doc.path().empty() ? "Not saved yet" : doc.path().c_str());
            }
            if (!open) {
                closing = static_cast<int>(i);
            } else if (i != editor.activeTab && ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
                chosen = static_cast<int>(i);
            }
        }
        ImGui::EndTabBar();
    }
    ImGui::PopStyleVar();
    if (closing >= 0) {
        switchToTab(editor, canvas, static_cast<size_t>(closing));
        if (editor.activeTab == static_cast<size_t>(closing)) {
            requestAction(editor, canvas, window, PendingAction::CloseTab);
        }
    } else if (chosen >= 0) {
        switchToTab(editor, canvas, static_cast<size_t>(chosen));
    }
}

} // namespace fast
