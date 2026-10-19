// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

// panel_view.cpp -- the preview in the corner of the canvas, and the status bar.
// Declared in panels.h; what the panel files share is in panels_internal.h.

#include "ui/panels_internal.h"
#include "app/i18n.h"
#include "ui/os_clipboard.h"
#include "ui/reference_section.h"
#include "ui/theme.h"
#include "ui/widgets.h"
#include "ui/tile_tools.h"
#include "ui/ui_script.h"

#include "app/reference.h"
#include "app/library.h"
#include "app/file_io.h"
#include "app/image_io.h"
#include "app/palette_io.h"
#include "app/palette_tools.h"
#include "app/shape.h"
#include "app/transform.h"
#include "app/tracks.h"
#include "app/tilemap.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace fast {

// ---------------------------------------------------------------- preview --

namespace {

// Backgrounds worth checking a sprite against. Not a palette: these are the
// situations a sprite has to survive -- a bright sky, a dark interior, snow,
// grass -- and they are the ones that catch a silhouette that does not read or
// an outline that disappears.
struct Backdrop {
    const char* name;
    ImU32       colour;
};

const Backdrop kBackdrops[] = {
    { "Black",  IM_COL32(  8,   8,  10, 255) },
    { "Dark",   IM_COL32( 42,  40,  52, 255) },
    { "Grey",   IM_COL32(128, 128, 130, 255) },
    { "White",  IM_COL32(244, 246, 248, 255) },
    { "Sky",    IM_COL32( 92, 140, 200, 255) },
    { "Grass",  IM_COL32( 86, 132,  70, 255) },
    { "Sand",   IM_COL32(214, 184, 130, 255) },
    { "Blood",  IM_COL32(120,  38,  40, 255) },
};

} // namespace

