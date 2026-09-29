// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

#include "ui/reference_section.h"

#include "app/i18n.h"
#include "app/reference.h"
#include "ui/canvas_view.h"
#include "ui/editor.h"
#include "ui/panels.h"
#include "ui/theme.h"
#include "ui/ui_script.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace fast {

namespace {

// Writes the section picked, as one undo step, and shows it.
void apply(Editor& editor, CanvasView& canvas, Reference& reference, uint32_t x, uint32_t y,
           uint32_t w, uint32_t h) {
    Reference edited = reference;
    setReferenceSection(edited, x, y, w, h);
    if (edited.clipX == reference.clipX && edited.clipY == reference.clipY &&
        edited.clipWidth == reference.clipWidth && edited.clipHeight == reference.clipHeight) {
        return;
    }
    if (editor.referenceSection.fit) {
        auto size = editor.doc.engine().getCanvasSize(editor.doc.id());
        if (size.ok() && size.value.x > 0 && size.value.y > 0) {
            fitReference(edited, static_cast<uint32_t>(size.value.x),
                         static_cast<uint32_t>(size.value.y));
        }
    }
    editor.doc.beginAction("Reference section");
    updateReference(editor.doc, edited);
    editor.doc.endAction();
    reference = edited;
    resyncReferences(editor, canvas);
    canvas.invalidate();
}

} // namespace

bool importReferenceToPick(Editor& editor, CanvasView& canvas, const std::string& utf8Path,
                           std::string* error) {
    Reference made;
    if (!importReference(editor.doc, utf8Path, &made, error)) {
        return false;
    }
    resyncReferences(editor, canvas);
    editor.activeReference = made.id;
    // Floating, and opened when there is something in it to look at -- with
    // this window, since a sheet is often imported for one cell of it.
    editor.panels.references = true;
    openReferenceSection(editor, made.id, true);
    canvas.invalidate();
    editor.say("Imported " + made.name + " -- pick the part to show, or keep it whole");
    return true;
}

void openReferenceSection(Editor& editor, const std::string& referenceId, bool fit) {
    Editor::ReferenceSectionView& view = editor.referenceSection;
    view.open = true;
    view.reference = referenceId;
    view.fit = fit;
    view.dragging = false;
}

