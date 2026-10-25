// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

// dialogs.cpp -- the windows that open from the menus: the sheet import, canvas and sprite size, adjust colours, modify selection, history and text. (New is the start screen.)
// See app_internal.h for how the window's files divide.

#include "app/limits.h"
#include "ui/app_internal.h"

#include "app/pixel_font.h"
#include "app/file_io.h"
#include "app/i18n.h"
#include "ui/widgets.h"
#include "ui/panels.h"
#include "ui/theme.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace fast {

// The history as a list. Clicking a step goes to just after it -- undoing or
// redoing as many steps as that takes -- and nothing is lost by looking: the
// steps after the one chosen stay as redo until something new is done.
void drawHistoryPanel(Editor& editor, CanvasView& canvas) {
    if (!editor.historyOpen) {
        return;
    }
    ImGui::SetNextWindowSize(ImVec2(260.f, 360.f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("History", &editor.historyOpen)) {
        if (widgets::escapePutsAway()) {
            editor.historyOpen = false;
        }
        const std::vector<std::string> done = editor.doc.undoLabels();
        const std::vector<std::string> undone = editor.doc.redoLabels();
        int target = -1;           // how many steps should be done afterwards
        if (ImGui::Selectable(tr("(as opened)"), done.empty())) {
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
    if (!widgets::beginModal("Text", &editor.textOpen, ImGuiWindowFlags_AlwaysAutoResize)) {
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
    ImGui::TextDisabled("%s", tr("Enter places it; Ctrl+Enter for a new line"));
    ImGui::SetNextItemWidth(160.f);
    widgets::sliderInt(tr("size"), &editor.textScale, 1, 8, "%dx");
    int width = 0;
    int height = 0;
    layOutText(editor.textBuffer, editor.textAt, editor.textScale, &width, &height);
    ImGui::TextDisabled("%d x %d pixels at %d, %d", width, height, editor.textAt.x,
                        editor.textAt.y);
    const bool empty = editor.textBuffer[0] == '\0';
    ImGui::BeginDisabled(empty);
    if (ImGui::Button(tr("Place"), ImVec2(110.f, 0.f)) || (entered && !empty)) {
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
    if (ImGui::Button(tr("Cancel"), ImVec2(90.f, 0.f))) {
        editor.textOpen = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
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
        ImGui::InputInt(tr("cell width"), &sheet.cellWidth);
        ImGui::SetNextItemWidth(120.f);
        ImGui::InputInt(tr("cell height"), &sheet.cellHeight);
        ImGui::SetNextItemWidth(120.f);
        ImGui::InputInt(tr("hold (ms)"), &sheet.holdMs, 10, 100);
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
                ImGui::TextDisabled("%s", tr("The edge that does not make a whole cell is left out."));
            }
        } else {
            ImGui::PushStyleColor(ImGuiCol_Text, theme::palette().danger);
            ImGui::TextWrapped("%s", error.c_str());
            ImGui::PopStyleColor();
        }
        ImGui::Dummy(ImVec2(0.f, 6.f));
        ImGui::BeginDisabled(!fits);
        if (ImGui::Button(tr("Open as frames"), ImVec2(-1.f, 0.f))) {
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
    if (!widgets::beginModal("Canvas size", &dialog.open,
                                ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }
    auto size = editor.doc.engine().getCanvasSize(editor.doc.id());
    if (size.ok()) {
        ImGui::TextDisabled("Now %d x %d", size.value.x, size.value.y);
    }
    ImGui::SetNextItemWidth(120.f);
    ImGui::InputInt(tr("width"), &dialog.width);
    ImGui::SetNextItemWidth(120.f);
    ImGui::InputInt(tr("height"), &dialog.height);
    dialog.width = std::clamp(dialog.width, 1, static_cast<int>(limits().canvasDimension));
    dialog.height = std::clamp(dialog.height, 1, static_cast<int>(limits().canvasDimension));

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
    ImGui::TextWrapped("%s", tr("Pixels a smaller canvas leaves outside are kept, not "
                       "thrown away: grow it again and they are back."));
    ImGui::PopStyleColor();

    ImGui::Dummy(ImVec2(0.f, 6.f));
    if (ImGui::Button(tr("Resize"), ImVec2(110.f, 0.f))) {
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
    if (ImGui::Button(tr("Cancel"), ImVec2(90.f, 0.f))) {
        dialog.open = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

// The layers an adjustment covers.
static std::vector<ls::LayerId> adjustLayers(Editor& editor, int scope) {
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
    if (!widgets::beginModal("Adjust colours", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }
    const char* scopes[] = { "This layer", "Every layer of this frame", "Every frame" };
    ImGui::SetNextItemWidth(220.f);
    ImGui::Combo(tr("covers"), &dialog.scope, scopes, 3);
    ImGui::Checkbox(tr("Palette slots too"), &dialog.slots);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", tr("The slots these layers paint through change as well -- and so "
                          "does everything else that paints through them."));
    }
    ImGui::TextDisabled("%zu colour(s)%s", dialog.base.sites.size() + dialog.base.slots.size(),
                        dialog.base.slots.empty() ? "" : ", slots included");

    ColourAdjust& a = dialog.adjust;
    bool changed = false;
    ImGui::SetNextItemWidth(260.f);
    changed |= widgets::sliderFloat(tr("hue"), &a.hue, -180.f, 180.f, "%.0f deg");
    ImGui::SetNextItemWidth(260.f);
    changed |= widgets::sliderFloat(tr("saturation"), &a.saturation, -1.f, 1.f, "%.2f");
    ImGui::SetNextItemWidth(260.f);
    changed |= widgets::sliderFloat(tr("lightness"), &a.lightness, -1.f, 1.f, "%.2f");
    ImGui::SetNextItemWidth(260.f);
    changed |= widgets::sliderFloat(tr("brightness"), &a.brightness, -1.f, 1.f, "%.2f");
    ImGui::SetNextItemWidth(260.f);
    changed |= widgets::sliderFloat(tr("contrast"), &a.contrast, -1.f, 1.f, "%.2f");
    changed |= ImGui::Checkbox(tr("Invert"), &a.invert);
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
    if (ImGui::Button(tr("Apply"), ImVec2(100.f, 0.f))) {
        close(true);
        editor.say("Adjusted; every element is still what it was");
    }
    ImGui::SameLine();
    if (ImGui::Button(tr("Reset"), ImVec2(80.f, 0.f))) {
        a = ColourAdjust{};
        applyAdjust(editor.doc, dialog.base, a);
        canvas.invalidate();
    }
    ImGui::SameLine();
    if (ImGui::Button(tr("Cancel"), ImVec2(90.f, 0.f)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
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
    if (!widgets::beginModal(id.c_str(), &dialog.open, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }
    ImGui::SetNextItemWidth(160.f);
    if (ImGui::InputInt(tr("pixels"), &dialog.pixels)) {
        dialog.pixels = std::clamp(dialog.pixels, 1, 256);
    }
    ImGui::Checkbox(tr("Round corners"), &dialog.round);
    ImGui::PushStyleColor(ImGuiCol_Text, theme::palette().textDim);
    ImGui::TextWrapped(dialog.how == SelectionModify::Border
                           ? "Keeps only a band this wide just inside the edge -- the "
                             "pixels Edit > Stroke would paint."
                           : "Square keeps a rectangle a rectangle; round keeps a circle "
                             "a circle.");
    ImGui::PopStyleColor();
    ImGui::Dummy(ImVec2(0.f, 6.f));
    if (ImGui::Button(tr("OK"), ImVec2(110.f, 0.f))) {
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
    if (ImGui::Button(tr("Cancel"), ImVec2(90.f, 0.f))) {
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
    if (!widgets::beginModal("Sprite size", &dialog.open, ImGuiWindowFlags_AlwaysAutoResize)) {
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
    ImGui::Checkbox(tr("Keep the proportion"), &dialog.lockRatio);

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
    if (ImGui::InputInt(tr("width"), &dialog.width)) {
        dialog.width = std::clamp(dialog.width, 1, static_cast<int>(limits().canvasDimension));
        fromPixels(true);
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120.f);
    if (ImGui::InputInt(tr("height"), &dialog.height)) {
        dialog.height = std::clamp(dialog.height, 1, static_cast<int>(limits().canvasDimension));
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
                                  static_cast<int>(limits().canvasDimension));
        dialog.height = std::clamp(static_cast<int>(std::lround(h0 * dialog.percentY / 100.f)), 1,
                                   static_cast<int>(limits().canvasDimension));
    };
    ImGui::SetNextItemWidth(120.f);
    if (ImGui::InputFloat(tr("% across"), &dialog.percentX, 25.f, 100.f, "%.1f")) {
        dialog.percentX = std::clamp(dialog.percentX, 1.f, 10000.f);
        fromPercent(true);
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120.f);
    if (ImGui::InputFloat(tr("% down"), &dialog.percentY, 25.f, 100.f, "%.1f")) {
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
    if (ImGui::Button(tr("Resize"), ImVec2(110.f, 0.f))) {
        const uint32_t w = static_cast<uint32_t>(dialog.width);
        const uint32_t h = static_cast<uint32_t>(dialog.height);
        changeCanvas(editor, canvas, [&](std::string* error) {
            return resizeSprite(editor.doc, w, h, error);
        }, "Resized to " + std::to_string(w) + " x " + std::to_string(h));
        dialog.open = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button(tr("Cancel"), ImVec2(90.f, 0.f))) {
        dialog.open = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}


} // namespace fast