void drawPreviewOverlay(Editor& editor, CanvasView& canvas) {
    if (!editor.preview.visible || canvas.compiledWidth() == 0) {
        canvas.setCornerOverlay({ 0.f, 0.f });
        return;
    }

    const theme::Palette& c = theme::palette();
    const float scale = static_cast<float>(editor.preview.scale);
    const float artWidth = static_cast<float>(canvas.compiledWidth()) * scale;
    const float artHeight = static_cast<float>(canvas.compiledHeight()) * scale;

    // The sample itself is in pixels of the artwork (1x to 4x), never scaled by the
    // interface; the box round it and the controls under it are, and their widths
    // follow the font.
    const float padding = theme::px(10.f);
    const float swatchRow = theme::px(16.f);
    const float gap = theme::px(3.f);
    const float margin = theme::px(14.f);

    // Two rows of controls, not one: the scales and the colour, then the
    // backdrops. Sizing the box for one leaves the second hanging outside it.
    const float controlsHeight =
        ImGui::GetFrameHeight() + theme::metrics().itemSpacing + swatchRow + theme::px(12.f);

    // The 1x to 4x buttons: a digit and an x, so the same width in every language.
    const float scaleWidth = theme::px(26.f);

    // Wide enough for the controls even when the sprite is tiny, which it
    // usually is: a 16x16 at 1x is smaller than the buttons under it.
    // Wide enough for eight backdrop swatches in a row, whatever the sprite.
    const float backdropRow = 8.f * swatchRow + 7.f * gap;
    const float boxWidth = std::max(artWidth + padding * 2.f,
                                    backdropRow + padding * 2.f + theme::px(4.f));
    const float boxHeight = artHeight + controlsHeight + padding * 2.f;
    // The room it takes from the view -- the box, its distance from the edge
    // and a gap -- which fitting the artwork keeps clear.
    canvas.setCornerOverlay({ boxWidth + margin + theme::px(8.f),
                              boxHeight + margin + theme::px(8.f) });

    // Bottom right of the canvas, out of the way of the artwork, which is
    // usually centred.
    const ImVec2 windowMin = ImGui::GetWindowPos();
    const ImVec2 windowSize = ImGui::GetWindowSize();
    const ImVec2 at(windowMin.x + windowSize.x - boxWidth - margin,
                    windowMin.y + windowSize.y - boxHeight - margin);

    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(at, ImVec2(at.x + boxWidth, at.y + boxHeight),
                        ImGui::GetColorU32(c.windowBackground), theme::px(4.f));
    draw->AddRect(at, ImVec2(at.x + boxWidth, at.y + boxHeight),
                  ImGui::GetColorU32(c.border), theme::px(4.f));

    const ImVec2 artAt(at.x + (boxWidth - artWidth) * 0.5f, at.y + padding);
    canvas.drawSample(draw, artAt, scale,
                      ImGui::ColorConvertFloat4ToU32(
                          ImVec4(editor.preview.color[0], editor.preview.color[1],
                                 editor.preview.color[2], editor.preview.color[3])),
                      editor.preview.transparent);

    // A hairline round the artwork, so a sprite whose edge matches the backdrop
    // still has a visible extent.
    draw->AddRect(artAt, ImVec2(artAt.x + artWidth, artAt.y + artHeight),
                  ImGui::GetColorU32(c.border));

    // The navigator: while the view shows only part of the canvas, the part
    // it shows is outlined here, and clicking or dragging here looks there.
    {
        const ls::Rect2f seen = canvas.visibleArea();
        const float w = static_cast<float>(canvas.compiledWidth());
        const float h = static_cast<float>(canvas.compiledHeight());
        const bool partial = seen.min.x > 0.f || seen.min.y > 0.f || seen.max.x < w ||
                             seen.max.y < h;
        const ImVec2 cursor = ImGui::GetCursorScreenPos();
        ImGui::SetCursorScreenPos(artAt);
        ImGui::InvisibleButton("navigate", ImVec2(artWidth, artHeight));
        if (partial && ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            const ImVec2 mouse = ImGui::GetIO().MousePos;
            canvas.centreOn(std::clamp((mouse.x - artAt.x) / scale, 0.f, w),
                            std::clamp((mouse.y - artAt.y) / scale, 0.f, h));
        }
        if (partial && ImGui::IsItemHovered() && !ImGui::IsItemActive()) {
            ImGui::SetTooltip("%s", tr("Click or drag to look there"));
        }
        ImGui::SetCursorScreenPos(cursor);
        if (partial) {
            const ImVec2 a(artAt.x + std::clamp(seen.min.x, 0.f, w) * scale,
                           artAt.y + std::clamp(seen.min.y, 0.f, h) * scale);
            const ImVec2 b(artAt.x + std::clamp(seen.max.x, 0.f, w) * scale,
                           artAt.y + std::clamp(seen.max.y, 0.f, h) * scale);
            draw->AddRect(ImVec2(a.x - 1.f, a.y - 1.f), ImVec2(b.x + 1.f, b.y + 1.f),
                          IM_COL32(0, 0, 0, 160));
            draw->AddRect(a, b, ImGui::GetColorU32(c.accent));
        }
    }

    // The controls sit inside the box as a real ImGui region, so they can be
    // clicked rather than only looked at.
    ImGui::SetCursorScreenPos(ImVec2(at.x + padding, at.y + artHeight + padding + theme::px(4.f)));
    ImGui::BeginGroup();
    ImGui::PushID("preview");

    for (int step = 1; step <= 4; ++step) {
        if (step > 1) {
            ImGui::SameLine(0.f, gap);
        }
        const bool selected = editor.preview.scale == step;
        ImGui::PushStyleColor(ImGuiCol_Button,
                              selected ? c.accentDim : ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_Text, selected ? c.accent : c.textDim);
        char label[8];
        std::snprintf(label, sizeof(label), "%dx", step);
        if (ImGui::Button(label, ImVec2(scaleWidth, 0.f))) {
            editor.preview.scale = step;
        }
        ImGui::PopStyleColor(2);
    }

    ImGui::SameLine(0.f, theme::px(8.f));
    if (theme::swatch("transparent", IM_COL32(0, 0, 0, 0),
                      editor.preview.transparent, theme::px(18.f))) {
        editor.preview.transparent = true;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", tr("No background"));
    }

    ImGui::SameLine(0.f, gap);
    ImGui::SetNextItemWidth(theme::px(40.f));
    if (ImGui::ColorEdit4("##bg", editor.preview.color,
                          ImGuiColorEditFlags_NoInputs |
                          ImGuiColorEditFlags_NoLabel)) {
        editor.preview.transparent = false;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", tr("Background colour"));
    }

    // The situations a sprite has to survive, one click each.
    for (size_t i = 0; i < sizeof(kBackdrops) / sizeof(kBackdrops[0]); ++i) {
        if (i % 8 != 0) {
            ImGui::SameLine(0.f, gap);
        }
        const ImU32 colour = kBackdrops[i].colour;
        // Packed without ImGui::GetColorU32, which multiplies in the global
        // style alpha and so would never compare equal to a literal colour.
        const bool selected =
            !editor.preview.transparent &&
            colour == ImGui::ColorConvertFloat4ToU32(
                ImVec4(editor.preview.color[0], editor.preview.color[1],
                       editor.preview.color[2], editor.preview.color[3]));
        if (theme::swatch(kBackdrops[i].name, colour, selected, swatchRow)) {
            const ImVec4 unpacked = ImGui::ColorConvertU32ToFloat4(colour);
            editor.preview.color[0] = unpacked.x;
            editor.preview.color[1] = unpacked.y;
            editor.preview.color[2] = unpacked.z;
            editor.preview.color[3] = unpacked.w;
            editor.preview.transparent = false;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", kBackdrops[i].name);
        }
    }

    ImGui::PopID();
    ImGui::EndGroup();
}

// ------------------------------------------------------------- status bar --