void drawReferenceSectionWindow(Editor& editor, CanvasView& canvas) {
    Editor::ReferenceSectionView& view = editor.referenceSection;
    if (!view.open) {
        return;
    }
    Reference* found = nullptr;
    for (Reference& reference : editor.references) {
        if (reference.id == view.reference) {
            found = &reference;
        }
    }
    if (found == nullptr) {
        view.open = false;
        return;
    }
    Reference& reference = *found;
    ImGui::SetNextWindowSize(ImVec2(440.f, 600.f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin((std::string(tr("Reference section")) + "###referencesection").c_str(),
                      &view.open)) {
        ImGui::End();
        return;
    }
    theme::hint(tr("Show only part of the image -- one cell of a sprite sheet, say. The whole "
                   "image is kept, so another part can be picked later."));

    // A sprite sheet's grid: the cell size and the gap between cells.
    int cell[2] = { view.cellWidth, view.cellHeight };
    ImGui::SetNextItemWidth(-60.f);
    if (ImGui::InputInt2(tr("cell"), cell)) {
        view.cellWidth = std::clamp(cell[0], 0, static_cast<int>(reference.width));
        view.cellHeight = std::clamp(cell[1], 0, static_cast<int>(reference.height));
    }
    ImGui::SetNextItemWidth(-60.f);
    if (ImGui::InputInt(tr("gap"), &view.gap)) {
        view.gap = std::clamp(view.gap, 0, 256);
    }
    const bool grid = view.cellWidth > 0 && view.cellHeight > 0;
    // Stepping goes by the grid's cell, or -- with no grid given -- by the
    // part shown: pick one cell and the arrows walk the sheet in its size.
    const int cellW = grid ? view.cellWidth : static_cast<int>(reference.clipWidth);
    const int cellH = grid ? view.cellHeight : static_cast<int>(reference.clipHeight);
    const bool steps = cellW > 0 && cellH > 0;
    const int stepX = cellW + view.gap;
    const int stepY = cellH + view.gap;

    // The preview: the whole image, the part shown outlined, the grid over it.
    const ReferenceCache::Entry* entry = canvas.referenceTextures().entryFor(editor.doc, reference);
    const float W = static_cast<float>(reference.width);
    const float H = static_cast<float>(reference.height);
    const float room = ImGui::GetContentRegionAvail().x;
    const float zoom = std::max(0.05f, std::min(room / W, 320.f / H));
    const ImVec2 at = ImGui::GetCursorScreenPos();
    const ImVec2 size(W * zoom, H * zoom);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(at, ImVec2(at.x + size.x, at.y + size.y), IM_COL32(40, 42, 50, 255));
    if (entry != nullptr && entry->texture != nullptr) {
        // Pixel for pixel when magnified, so the cells read as the sheet's.
        const bool sharp = zoom >= 1.f;
        if (sharp) {
            draw->AddCallback(ImGui::GetPlatformIO().DrawCallback_SetSamplerNearest, nullptr);
        }
        draw->AddImage(reinterpret_cast<ImTextureID>(entry->texture), at,
                       ImVec2(at.x + size.x, at.y + size.y));
        if (sharp) {
            draw->AddCallback(ImGui::GetPlatformIO().DrawCallback_SetSamplerLinear, nullptr);
        }
    }
    ImGui::InvisibleButton("##sectionpreview", size);
    nameForScripts("section preview");
    const auto imagePixel = [&](ImVec2 screen) {
        return ImVec2(std::clamp((screen.x - at.x) / zoom, 0.f, W - 0.001f),
                      std::clamp((screen.y - at.y) / zoom, 0.f, H - 0.001f));
    };
    // The cell a point of the image is in, as its rectangle.
    const auto cellAt = [&](ImVec2 p, int* cx, int* cy) {
        *cx = static_cast<int>(p.x) / std::max(1, stepX);
        *cy = static_cast<int>(p.y) / std::max(1, stepY);
    };
    // A drag is one undo step, however many sections it passes through.
    if (ImGui::IsItemActivated()) {
        view.dragging = true;
        view.from = imagePixel(ImGui::GetIO().MousePos);
        editor.doc.beginAction("Reference section");
    }
    const ImVec2 to = imagePixel(ImGui::GetIO().MousePos);
    // Without a grid a click is not a section: a drag of at least a pixel is.
    const bool spans = grid || std::fabs(to.x - view.from.x) >= 1.f ||
                       std::fabs(to.y - view.from.y) >= 1.f;
    if (view.dragging && ImGui::IsItemActive() && spans) {
        uint32_t x0, y0, x1, y1;
        if (grid) {
            int ax, ay, bx, by;
            cellAt(view.from, &ax, &ay);
            cellAt(to, &bx, &by);
            x0 = static_cast<uint32_t>(std::min(ax, bx) * stepX);
            y0 = static_cast<uint32_t>(std::min(ay, by) * stepY);
            x1 = static_cast<uint32_t>(std::max(ax, bx) * stepX + view.cellWidth);
            y1 = static_cast<uint32_t>(std::max(ay, by) * stepY + view.cellHeight);
        } else {
            x0 = static_cast<uint32_t>(std::min(view.from.x, to.x));
            y0 = static_cast<uint32_t>(std::min(view.from.y, to.y));
            x1 = static_cast<uint32_t>(std::max(view.from.x, to.x)) + 1;
            y1 = static_cast<uint32_t>(std::max(view.from.y, to.y)) + 1;
        }
        apply(editor, canvas, reference, x0, y0, x1 - x0, y1 - y0);
    }
    if (ImGui::IsItemDeactivated() && view.dragging) {
        view.dragging = false;
        editor.doc.endAction();
    }
    if (grid) {
        const ImU32 line = IM_COL32(255, 255, 255, 60);
        for (int x = 0; x <= static_cast<int>(W); x += stepX) {
            draw->AddLine(ImVec2(at.x + x * zoom, at.y), ImVec2(at.x + x * zoom, at.y + size.y), line);
            if (view.gap > 0 && x + view.cellWidth <= static_cast<int>(W)) {
                const float e = at.x + (x + view.cellWidth) * zoom;
                draw->AddLine(ImVec2(e, at.y), ImVec2(e, at.y + size.y), line);
            }
        }
        for (int y = 0; y <= static_cast<int>(H); y += stepY) {
            draw->AddLine(ImVec2(at.x, at.y + y * zoom), ImVec2(at.x + size.x, at.y + y * zoom), line);
            if (view.gap > 0 && y + view.cellHeight <= static_cast<int>(H)) {
                const float e = at.y + (y + view.cellHeight) * zoom;
                draw->AddLine(ImVec2(at.x, e), ImVec2(at.x + size.x, e), line);
            }
        }
    }
    const ImVec2 lo(at.x + reference.shownX() * zoom, at.y + reference.shownY() * zoom);
    const ImVec2 hi(lo.x + reference.shownWidth() * zoom, lo.y + reference.shownHeight() * zoom);
    draw->AddRect(lo, hi, IM_COL32(0, 0, 0, 200), 0.f, 0, 4.f);
    draw->AddRect(lo, hi, ImGui::GetColorU32(theme::palette().accent), 0.f, 0, 2.f);

    // Typed.
    int rect[4] = { static_cast<int>(reference.shownX()), static_cast<int>(reference.shownY()),
                    static_cast<int>(reference.shownWidth()), static_cast<int>(reference.shownHeight()) };
    ImGui::SetNextItemWidth(-60.f);
    if (ImGui::InputInt4(tr("x y w h"), rect, ImGuiInputTextFlags_EnterReturnsTrue)) {
        apply(editor, canvas, reference, static_cast<uint32_t>(std::max(0, rect[0])),
              static_cast<uint32_t>(std::max(0, rect[1])), static_cast<uint32_t>(std::max(0, rect[2])),
              static_cast<uint32_t>(std::max(0, rect[3])));
    }

    // Stepping through the sheet, cell by cell, row after row.
    const float third = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x * 2.f) / 3.f;
    ImGui::BeginDisabled(!steps);
    const auto step = [&](int by) {
        Reference moved = reference;
        stepReferenceSection(moved, by, static_cast<uint32_t>(cellW), static_cast<uint32_t>(cellH),
                             static_cast<uint32_t>(view.gap));
        apply(editor, canvas, reference, moved.shownX(), moved.shownY(), moved.shownWidth(),
              moved.shownHeight());
    };
    if (ImGui::Button(tr("< Cell"), ImVec2(third, 0.f))) {
        step(-1);
    }
    ImGui::SameLine();
    if (ImGui::Button(tr("Cell >"), ImVec2(third, 0.f))) {
        step(1);
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button(tr("Whole image"), ImVec2(third, 0.f))) {
        apply(editor, canvas, reference, 0, 0, 0, 0);
    }
    char shown[64];
    std::snprintf(shown, sizeof(shown), "%u x %u %s %u x %u", reference.shownWidth(),
                  reference.shownHeight(), tr("of"), reference.width, reference.height);
    ImGui::TextDisabled("%s", shown);
    if (ImGui::Button(tr("Done"), ImVec2(-1.f, 0.f))) {
        view.open = false;
    }
    ImGui::End();
}

} // namespace fast