// What the program has just done, on the left, and where things stand on the
// right, in a fixed order so the eye learns where to look. Set in the caption
// size (the caller pushes it). The right-hand group is measured, not given a
// width, so it neither clips nor leaves a gap when a value gets longer.
void drawStatusBar(Editor& editor, const CanvasView& canvas) {
    const theme::Palette& p = theme::palette();
    char buffer[128];
    ImDrawList* draw = ImGui::GetWindowDrawList();

    // Unsaved work is a dot in the accent, not an asterisk in the text: it is
    // the one thing in this bar that must be noticed.
    const float lineHeight = ImGui::GetTextLineHeight();
    const ImVec2 dotAt = ImGui::GetCursorScreenPos();
    const float dot = std::round(lineHeight * 0.34f);
    if (editor.doc.modified()) {
        draw->AddCircleFilled(ImVec2(dotAt.x + dot + 1.f, dotAt.y + lineHeight * 0.5f), dot,
                              ImGui::GetColorU32(p.accent));
    }
    ImGui::Dummy(ImVec2(dot * 2.f + 6.f, lineHeight));
    if (editor.doc.modified() && ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", tr("This document has changes that are not saved."));
    }
    ImGui::SameLine(0.f, 0.f);
    ImGui::PushStyleColor(ImGuiCol_Text, p.text);
    ImGui::TextUnformatted(editor.status.c_str());
    ImGui::PopStyleColor();

    // The values on the right.
    auto size = editor.doc.engine().getCanvasSize(editor.doc.id());
    const int width = size.ok() ? size.value.x : 0;
    const int height = size.ok() ? size.value.y : 0;

    std::snprintf(buffer, sizeof(buffer), "%d x %d", width, height);
    const std::string canvasSize = buffer;

    std::string cursor = "-";
    if (editor.hovered.x >= 0) {
        std::snprintf(buffer, sizeof(buffer), "%d, %d", editor.hovered.x, editor.hovered.y);
        cursor = buffer;
    }

    std::snprintf(buffer, sizeof(buffer), "%.0fx", static_cast<double>(canvas.zoom()));
    const std::string zoom = buffer;

    std::snprintf(buffer, sizeof(buffer), "%.2f ms", canvas.lastCompileMs());
    const std::string compile = buffer;

    // How many compiles this UI frame actually cost: 0 at rest, 1 while
    // drawing, and never once per frame on screen. The number that says
    // whether the frame cache is doing its job.
    std::snprintf(buffer, sizeof(buffer), "%d", canvas.compilesThisFrame());
    const std::string compiles = buffer;

    std::string frameLabel = "-";
    if (!editor.frames.empty()) {
        std::snprintf(buffer, sizeof(buffer), "%d / %d", editor.timeline.activeFrame + 1,
                      static_cast<int>(editor.frames.size()));
        frameLabel = buffer;
    }

    struct Item {
        const char* label;
        const std::string* value;
        bool        bright;
        const char* hint;
    };
    const Item items[] = {
        { "Canvas", &canvasSize, true, nullptr },
        { "Cursor", &cursor, true, nullptr },
        { "Zoom", &zoom, true, nullptr },
        { "Frame", &frameLabel, true, nullptr },
        { "Compile", &compile, false,
          "How long the last real compile took. Cached frames cost nothing, so this "
          "only moves when something changed." },
        { "x", &compiles, canvas.compilesThisFrame() > 0,
          "Compiles this frame. Zero at rest and one while drawing: every other frame "
          "on screen is a texture that already exists, not a second compile." },
    };

    // Measure first, then place: the label and value of each and the gap
    // between items, from the right edge.
    const float gap = ImGui::GetFontSize() * 1.4f;
    const float labelGap = ImGui::GetFontSize() * 0.4f;
    float total = 0.f;
    for (const Item& item : items) {
        total += ImGui::CalcTextSize(tr(item.label)).x + labelGap +
                 ImGui::CalcTextSize(item.value->c_str()).x + gap;
    }
    total -= gap;
    const float padding = ImGui::GetStyle().WindowPadding.x;
    ImGui::SameLine(std::max(ImGui::GetCursorPosX() + gap,
                             ImGui::GetWindowWidth() - total - padding));
    for (size_t i = 0; i < sizeof(items) / sizeof(items[0]); ++i) {
        const Item& item = items[i];
        if (i > 0) {
            ImGui::SameLine(0.f, gap);
        }
        ImGui::PushStyleColor(ImGuiCol_Text, p.textMuted);
        ImGui::TextUnformatted(tr(item.label));
        ImGui::PopStyleColor();
        ImGui::SameLine(0.f, labelGap);
        ImGui::PushStyleColor(ImGuiCol_Text, item.bright ? p.textBright : p.textDim);
        ImGui::TextUnformatted(item.value->c_str());
        ImGui::PopStyleColor();
        if (item.hint != nullptr && ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", tr(item.hint));
        }
    }
}


} // namespace fast
