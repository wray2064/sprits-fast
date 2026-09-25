// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

#include "ui/panels.h"
#include "ui/theme.h"

#include "app/reference.h"
#include "app/library.h"
#include "app/file_io.h"
#include "app/palette_io.h"
#include "app/palette_tools.h"
#include "app/shape.h"
#include "app/transform.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace fast {
namespace {

// Opens a history bracket on the frame a control is grabbed and closes it when
// released, so a whole drag is one undo step rather than none or hundreds.
//
// Every live control in the program needs this, and getting it wrong is
// invisible until somebody tries to undo, so it is written once here.
void bracketDrag(Editor& editor, bool& flag, const char* label) {
    if (ImGui::IsItemActivated() && !flag) {
        editor.doc.beginAction(label);
        flag = true;
    }
    if (flag && ImGui::IsItemDeactivated()) {
        editor.doc.endAction();
        flag = false;
    }
}

// A change made by a control that is not being dragged -- a combo, a checkbox --
// still needs to be one undo step.
void singleAction(Editor& editor, const char* label) {
    editor.doc.beginAction(label);
    editor.doc.endAction();
}

} // namespace

// ---------------------------------------------------------------- toolbar --

void drawToolbar(Editor& editor) {
    struct Entry {
        Tool        tool;
        theme::Icon icon;
        const char* name;
        const char* shortcut;
        const char* description;
    };
    // Drawn from primitives rather than an icon font: four tools do not justify
    // a font file, a licence and a build step, and a drawn icon can take the
    // accent colour when selected.
    static const Entry kTools[] = {
        { Tool::Pencil, theme::Icon::Pencil,  "Pencil", "B",
          "Draw single pixels. A drag is one undo step." },
        { Tool::Spray, theme::Icon::Spray, "Spray", "Shift+B",
          "Scatter pixels of the current colour about the pointer, for as long "
          "as it is held." },
        { Tool::Eraser, theme::Icon::Eraser,  "Eraser", "E",
          "Remove pixels from the shape of the active layer." },
        { Tool::Bucket, theme::Icon::Bucket,  "Fill",   "G",
          "Flood the area under the cursor. Diagonals do not conduct unless "
          "you ask them to." },
        { Tool::Picker, theme::Icon::Dropper, "Pick colour", "I",
          "Take the colour under the cursor, then return to the previous tool." },
        { Tool::Rectangle, theme::Icon::Rectangle, "Rectangle", "R",
          "Drag out a rectangle. It stays a rectangle: its size, position and "
          "corner radius can be changed afterwards, and anything built on it "
          "follows." },
        { Tool::Ellipse, theme::Icon::Ellipse, "Ellipse", "U",
          "Drag out an ellipse, editable afterwards in the same way." },
        { Tool::Line, theme::Icon::Line, "Line", "L",
          "Drag out a line. Both ends stay adjustable." },
        { Tool::Gradient, theme::Icon::Gradient, "Gradient", "Shift+G",
          "Drag across an area -- the selection, or the colour under the press -- "
          "to lay a dithered gradient between the two colours. It stays a "
          "gradient: its ends, pattern and colours are in the Element panel." },
        { Tool::Contour, theme::Icon::Contour, "Contour", "D",
          "Draw round an area; on release it is filled with the current colour." },
        { Tool::Select, theme::Icon::Marquee, "Select", "M",
          "Drag a rectangle to select. Shift adds, Alt subtracts, both "
          "intersect. Drag inside the selection to move what it holds." },
        { Tool::SelectEllipse, theme::Icon::EllipseMarquee, "Select ellipse", "Shift+M",
          "Drag an ellipse to select, with the same modifiers." },
        { Tool::Lasso, theme::Icon::Lasso, "Lasso", "Q",
          "Draw round what to select. The outline counts as well as the inside." },
        { Tool::Wand, theme::Icon::Wand, "Magic wand", "W",
          "Select what a fill would fill: the area of one colour, or every pixel "
          "of it with Whole canvas." },
        { Tool::Move, theme::Icon::Move, "Move", "V",
          "Drag the selected pixels -- or, with nothing selected, the whole layer. "
          "Shapes inside go along as shapes. Arrows nudge; Enter drops." },
        { Tool::Hand, theme::Icon::Hand, "Hand", "H",
          "Drag to pan. Space does the same with any tool held." },
        { Tool::Zoom, theme::Icon::Zoom, "Zoom", "Z",
          "Click to zoom in on a point; Alt+click or right-click to zoom out." },
    };

    for (const Entry& entry : kTools) {
        if (theme::toolButton(entry.icon, entry.name, entry.shortcut,
                              editor.tool == entry.tool, entry.description)) {
            if (entry.tool == Tool::Picker && editor.tool != Tool::Picker) {
                editor.toolBeforePicker = editor.tool;
            }
            editor.tool = entry.tool;
        }
    }
}

// ------------------------------------------------------------- tool panel --

namespace {

void drawDitherControls(Editor& editor, CanvasView& canvas, const PaintLayer& layer) {
    DitherSettings settings;
    if (!readDitherSettings(editor.doc, layer, &settings)) {
        ImGui::TextDisabled("This layer is not dithered.");
        return;
    }

    bool changed = false;

    theme::sectionHeader("PATTERN");
    int pattern = static_cast<int>(settings.pattern);
    ImGui::SetNextItemWidth(-1.f);
    if (ImGui::Combo("##pattern", &pattern, ditherPatternNames().data(),
                     static_cast<int>(ditherPatternNames().size()))) {
        settings.pattern = static_cast<ls::DitherPatternKind>(pattern);
        changed = true;
        singleAction(editor, "Dither pattern");
    }
    ImGui::SameLine();
    theme::hint("Threshold matrices, not stamps. The same tile works at any "
                "density and at every step of a gradient.");

    theme::sectionHeader("VALUE");
    int modulation = static_cast<int>(settings.modulation);
    ImGui::SetNextItemWidth(-1.f);
    if (ImGui::Combo("##modulation", &modulation, ditherModulationNames().data(),
                     static_cast<int>(ditherModulationNames().size()))) {
        settings.modulation = static_cast<ls::DitherModulation>(modulation);
        changed = true;
        singleAction(editor, "Dither value");
    }
    ImGui::SameLine();
    theme::hint("Constant is a flat screen. The others vary the value across "
                "the shape, which is what makes a gradient out of dithered "
                "colour.");

    if (settings.modulation == ls::DitherModulation::Constant) {
        ImGui::SetNextItemWidth(-1.f);
        if (ImGui::SliderFloat("##density", &settings.density, 0.f, 1.f,
                               "density  %.2f")) {
            changed = true;
        }
        bracketDrag(editor, editor.draggingDither, "Dither density");
    } else {
        float start[2] = { settings.gradientStart.x, settings.gradientStart.y };
        float end[2]   = { settings.gradientEnd.x, settings.gradientEnd.y };
        ImGui::SetNextItemWidth(-42.f);
        if (ImGui::DragFloat2("from", start, 0.25f, 0.f, 0.f, "%.0f")) {
            settings.gradientStart = { start[0], start[1] };
            changed = true;
        }
        bracketDrag(editor, editor.draggingDither, "Gradient");
        ImGui::SetNextItemWidth(-42.f);
        if (ImGui::DragFloat2("to", end, 0.25f, 0.f, 0.f, "%.0f")) {
            settings.gradientEnd = { end[0], end[1] };
            changed = true;
        }
    }

    theme::sectionHeader("ANCHOR");
    int anchor = static_cast<int>(settings.anchor);
    ImGui::SetNextItemWidth(-1.f);
    if (ImGui::Combo("##anchor", &anchor, patternAnchorNames().data(),
                     static_cast<int>(patternAnchorNames().size()))) {
        settings.anchor = static_cast<ls::PatternAnchor>(anchor);
        changed = true;
        singleAction(editor, "Dither anchor");
    }
    ImGui::PushStyleColor(ImGuiCol_Text, theme::palette().textDim);
    ImGui::TextWrapped("%s",
        settings.anchor == ls::PatternAnchor::Local
            ? "The screen turns with the artwork."
        : settings.anchor == ls::PatternAnchor::Global
            ? "The screen stays level with the canvas."
            : "The screen stays put; the artwork moves across it.");
    ImGui::PopStyleColor();

    theme::sectionHeader("RAMP");

    // One row per end: the colour, the slot it follows if any, and a button
    // that points it at the current colour -- a slot when the colour came from
    // the palette, so the dither then recolours with a palette change.
    const auto rampEnd = [&](const char* label, ls::Color& colour, ls::ColorRole& role) {
        ImGui::PushID(label);
        float rgba[4];
        fromColor(colour, rgba);
        if (ImGui::ColorEdit4(label, rgba, ImGuiColorEditFlags_NoInputs)) {
            colour = toColor(rgba);
            role = ls::kColorRoleNone;         // a picked colour is a value
            changed = true;
        }
        bracketDrag(editor, editor.draggingDither, "Ramp colour");
        ImGui::SameLine();
        if (role != ls::kColorRoleNone) {
            const std::string slot = "slot " + std::to_string(role);
            ImGui::TextColored(theme::palette().accent, "%s", slot.c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton("detach")) {
                role = ls::kColorRoleNone;
                changed = true;
            }
            ImGui::SameLine();
        }
        if (ImGui::SmallButton("= current")) {
            const Ink ink = foregroundInk(editor);
            colour = ink.colour;
            role = ink.role;
            changed = true;
            singleAction(editor, "Ramp end from the current colour");
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Set this end to the current colour -- its palette "
                              "slot, when it has one.");
        }
        ImGui::PopID();
    };
    rampEnd("dark",  settings.from, settings.fromRole);
    ImGui::SameLine();
    theme::hint("Each end is a colour or a palette slot. Pick a slot in the "
                "palette, then press = current on an end to point it there -- "
                "the dither then recolours with a palette change like "
                "everything else.");
    rampEnd("light", settings.to,   settings.toRole);

    if (changed) {
        applyDitherSettings(editor.doc, layer, settings);
        editor.dither = settings;
        canvas.invalidate();
        editor.say("Recompiled from the drawing");
    }
}

} // namespace

namespace {
void drawInkControls(Editor& editor);
} // namespace

void drawToolPanel(Editor& editor) {
    if (isSelectionTool(editor.tool) || editor.tool == Tool::Move) {
        theme::sectionHeader("SELECTION");
        ImGui::PushStyleColor(ImGuiCol_Text, theme::palette().textDim);
        ImGui::TextWrapped("Shift adds, Alt subtracts, Shift+Alt intersects. Drag "
                           "inside the selection to move what it holds; arrows "
                           "nudge a pixel, Shift+arrows eight. Enter drops a move, "
                           "Escape takes it back. Ctrl+C, Ctrl+X and Ctrl+V work on "
                           "the selected pixels; Delete clears them.");
        ImGui::PopStyleColor();
        if (editor.tool == Tool::Wand) {
            ImGui::Checkbox("Follow diagonals##wand", &editor.wand.diagonal);
            ImGui::Checkbox("Whole canvas##wand", &editor.wand.global);
            ImGui::SameLine();
            theme::hint("Every pixel of the colour clicked, wherever it is, rather "
                        "than only the area touching the click.");
            ImGui::SetNextItemWidth(-1.f);
            ImGui::SliderInt("##wandtolerance", &editor.wand.tolerance, 0, 64,
                             "tolerance  %d");
        }
        if (!editor.selection.empty()) {
            const ls::Rect2i box = editor.selection.bounds();
            ImGui::Text("%d x %d at %d, %d", box.width(), box.height(), box.min.x, box.min.y);
        }
        ImGui::Dummy(ImVec2(0.f, theme::metrics().sectionGap));
    }

    if (editor.tool == Tool::Bucket) {
        theme::sectionHeader("FILL");
        ImGui::Checkbox("Follow diagonals", &editor.bucket.diagonal);
        ImGui::SameLine();
        theme::hint("Off by default. A one-pixel diagonal is a wall in pixel "
                    "art, and leaking through it is the classic paint-bucket "
                    "annoyance.");
        ImGui::Checkbox("Whole canvas", &editor.bucket.global);
        ImGui::SetNextItemWidth(-1.f);
        ImGui::SliderInt("##tolerance", &editor.bucket.tolerance, 0, 64,
                         "tolerance  %d");
        ImGui::Dummy(ImVec2(0.f, theme::metrics().sectionGap));
    }

    if (editor.tool == Tool::Pencil || editor.tool == Tool::Spray) {
        theme::sectionHeader("INK");
        const char* modes[] = { "Simple", "Lock alpha", "Replace colour", "Shading" };
        int mode = static_cast<int>(editor.inkMode);
        ImGui::SetNextItemWidth(-1.f);
        if (ImGui::Combo("##inkmode", &mode, modes, 4)) {
            editor.inkMode = static_cast<InkMode>(mode);
        }
        ImGui::PushStyleColor(ImGuiCol_Text, theme::palette().textDim);
        ImGui::TextWrapped("%s",
            editor.inkMode == InkMode::Simple
                ? "Paints every pixel the brush covers."
            : editor.inkMode == InkMode::LockAlpha
                ? "Paints only where the layer already draws: recolour a shape "
                  "without spilling past its edge."
            : editor.inkMode == InkMode::Replace
                ? "Paints only pixels of the other button's colour, turning "
                  "them to this one."
                : "Steps each pixel to the next palette slot -- or the one "
                  "before, with the right button. Lay a ramp out in order and "
                  "it is the shading scale, and the result still follows the "
                  "palette.");
        ImGui::PopStyleColor();
        ImGui::Dummy(ImVec2(0.f, theme::metrics().sectionGap));
    }

    if (editor.tool == Tool::Gradient) {
        theme::sectionHeader("GRADIENT");
        int pattern = static_cast<int>(editor.dither.pattern);
        ImGui::SetNextItemWidth(-1.f);
        if (ImGui::Combo("##gradpattern", &pattern, ditherPatternNames().data(),
                         static_cast<int>(ditherPatternNames().size()))) {
            editor.dither.pattern = static_cast<ls::DitherPatternKind>(pattern);
        }
        ImGui::PushStyleColor(ImGuiCol_Text, theme::palette().textDim);
        ImGui::TextWrapped("From the left colour to the right one, along the drag. "
                           "Inside the selection when there is one, otherwise the "
                           "area of the colour under the press, as a fill would "
                           "find it.");
        ImGui::PopStyleColor();
        ImGui::Dummy(ImVec2(0.f, theme::metrics().sectionGap));
    }

    if (editor.tool == Tool::Spray) {
        theme::sectionHeader("SPRAY");
        ImGui::SetNextItemWidth(-1.f);
        ImGui::SliderInt("##sprayradius", &editor.sprayRadius, 1, 32, "reach  %d");
        ImGui::SetNextItemWidth(-1.f);
        ImGui::SliderInt("##spraydensity", &editor.sprayDensity, 1, 60, "density  %d");
        ImGui::Dummy(ImVec2(0.f, theme::metrics().sectionGap));
    }

    if (editor.tool == Tool::Pencil || editor.tool == Tool::Eraser) {
        theme::sectionHeader("BRUSH");
        ImGui::SetNextItemWidth(-1.f);
        ImGui::SliderInt("##size", &editor.brush.size, 1, kMaxBrushSize, "size  %d");
        ImGui::SameLine();
        theme::hint("Shift+] and Shift+[ change it from the keyboard. Even "
                    "sizes hang right and down from the pointer's pixel.");
        ImGui::BeginDisabled(editor.brush.size < 3);
        ImGui::Checkbox("Round", &editor.brush.round);
        ImGui::EndDisabled();
        if (editor.tool == Tool::Pencil) {
            ImGui::SameLine();
            ImGui::BeginDisabled(editor.brush.size != 1);
            ImGui::Checkbox("Pixel-perfect", &editor.brush.pixelPerfect);
            ImGui::EndDisabled();
            ImGui::SameLine();
            theme::hint("At one pixel: the corner of every L in the stroke is "
                        "dropped, so a diagonal reads as a line rather than a "
                        "staircase with doubled steps. The last pixel lands when "
                        "the stroke ends.");
        }
        if (editor.pen.seen) {
            ImGui::Checkbox("Pen pressure sets size", &editor.brush.pressureSize);
            ImGui::SameLine();
            theme::hint("Light touch, one pixel; full pressure, the size above. "
                        "The pen's eraser end erases while it touches.");
        } else {
            ImGui::PushStyleColor(ImGuiCol_Text, theme::palette().textDim);
            ImGui::TextWrapped("A pen tablet works as is; bring one near and "
                               "pressure appears here.");
            ImGui::PopStyleColor();
        }
        ImGui::Dummy(ImVec2(0.f, theme::metrics().sectionGap));
    }

    if (editor.tool == Tool::Rectangle || editor.tool == Tool::Ellipse ||
        editor.tool == Tool::Line) {
        theme::sectionHeader("SHAPES");
        if (editor.tool != Tool::Line) {
            ImGui::Checkbox("Outline only", &editor.shapeOutline);
            if (editor.shapeOutline) {
                ImGui::SameLine();
                ImGui::SetNextItemWidth(-1.f);
                ImGui::SliderFloat("##outlinewidth", &editor.shapeOutlineWidth, 1.f, 8.f,
                                   "width %.0f");
            }
        }
        ImGui::Checkbox("Each shape on its own layer", &editor.shapesOnOwnLayer);
        ImGui::SameLine();
        theme::hint("Off: a shape joins the active layer as one of its "
                    "elements, beside the pixels and the other shapes, and "
                    "stays editable in the Shape panel. On: every shape is a "
                    "layer of its own, listed in the stack.");
        ImGui::Dummy(ImVec2(0.f, theme::metrics().sectionGap));
    }

    if (editor.tool == Tool::Pencil || editor.tool == Tool::Eraser ||
        editor.tool == Tool::Bucket || editor.tool == Tool::Spray) {
        theme::sectionHeader("SYMMETRY");
        ImGui::Checkbox("Across", &editor.symmetryAcross);
        ImGui::SameLine();
        ImGui::Checkbox("Down", &editor.symmetryDown);
        ImGui::SameLine();
        theme::hint("Drawing on one side of an axis draws on the other too. The "
                    "axes start at the middle of the canvas; move them here. "
                    "Shift+click with the pencil draws a straight line from "
                    "where the last stroke ended.");
        const Symmetry now = symmetryNow(editor);
        if (editor.symmetryAcross) {
            float x = static_cast<float>(now.axisX) * 0.5f;
            ImGui::SetNextItemWidth(-1.f);
            if (ImGui::DragFloat("##axisx", &x, 0.5f, 0.f, 16384.f, "axis x  %.1f")) {
                editor.symmetryAxisX = static_cast<int>(std::lround(x * 2.f));
            }
        }
        if (editor.symmetryDown) {
            float y = static_cast<float>(now.axisY) * 0.5f;
            ImGui::SetNextItemWidth(-1.f);
            if (ImGui::DragFloat("##axisy", &y, 0.5f, 0.f, 16384.f, "axis y  %.1f")) {
                editor.symmetryAxisY = static_cast<int>(std::lround(y * 2.f));
            }
        }
        ImGui::Dummy(ImVec2(0.f, theme::metrics().sectionGap));
    }

    drawInkControls(editor);
}

namespace {

// The two inks, and the picker for the one the left button paints with.
//
// This used to recolour the active layer, because a layer was one colour.
// A layer is as many colours as it is painted with now, so this is what it is
// in every other editor -- the colour the next stroke lays down -- and
// recolouring what is already drawn is the element panel's job, or the
// palette's.
void drawInkControls(Editor& editor) {
    theme::sectionHeader("COLOUR");

    const auto toU32 = [](const float rgba[4]) {
        return ImGui::GetColorU32(ImVec4(rgba[0], rgba[1], rgba[2], rgba[3]));
    };
    theme::swatch("##fore", toU32(editor.color), true, 30.f);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("What the left button paints with");
    }
    ImGui::SameLine();
    if (theme::swatch("##back", toU32(editor.backColor), false, 22.f)) {
        swapInks(editor);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("What the right button paints with. Click, or press X, "
                          "to swap the two.");
    }
    ImGui::SameLine();
    const auto describe = [&](const float rgba[4], ls::ColorRole role) {
        const ls::Color c = toColor(rgba);
        char hex[16];
        std::snprintf(hex, sizeof(hex), "#%02X%02X%02X", c.r, c.g, c.b);
        if (role == ls::kColorRoleNone) {
            return std::string(hex);
        }
        return "slot " + std::to_string(role) + "  " + hex;
    };
    ImGui::BeginGroup();
    ImGui::TextUnformatted(describe(editor.color, editor.inkRole).c_str());
    ImGui::TextDisabled("%s", describe(editor.backColor, editor.backRole).c_str());
    ImGui::EndGroup();
    ImGui::SameLine();
    theme::hint("A colour taken from the palette paints through its slot, so "
                "what you draw with it recolours when the slot changes. A colour "
                "picked here is a value of its own. Right-click paints with the "
                "second colour.");

    // Smaller than the column: the palette under it is used more often than
    // the picker, and a picker the width of the panel pushes it off screen.
    ImGui::SetNextItemWidth(std::min(ImGui::GetContentRegionAvail().x, 150.f));
    if (ImGui::ColorPicker4("##colour", editor.color,
                            ImGuiColorEditFlags_NoSidePreview |
                            ImGuiColorEditFlags_NoSmallPreview |
                            ImGuiColorEditFlags_NoInputs)) {
        // A colour chosen here is a value, not a slot -- unless it happens
        // to be exactly one, which is how a slot's own colour stays one.
        editor.inkRole = ls::kColorRoleNone;
    }
    ImGui::SetNextItemWidth(-1.f);
    if (ImGui::ColorEdit4("##hex", editor.color,
                          ImGuiColorEditFlags_DisplayHex | ImGuiColorEditFlags_NoPicker |
                          ImGuiColorEditFlags_NoSmallPreview)) {
        editor.inkRole = ls::kColorRoleNone;
    }
    // The same colour as numbers, both ways: RGB for matching a value from
    // elsewhere, HSV for nudging one along a ramp.
    ImGui::SetNextItemWidth(-1.f);
    if (ImGui::ColorEdit4("##rgb", editor.color,
                          ImGuiColorEditFlags_DisplayRGB | ImGuiColorEditFlags_NoPicker |
                          ImGuiColorEditFlags_NoSmallPreview)) {
        editor.inkRole = ls::kColorRoleNone;
    }
    ImGui::SetNextItemWidth(-1.f);
    if (ImGui::ColorEdit4("##hsv", editor.color,
                          ImGuiColorEditFlags_DisplayHSV | ImGuiColorEditFlags_NoPicker |
                          ImGuiColorEditFlags_NoSmallPreview)) {
        editor.inkRole = ls::kColorRoleNone;
    }
}

} // namespace

// ---------------------------------------------------------------- palette --

namespace {

// The quick row: which palette the document uses, and one button to step to
// the next. This is the whole interface for the common case -- a character
// with a day and a night palette, or a few team colours -- and it is the
// feature the engine exists for, so it sits at the top rather than behind a
// header.
void drawPaletteQuickRow(Editor& editor, CanvasView& canvas,
                         const std::vector<PaletteInfo>& palettes,
                         ls::PaletteId documents, ls::PaletteId shown) {
    const char* currentName = "";
    for (const PaletteInfo& info : palettes) {
        if (info.id == documents) { currentName = info.name.c_str(); }
    }
    const float swapWidth = 30.f;
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - swapWidth -
                            theme::metrics().itemSpacing);
    if (ImGui::BeginCombo("##document-palette", currentName)) {
        for (const PaletteInfo& info : palettes) {
            ImGui::PushID(static_cast<int>(info.id.value));
            const std::string label = info.name + "  (" + std::to_string(info.colours) + ")";
            if (ImGui::Selectable(label.c_str(), info.id == documents)) {
                swapPalette(editor, canvas, info.id);
            }
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("The palette every frame uses. Switching it recolours "
                          "the whole animation in one step, from the drawing.");
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(palettes.size() < 2);
    if (ImGui::Button("<>", ImVec2(swapWidth, 0.f))) {
        swapPalette(editor, canvas, nextPalette(editor.doc, documents));
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(palettes.size() < 2
            ? "Swap to the next palette  (Ctrl+P)\nMake a second one below."
            : "Swap to the next palette  (Ctrl+P)");
    }

    // A frame with a palette of its own shows that one and says so, because
    // the swatches below are about to edit it rather than the document's.
    if (shown != documents) {
        std::string name;
        for (const PaletteInfo& info : palettes) {
            if (info.id == shown) { name = info.name; }
        }
        ImGui::PushStyleColor(ImGuiCol_Text, theme::palette().accent);
        ImGui::TextWrapped("This frame uses %s, its own. The slots below are its.",
                           name.c_str());
        ImGui::PopStyleColor();
    }
}

// The rest: the list, with rename, copy and delete, and the frame's own
// binding. Behind a header because most sessions never need it.
void drawPaletteList(Editor& editor, CanvasView& canvas,
                     const std::vector<PaletteInfo>& palettes,
                     ls::PaletteId documents) {
    ImGui::SetNextItemOpen(editor.palettesOpen, ImGuiCond_Always);
    const bool open = ImGui::CollapsingHeader("Palettes");
    editor.palettesOpen = open;
    if (!open) {
        return;
    }

    for (const PaletteInfo& info : palettes) {
        ImGui::PushID(static_cast<int>(info.id.value));
        const bool isDocuments = info.id == documents;

        // The name, or a field to change it.
        if (editor.renamingPalette == info.id) {
            ImGui::SetNextItemWidth(-1.f);
            const bool entered = ImGui::InputText(
                "##rename", editor.paletteNameBuffer, sizeof(editor.paletteNameBuffer),
                ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
            if (entered || ImGui::IsItemDeactivatedAfterEdit()) {
                if (info.name != editor.paletteNameBuffer &&
                    editor.paletteNameBuffer[0] != '\0') {
                    editor.doc.beginAction("Rename palette");
                    renamePalette(editor.doc, info.id, editor.paletteNameBuffer);
                    editor.doc.endAction();
                }
                editor.renamingPalette = ls::PaletteId{};
            }
            if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                editor.renamingPalette = ls::PaletteId{};
            }
        } else {
            if (ImGui::RadioButton("##use", isDocuments) && !isDocuments) {
                swapPalette(editor, canvas, info.id);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Use this palette for every frame");
            }
            ImGui::SameLine();
            ImGui::Selectable(info.name.c_str(), false,
                              ImGuiSelectableFlags_AllowDoubleClick,
                              ImVec2(ImGui::GetContentRegionAvail().x - 96.f, 0.f));
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                editor.renamingPalette = info.id;
                std::snprintf(editor.paletteNameBuffer, sizeof(editor.paletteNameBuffer),
                              "%s", info.name.c_str());
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%zu colour(s). Double-click to rename.", info.colours);
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("copy")) {
                editor.doc.beginAction("Copy palette");
                const ls::PaletteId made =
                    addPalette(editor.doc, info.name + " copy", info.id);
                editor.doc.endAction();
                if (made.valid()) {
                    editor.say("Copied " + info.name + "; switch to it to edit it");
                }
            }
            ImGui::SameLine();
            ImGui::BeginDisabled(palettes.size() < 2);
            if (ImGui::SmallButton("x")) {
                editor.doc.beginAction("Delete palette");
                if (deletePalette(editor.doc, info.id)) {
                    editor.doc.endAction();
                    refreshInks(editor);
                    canvas.invalidate();
                    editor.say("Deleted " + info.name +
                               "; frames that used it follow the document's");
                } else {
                    editor.doc.abandonAction();
                }
            }
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(palettes.size() < 2
                    ? "The last palette stays."
                    : "Delete. Frames bound to it follow the document's palette.");
            }
        }
        ImGui::PopID();
    }

    if (ImGui::Button("New palette", ImVec2(-1.f, 0.f))) {
        editor.doc.beginAction("New palette");
        const ls::PaletteId made = addPalette(editor.doc, "palette " +
                                              std::to_string(palettes.size() + 1),
                                              documents);
        editor.doc.endAction();
        if (made.valid()) {
            editor.say("New palette, a copy of the current one");
        }
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("A copy of the current palette, to change some slots "
                          "of. Every layer keeps its slots, so the copy is a "
                          "recolour waiting to happen.");
    }

    // The frame's own binding. Per frame, because that is what makes a flash
    // frame, and a palette per frame with the timeline is colour cycling.
    ImGui::Dummy(ImVec2(0.f, 4.f));
    const ls::SpriteId sprite = editor.activeSprite();
    const ls::PaletteId own = frameBinding(editor.doc, sprite);
    const char* ownName = "the document's";
    for (const PaletteInfo& info : palettes) {
        if (info.id == own) { ownName = info.name.c_str(); }
    }
    ImGui::TextUnformatted("This frame");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1.f);
    if (ImGui::BeginCombo("##frame-palette", ownName)) {
        if (ImGui::Selectable("the document's", !own.valid())) {
            if (own.valid()) {
                editor.doc.beginAction("Frame follows the document's palette");
                bindFrame(editor.doc, sprite, ls::PaletteId{});
                editor.doc.endAction();
                refreshInks(editor);
                canvas.invalidate();
            }
        }
        for (const PaletteInfo& info : palettes) {
            ImGui::PushID(static_cast<int>(info.id.value));
            if (ImGui::Selectable(info.name.c_str(), info.id == own) && info.id != own) {
                editor.doc.beginAction("Frame palette");
                bindFrame(editor.doc, sprite, info.id);
                editor.doc.endAction();
                refreshInks(editor);
                canvas.invalidate();
            }
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("A palette for this frame alone. It sits out the "
                          "swap above -- a flash frame -- and one per frame "
                          "is colour cycling.");
    }
}

} // namespace

void drawPalettePanel(Editor& editor, CanvasView& canvas, SDL_Window* window) {
    PaintLayer* layer = editor.active();
    const std::vector<PaletteInfo> palettes = listPalettes(editor.doc);
    const ls::PaletteId documents = documentPalette(editor.doc);
    // The palette this frame draws with is the one the swatches edit.
    const ls::PaletteId shown = paletteFor(editor.doc, editor.activeSprite());
    const std::vector<PaletteEntry> entries = paletteEntries(editor.doc, shown);

    if (palettes.empty() || !shown.valid()) {
        ImGui::TextDisabled("No palette.");
        return;
    }

    drawPaletteQuickRow(editor, canvas, palettes, documents, shown);
    ImGui::Dummy(ImVec2(0.f, 4.f));

    // The ringed slot is the one the left button paints through.
    const ls::ColorRole current = editor.inkRole;
    (void)layer;

    const float swatchSize = 22.f;
    const float spacing = 5.f;
    const float available = ImGui::GetContentRegionAvail().x;
    const int perRow = (available > 0.f)
        ? static_cast<int>((available + spacing) / (swatchSize + spacing))
        : 1;

    int column = 0;
    for (size_t i = 0; i < entries.size(); ++i) {
        const PaletteEntry& entry = entries[i];
        const ImU32 colour = IM_COL32(entry.color.r, entry.color.g, entry.color.b,
                                      entry.color.a);
        const std::string id = "slot" + std::to_string(entry.role);

        if (column > 0 && column < perRow) {
            ImGui::SameLine(0.f, spacing);
        }
        // The ring needs room, or selection clips against the neighbour.
        if (theme::swatch(id.c_str(), colour, entry.role == current, swatchSize)) {
            Ink ink;
            ink.colour = entry.color;
            ink.role = entry.role;
            setForegroundInk(editor, ink);
            editor.say("Painting with slot " + std::to_string(entry.role));
        }
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
            Ink ink;
            ink.colour = entry.color;
            ink.role = entry.role;
            setBackgroundInk(editor, ink);
            editor.say("Right button paints with slot " + std::to_string(entry.role));
        }
        // Drag a swatch onto another to put it there. The order is what the
        // swatches show and what a save keeps; no slot changes number, so
        // nothing painted through one notices.
        if (ImGui::BeginDragDropSource()) {
            const ls::ColorRole dragged = entry.role;
            ImGui::SetDragDropPayload("fast-slot", &dragged, sizeof(dragged));
            ImGui::ColorButton("##dragging", ImGui::ColorConvertU32ToFloat4(colour),
                               ImGuiColorEditFlags_NoTooltip, ImVec2(swatchSize, swatchSize));
            ImGui::EndDragDropSource();
        }
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("fast-slot")) {
                ls::ColorRole dragged = ls::kColorRoleNone;
                std::memcpy(&dragged, payload->Data, sizeof(dragged));
                editor.doc.beginAction("Reorder palette");
                moveSlot(editor.doc, shown, dragged, static_cast<int>(i));
                editor.doc.endAction();
            }
            ImGui::EndDragDropTarget();
        }
        if (ImGui::IsItemHovered()) {
            if (entry.label.empty()) {
                ImGui::SetTooltip("Slot %u  -  #%02X%02X%02X\nClick to paint with it, "
                                  "right-click for the right button.\nDouble-click to edit it.",
                                  entry.role, entry.color.r, entry.color.g, entry.color.b);
            } else {
                ImGui::SetTooltip("%s  (slot %u)  -  #%02X%02X%02X\nClick to paint with it, "
                                  "right-click for the right button.\nDouble-click to edit it.",
                                  entry.label.c_str(), entry.role,
                                  entry.color.r, entry.color.g, entry.color.b);
            }
        }
        // Double-click opens the editor for the entry itself, which changes it
        // for every layer using it.
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            ImGui::OpenPopup(("edit" + id).c_str());
        }
        if (ImGui::BeginPopup(("edit" + id).c_str())) {
            // The name. Typed into a buffer and written on Enter or when the
            // field loses focus, so every keystroke is not an undo step.
            if (editor.renamingSlot != entry.role) {
                editor.renamingSlot = entry.role;
                std::snprintf(editor.slotNameBuffer, sizeof(editor.slotNameBuffer),
                              "%s", entry.label.c_str());
            }
            ImGui::TextDisabled("slot %u", entry.role);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(150.f);
            const bool entered = ImGui::InputTextWithHint(
                "##name", "name", editor.slotNameBuffer, sizeof(editor.slotNameBuffer),
                ImGuiInputTextFlags_EnterReturnsTrue);
            if (entered || ImGui::IsItemDeactivatedAfterEdit()) {
                if (entry.label != editor.slotNameBuffer) {
                    editor.doc.beginAction("Name palette slot");
                    setPaletteLabel(editor.doc, shown, entry.role, editor.slotNameBuffer);
                    editor.doc.endAction();
                }
            }

            float rgba[4];
            fromColor(entry.color, rgba);
            if (ImGui::ColorPicker4("##edit", rgba,
                                    ImGuiColorEditFlags_NoSidePreview |
                                    ImGuiColorEditFlags_DisplayHex)) {
                setPaletteEntry(editor.doc, shown, entry.role, toColor(rgba));
                canvas.invalidate();
                editor.say("Every layer using this slot recoloured");
            }
            bracketDrag(editor, editor.draggingPalette, "Palette colour");

            ImGui::Separator();
            const bool inUse = paletteRoleInUse(editor.doc, entry.role);
            if (editor.confirmRemoveSlot == entry.role) {
                // Something paints through it. Say what happens, and ask.
                ImGui::PushStyleColor(ImGuiCol_Text, theme::palette().danger);
                ImGui::TextWrapped("Layers use this slot. They will keep the "
                                   "colour they show now and stop following "
                                   "the palette.");
                ImGui::PopStyleColor();
                if (ImGui::Button("Remove anyway")) {
                    editor.doc.beginAction("Remove palette slot");
                    removePaletteEntry(editor.doc, shown, entry.role);
                    editor.doc.endAction();
                    editor.confirmRemoveSlot = ls::kColorRoleNone;
                    canvas.invalidate();
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ImGui::Button("Keep it")) {
                    editor.confirmRemoveSlot = ls::kColorRoleNone;
                }
            } else if (ImGui::Button("Remove slot")) {
                if (inUse) {
                    editor.confirmRemoveSlot = entry.role;
                } else {
                    editor.doc.beginAction("Remove palette slot");
                    removePaletteEntry(editor.doc, shown, entry.role);
                    editor.doc.endAction();
                    canvas.invalidate();
                    ImGui::CloseCurrentPopup();
                }
            }
            if (ImGui::IsItemHovered() && inUse && editor.confirmRemoveSlot != entry.role) {
                ImGui::SetTooltip("Something paints through this slot; you will "
                                  "be asked first.");
            }
            ImGui::EndPopup();
        } else if (editor.renamingSlot == entry.role) {
            editor.renamingSlot = ls::kColorRoleNone;      // the popup closed
            editor.confirmRemoveSlot = ls::kColorRoleNone;
        }

        column = (column + 1) % perRow;
        if (column == 0) {
            ImGui::Dummy(ImVec2(0.f, 0.f));   // start the next row
        }
    }

    ImGui::Dummy(ImVec2(0.f, 6.f));

    if (ImGui::Button("Add current colour", ImVec2(-1.f, 0.f))) {
        editor.doc.beginAction("Add palette colour");
        const ls::ColorRole added =
            addPaletteEntry(editor.doc, editor.sprite, toColor(editor.color));
        editor.doc.endAction();
        if (added != ls::kColorRoleNone) {
            // And paint through it from now on: a colour worth keeping in the
            // palette is one whose pixels should follow it.
            editor.inkRole = added;
            editor.say("Added as slot " + std::to_string(added) + "; painting through it");
        }
    }

    // In and out, in the formats other programs use. Loading replaces the
    // palette, which is what loading a palette means everywhere else -- and
    // it recolours a sprite drawn through roles, which is the point of them.
    const float half = (ImGui::GetContentRegionAvail().x - theme::metrics().itemSpacing) * 0.5f;
    if (ImGui::Button("Load...", ImVec2(half, 0.f))) {
        showImportPaletteDialog(editor.files, window, editor.doc);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(".gpl from GIMP or Aseprite, or .hex from Lospec.\n"
                          "Replaces the palette this frame uses; layers drawn "
                          "through slots recolour.");
    }
    ImGui::SameLine();
    if (ImGui::Button("Save...", ImVec2(half, 0.f))) {
        showExportPaletteDialog(editor.files, window, editor.doc);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(".gpl keeps the slot names; .hex, .pal and .act are just "
                          "the colours.");
    }

    // Arranging and building: sort, a ramp between the two colours, an
    // adjustment of the whole palette, and the presets that ship with Fast.
    const float quarter = (ImGui::GetContentRegionAvail().x -
                           theme::metrics().itemSpacing * 3.f) * 0.25f;
    if (ImGui::Button("Sort", ImVec2(quarter, 0.f))) {
        ImGui::OpenPopup("sort-palette");
    }
    if (ImGui::BeginPopup("sort-palette")) {
        const struct { const char* name; PaletteSort by; } sorts[] = {
            { "By hue", PaletteSort::Hue }, { "By saturation", PaletteSort::Saturation },
            { "By lightness", PaletteSort::Lightness }, { "Reverse", PaletteSort::Reverse } };
        for (const auto& sort : sorts) {
            if (ImGui::MenuItem(sort.name)) {
                editor.doc.beginAction("Sort palette");
                sortPalette(editor.doc, shown, sort.by);
                editor.doc.endAction();
                editor.say("Sorted; no slot changed, so nothing on the canvas did");
            }
        }
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    const bool rampable = editor.inkRole != ls::kColorRoleNone &&
                          editor.backRole != ls::kColorRoleNone &&
                          editor.inkRole != editor.backRole;
    ImGui::BeginDisabled(!rampable);
    if (ImGui::Button("Ramp", ImVec2(quarter, 0.f))) {
        ImGui::OpenPopup("ramp-palette");
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip(rampable
            ? "New slots between the left and right colours, evenly spaced."
            : "Pick two palette slots, one for each button, to ramp between.");
    }
    if (ImGui::BeginPopup("ramp-palette")) {
        static int steps = 3;
        ImGui::SetNextItemWidth(120.f);
        ImGui::SliderInt("steps", &steps, 1, 16);
        if (ImGui::Button("Add the ramp")) {
            editor.doc.beginAction("Palette ramp");
            const std::vector<ls::ColorRole> made =
                addRampBetween(editor.doc, shown, editor.inkRole, editor.backRole, steps);
            editor.doc.endAction();
            editor.say("Added " + std::to_string(made.size()) + " slot(s) between them");
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Adjust", ImVec2(quarter, 0.f))) {
        editor.paletteAdjust = Editor::PaletteAdjust{};
        editor.paletteAdjust.base = entries;
        ImGui::OpenPopup("adjust-palette");
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Turn the hue, and push saturation and lightness, of every "
                          "slot at once -- a recolour of everything painted through "
                          "them, from the drawing.");
    }
    if (ImGui::BeginPopup("adjust-palette")) {
        Editor::PaletteAdjust& adjust = editor.paletteAdjust;
        bool changed = false;
        ImGui::SetNextItemWidth(200.f);
        changed |= ImGui::SliderFloat("hue", &adjust.hue, -180.f, 180.f, "%.0f deg");
        bracketDrag(editor, editor.draggingPalette, "Adjust palette");
        ImGui::SetNextItemWidth(200.f);
        changed |= ImGui::SliderFloat("saturation", &adjust.saturation, -1.f, 1.f, "%.2f");
        bracketDrag(editor, editor.draggingPalette, "Adjust palette");
        ImGui::SetNextItemWidth(200.f);
        changed |= ImGui::SliderFloat("lightness", &adjust.lightness, -1.f, 1.f, "%.2f");
        bracketDrag(editor, editor.draggingPalette, "Adjust palette");
        if (changed) {
            adjustPalette(editor.doc, shown, adjust.base, adjust.hue, adjust.saturation,
                          adjust.lightness);
            refreshInks(editor);
            canvas.invalidate();
        }
        ImGui::TextDisabled("From the palette as it was when this opened.");
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Presets", ImVec2(quarter, 0.f))) {
        ImGui::OpenPopup("palette-presets");
    }
    if (ImGui::BeginPopup("palette-presets")) {
        ImGui::TextDisabled("Replaces this palette, as loading a file does.");
        for (const PalettePreset& preset : palettePresets()) {
            const std::string label = preset.name + "  (" +
                                      std::to_string(preset.colours.size()) + ")";
            if (ImGui::MenuItem(label.c_str())) {
                PaletteFile file;
                file.name = preset.name;
                for (size_t n = 0; n < preset.colours.size(); ++n) {
                    file.entries.push_back({ static_cast<ls::ColorRole>(n), preset.colours[n], "" });
                }
                int dropped = 0;
                if (applyPaletteFile(editor.doc, editor.activeSprite(), file, &dropped)) {
                    refreshInks(editor);
                    canvas.invalidate();
                    editor.say("Loaded " + preset.name +
                               (dropped > 0 ? "; " + std::to_string(dropped) +
                                                  " slot(s) in use were not in it"
                                            : std::string()));
                }
            }
        }
        ImGui::EndPopup();
    }

    if (ImGui::Button("Make every colour a slot", ImVec2(-1.f, 0.f))) {
        editor.doc.beginAction("Colours to slots");
        const int made = slotsFromColours(editor.doc, shown);
        if (made > 0) {
            editor.doc.endAction();
            editor.say(std::to_string(made) + " colour(s) now paint through the palette");
        } else {
            editor.doc.abandonAction();
            editor.say("Every colour already paints through the palette");
        }
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Every colour painted as a value of its own, on every "
                          "frame, becomes a palette slot -- one that already has "
                          "the colour, or a new one. After this the palette "
                          "recolours the whole sprite.");
    }

    if (current != ls::kColorRoleNone) {
        ImGui::PushStyleColor(ImGuiCol_Text, theme::palette().textDim);
        ImGui::TextWrapped("Editing a swatch changes every layer that uses it, "
                           "from the drawing rather than over it.");
        ImGui::PopStyleColor();
    }

    ImGui::Dummy(ImVec2(0.f, 6.f));
    drawPaletteList(editor, canvas, palettes, documents);
}

// ------------------------------------------------------------------ shape --

namespace {

// What an element looks like, for its row: the slot's colour when it paints
// through one, its own otherwise, and the middle of the ramp for a dither.
ImU32 elementChip(Editor& editor, ls::LayerId layer, const Element& element) {
    PaintLayer as;
    as.layer = layer;
    as.fill = element.fill;
    as.region = element.region;
    const ls::Color c = effectiveLayerColor(editor.doc, editor.sprite, as);
    return IM_COL32(c.r, c.g, c.b, c.a == 0 ? 255 : c.a);
}

std::string elementLabel(Editor& editor, const Element& element) {
    std::string label = elementKindName(element.kind);
    if (element.kind != ElementKind::Paint) {
        return element.outlined ? label + "  outline" : label;
    }
    Ink ink;
    if (!inkOfElement(editor.doc, element.fill, &ink)) {
        return "Dither";
    }
    if (ink.usesSlot()) {
        return label + "  slot " + std::to_string(ink.role);
    }
    char hex[16];
    std::snprintf(hex, sizeof(hex), "  #%02X%02X%02X", ink.colour.r, ink.colour.g,
                  ink.colour.b);
    return label + hex;
}

// The selected pixels: what colours them, and the controls for changing it
// without touching what was drawn.
void drawPixelsProperties(Editor& editor, CanvasView& canvas, PaintLayer target) {
    // Solid or dithered is a property of this element, since it is the rule
    // that colours this drawing. Switching does not touch the drawing.
    bool dithered = layerIsDithered(editor.doc, target);
    if (ImGui::Checkbox("Dithered fill", &dithered)) {
        editor.doc.beginAction(dithered ? "Dither the pixels" : "Solid fill");
        Ink was;
        const bool hadInk = inkOfElement(editor.doc, target.fill, &was);
        if (dithered) {
            DitherSettings settings = editor.dither;
            // Start from the colour it was, so the switch is a change of rule
            // rather than a change of colour as well.
            if (hadInk) {
                settings.from = was.colour;
                settings.fromRole = was.role;
            }
            setLayerDithered(editor.doc, target, settings);
        } else {
            const Ink ink = foregroundInk(editor);
            setLayerSolid(editor.doc, target, ink.colour);
            setElementInk(editor.doc, target.fill, ink);
        }
        editor.doc.endAction();
        editor.activeElement = target.fill;
        editor.paintIntoElement = dithered;
        resyncLayers(editor);
        canvas.invalidate();
        editor.say(dithered ? "The drawing is unchanged; only the rule that "
                              "colours it is different"
                            : "Back to a solid fill, drawing intact");
    }
    ImGui::SameLine();
    theme::hint("A dither compares a value against a threshold matrix and picks "
                "between two ramp stops. Switching back and forth costs "
                "nothing: the drawing is never touched.");

    if (dithered) {
        ImGui::Checkbox("Paint with this dither", &editor.paintIntoElement);
        ImGui::SameLine();
        theme::hint("On: the pencil and the bucket paint into this element, so "
                    "what you draw takes the dither. Off: they paint the "
                    "current colour, as usual.");
        ImGui::Dummy(ImVec2(0.f, 4.f));
        drawDitherControls(editor, canvas, target);
        return;
    }

    Ink ink;
    if (!inkOfElement(editor.doc, target.fill, &ink)) {
        return;
    }

    if (ink.usesSlot()) {
        const ls::PaletteId palette = paletteFor(editor.doc, editor.activeSprite());
        ls::Color resolved;
        if (!resolvePaletteRole(editor.doc, palette, ink.role, &resolved)) {
            // A slot the palette no longer has. The pixels still draw -- in
            // the colour they showed when the slot went -- and putting the
            // slot back at that colour changes nothing and re-attaches every
            // element that named it, on every frame.
            ImGui::PushStyleColor(ImGuiCol_Text, theme::palette().textDim);
            ImGui::TextWrapped("These pixels name slot %u, which the palette no "
                               "longer has, so they show their own colour.",
                               ink.role);
            ImGui::PopStyleColor();
            if (ImGui::Button("Put the slot back", ImVec2(-1.f, 0.f))) {
                editor.doc.beginAction("Restore palette slot");
                setPaletteEntry(editor.doc, palette, ink.role, ink.colour);
                editor.doc.endAction();
                canvas.invalidate();
                editor.say("Slot " + std::to_string(ink.role) +
                           " is back; everything that named it follows it again");
            }
        } else {
            ImGui::PushStyleColor(ImGuiCol_Text, theme::palette().textDim);
            ImGui::TextWrapped("Painted through slot %u. Edit the swatch to "
                               "recolour everything using it.", ink.role);
            ImGui::PopStyleColor();
            if (ImGui::SmallButton("Use its own colour")) {
                // Keep what is on screen: detaching should not move the colour.
                Ink own;
                own.colour = resolved;
                editor.doc.beginAction("Detach from palette");
                setElementInk(editor.doc, target.fill, own);
                editor.doc.endAction();
                canvas.invalidate();
                editor.say("These pixels now carry their own colour");
            }
        }
    } else {
        float rgba[4];
        fromColor(ink.colour, rgba);
        if (ImGui::ColorEdit4("colour##pixels", rgba, ImGuiColorEditFlags_NoInputs)) {
            Ink changed = ink;
            changed.colour = toColor(rgba);
            setElementInk(editor.doc, target.fill, changed);
            canvas.invalidate();
            editor.say("Recoloured without touching the drawing");
        }
        bracketDrag(editor, editor.recolouring, "Recolour");
    }

    ImGui::SameLine();
    if (ImGui::SmallButton("= current")) {
        editor.doc.beginAction("Recolour pixels");
        setElementInk(editor.doc, target.fill, foregroundInk(editor));
        editor.doc.endAction();
        canvas.invalidate();
        editor.say("These pixels take the current colour; the drawing is untouched");
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Give these pixels the current colour -- its slot, when "
                          "it has one. A replace-colour that can be changed back.");
    }
}

} // namespace

void drawShapePanel(Editor& editor, CanvasView& canvas) {
    PaintLayer* layer = editor.active();
    if (layer == nullptr) {
        ImGui::TextDisabled("No layer selected.");
        return;
    }

    // The elements of the layer, one row each: a colour of pixels, or a shape.
    // The selected one is what the controls below edit.
    const std::vector<Element> elements = elementsOf(editor.doc, layer->layer);
    const Element* selected = nullptr;
    for (const Element& element : elements) {
        if (element.fill == editor.activeElement) { selected = &element; }
    }
    if (selected == nullptr && !elements.empty()) {
        selected = &elements.front();
        editor.activeElement = selected->fill;
        editor.paintIntoElement = false;
    }
    if (elements.size() > 1) {
        theme::sectionHeader("ELEMENTS");
        // Topmost first, the way the layer stack reads.
        for (size_t n = elements.size(); n-- > 0;) {
            const Element& element = elements[n];
            ImGui::PushID(static_cast<int>(element.fill.value));
            ImGui::ColorButton("##chip", ImGui::ColorConvertU32ToFloat4(
                                   elementChip(editor, layer->layer, element)),
                               ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoBorder,
                               ImVec2(12.f, 12.f));
            ImGui::SameLine();
            const std::string label = elementLabel(editor, element) + "##" +
                                      std::to_string(n);
            if (ImGui::Selectable(label.c_str(), &element == selected,
                                  0, ImVec2(ImGui::GetContentRegionAvail().x - 26.f, 0.f))) {
                if (editor.activeElement != element.fill) {
                    editor.paintIntoElement = false;
                }
                editor.activeElement = element.fill;
                selected = &element;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("x")) {
                if (removeElement(editor.doc, layer->layer, element)) {
                    editor.activeElement = ls::OperationId{};
                    editor.paintIntoElement = false;
                    resyncLayers(editor);
                    canvas.invalidate();
                    editor.say("Element removed; the rest of the layer is untouched");
                    ImGui::PopID();
                    return;
                }
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Remove this element. The others stay.");
            }
            ImGui::PopID();
        }
        ImGui::SameLine();
        theme::hint("A layer holds several marks: one element per colour its "
                    "pixels are painted in, and any shapes drawn onto it. Each "
                    "shape stays a shape; each colour stays a rule, so it can be "
                    "changed without repainting.");
        ImGui::Dummy(ImVec2(0.f, 4.f));
    }

    ShapeLayer shape;
    const bool isShape = selected != nullptr && selected->isShape();
    if (isShape) {
        shape = shapeOfElement(layer->layer, *selected);
    }

    if (isShape) {
        ShapeParams params;
        if (readShapeParams(editor.doc, shape, &params)) {
            theme::sectionHeader(shapeKindName(shape.kind));

            bool changed = false;
            float from[2] = { params.from.x, params.from.y };
            float to[2]   = { params.to.x, params.to.y };

            ImGui::SetNextItemWidth(-42.f);
            if (ImGui::DragFloat2("from", from, 0.25f, 0.f, 0.f, "%.0f")) {
                params.from = { from[0], from[1] };
                changed = true;
            }
            bracketDrag(editor, editor.editingShape, "Edit shape");

            ImGui::SetNextItemWidth(-42.f);
            if (ImGui::DragFloat2("to", to, 0.25f, 0.f, 0.f, "%.0f")) {
                params.to = { to[0], to[1] };
                changed = true;
            }

            if (shape.kind == ShapeKind::Rectangle) {
                // The rectangle's own radius, read back from its geometry. A
                // radius set here is also the one the next rectangle starts
                // with.
                ImGui::SetNextItemWidth(-42.f);
                if (ImGui::SliderFloat("round", &params.cornerRadius, 0.f, 12.f,
                                       "%.1f")) {
                    editor.shapeCorner = params.cornerRadius;
                    changed = true;
                }
                bracketDrag(editor, editor.editingShape, "Corner radius");
            }

            if (changed) {
                updateShape(editor.doc, shape, params);
                canvas.invalidate();
                editor.say("The shape is still a shape");
            }

            // Its area, or its edge: the same shape either way.
            if (shape.kind != ShapeKind::Line) {
                float width = 1.f;
                bool outlined = shapeIsOutlined(editor.doc, selected->fill, &width);
                ls::OperationId op = selected->fill;
                if (ImGui::Checkbox("Outline only##element", &outlined)) {
                    editor.doc.beginAction(outlined ? "Outline the shape" : "Fill the shape");
                    setShapeOutlined(editor.doc, layer->layer, &op, outlined, width);
                    editor.doc.endAction();
                    editor.activeElement = op;
                    resyncLayers(editor);
                    canvas.invalidate();
                    return;
                }
                if (outlined) {
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(-1.f);
                    if (ImGui::SliderFloat("##elementwidth", &width, 1.f, 8.f, "width %.0f")) {
                        setShapeOutlined(editor.doc, layer->layer, &op, true, std::round(width));
                        canvas.invalidate();
                    }
                    bracketDrag(editor, editor.editingShape, "Outline width");
                }
            }

            if (ImGui::SmallButton("Current colour")) {
                editor.doc.beginAction("Recolour shape");
                const Ink ink = foregroundInk(editor);
                editor.doc.engine().setOperationParameter(
                    selected->fill, "fallbackColor", ls::ParameterValue{ink.colour});
                editor.doc.engine().setOperationParameter(
                    selected->fill, "paletteRole",
                    ls::ParameterValue{static_cast<int64_t>(ink.role)});
                editor.doc.endAction();
                canvas.invalidate();
                editor.say("The shape takes the current colour");
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Give this shape the current colour -- its slot, "
                                  "when it has one.");
            }
        }
    } else if (selected != nullptr) {
        PaintLayer target;
        target.layer = layer->layer;
        target.fill = selected->fill;
        target.region = selected->region;
        theme::sectionHeader("PIXELS");
        drawPixelsProperties(editor, canvas, target);
    }

    ImGui::Dummy(ImVec2(0.f, theme::metrics().sectionGap));
    theme::sectionHeader("OUTLINE");

    const bool wasOutlined = hasOutline(editor.doc, *layer);
    bool outlined = wasOutlined;
    if (ImGui::Checkbox("Outline", &outlined)) {
        editor.doc.beginAction(outlined ? "Add outline" : "Remove outline");
        if (outlined) {
            setOutline(editor.doc, *layer, OutlineSettings{});
        } else {
            removeOutline(editor.doc, *layer);
        }
        editor.doc.endAction();
        canvas.invalidate();
    }
    ImGui::SameLine();
    theme::hint("Generated during the compile from whatever is drawn, so it "
                "follows the artwork instead of being stamped where the artwork "
                "used to be. Move the shape and the outline moves.");

    if (!outlined) {
        return;
    }

    OutlineSettings settings = outlineOf(editor.doc, *layer);
    const OutlineSettings before = settings;

    // What it goes round. These are two different pictures rather than a
    // preference, so they are named for what they trace rather than offered as
    // a checkbox that says "whole sprite".
    const float half = (ImGui::GetContentRegionAvail().x -
                        theme::metrics().itemSpacing) * 0.5f;

    const char* scopes[] = { "This layer", "Whole sprite" };
    int scope = static_cast<int>(settings.scope);
    ImGui::SetNextItemWidth(half);
    if (ImGui::Combo("##scope", &scope, scopes, 2)) {
        settings.scope = static_cast<OutlineScope>(scope);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("This layer: a line round what this layer draws, which\n"
                          "is what one part of a character wants.\n"
                          "Whole sprite: one line round the figure however many\n"
                          "layers it is built from, with no seam where they meet.");
    }

    // The order is the engine's, not one chosen here: Inside, Outside, Center.
    // Writing the labels in a different order would silently mean the wrong
    // thing, which is exactly what happened the first time.
    const char* sides[] = { "Inside", "Outside", "Centred" };
    int side = static_cast<int>(settings.side);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(half);
    if (ImGui::Combo("##side", &side, sides, 3)) {
        settings.side = static_cast<ls::OutlineSide>(side);
    }

    ImGui::SetNextItemWidth(-1.f);
    ImGui::SliderInt("##thickness", &settings.thickness, 1, kMaxOutlineThickness,
                     "thickness  %d");
    bracketDrag(editor, editor.editingShape, "Outline thickness");

    // The colour, as a value or as a palette slot. A slot is the better answer
    // when there is one: the outline then joins a palette swap instead of being
    // the one thing left behind by it.
    float rgba[4];
    fromColor(settings.colour, rgba);
    if (ImGui::ColorEdit4("colour", rgba, ImGuiColorEditFlags_NoInputs)) {
        settings.colour = toColor(rgba);
        settings.role = ls::kColorRoleNone;      // a picked colour is a value
    }
    bracketDrag(editor, editor.editingShape, "Outline colour");

    ImGui::SameLine();
    const bool usingRole = settings.role != ls::kColorRoleNone;
    if (usingRole) {
        const std::string label = "slot " + std::to_string(settings.role);
        ImGui::TextColored(theme::palette().accent, "%s", label.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("detach")) {
            settings.role = ls::kColorRoleNone;
        }
    } else if (ImGui::SmallButton("use a palette slot")) {
        // The slot the layer already paints through, so the outline and the
        // fill move together under a palette swap unless told otherwise.
        const ls::ColorRole fillRole = layerRole(editor.doc, *layer);
        settings.role = fillRole != ls::kColorRoleNone ? fillRole : 0;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Take the colour from a palette slot, so changing what "
                          "the slot means recolours every outline using it -- "
                          "from the drawing rather than over it.");
    }

    // Field by field rather than memcmp: the struct has padding, and comparing
    // padding is comparing whatever happened to be on the stack.
    const bool changed =
        settings.scope != before.scope ||
        settings.thickness != before.thickness ||
        settings.side != before.side ||
        settings.role != before.role ||
        settings.colour.r != before.colour.r ||
        settings.colour.g != before.colour.g ||
        settings.colour.b != before.colour.b ||
        settings.colour.a != before.colour.a;
    if (changed) {
        setOutline(editor.doc, *layer, settings);
        canvas.invalidate();
    }
}

// ----------------------------------------------------------------- layers --

namespace {

// A layer's picture at thumbnail size over the chequer, the frame's shape
// kept: a wide canvas gives a wide thumbnail, not a squashed one. Hidden
// layers draw faint so the eye can find them without the checkbox.
void drawLayerThumbnail(Editor& editor, CanvasView& canvas, ls::SpriteId sprite,
                        ls::LayerId layer, bool visible) {
    constexpr float kBox = 22.f;
    const FrameCache::Entry* entry = canvas.frames().entryForLayer(editor.doc, sprite, layer);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 at = ImGui::GetCursorScreenPos();
    const theme::Palette& c = theme::palette();
    draw->AddRectFilled(at, ImVec2(at.x + kBox, at.y + kBox), ImGui::GetColorU32(c.checkerDark), 2.f);
    for (int y = 0; y < 4; ++y) {
        for (int x = (y % 2); x < 4; x += 2) {
            const float cell = kBox / 4.f;
            draw->AddRectFilled(ImVec2(at.x + x * cell, at.y + y * cell),
                                ImVec2(at.x + (x + 1) * cell, at.y + (y + 1) * cell),
                                ImGui::GetColorU32(c.checkerLight));
        }
    }
    if (entry != nullptr && entry->width > 0 && entry->height > 0) {
        const float scale = std::min(kBox / static_cast<float>(entry->width),
                                     kBox / static_cast<float>(entry->height));
        const float w = static_cast<float>(entry->width) * scale;
        const float h = static_cast<float>(entry->height) * scale;
        const ImVec2 origin(at.x + (kBox - w) * 0.5f, at.y + (kBox - h) * 0.5f);
        canvas.drawFrameTinted(draw, *entry, origin, scale,
                               visible ? IM_COL32_WHITE : IM_COL32(255, 255, 255, 90));
    }
    ImGui::Dummy(ImVec2(kBox, kBox));
}

} // namespace

void drawLayerPanel(Editor& editor, CanvasView& canvas) {
    const ls::SpriteId sprite = editor.activeSprite();
    const std::vector<ls::LayerId> order = layerOrder(editor.doc, sprite);
    // Thumbnails for the frame being shown; the others' are dropped, since a
    // frame change is the one time many textures would otherwise pile up.
    canvas.frames().retainOnlyLayers(order);

    // --- the buttons ---------------------------------------------------------
    if (ImGui::Button("Add", ImVec2(52.f, 0.f))) {
        PaintLayer layer;
        const std::string name = "Layer " + std::to_string(order.size() + 1);
        if (createPaintLayer(editor.doc, sprite, name, toColor(editor.color), &layer)) {
            editor.doc.beginAction("Add layer");
            if (editor.activeGroup.valid()) {
                // The group's row is selected: the new layer goes in it, on top.
                addToGroup(editor.doc, layer.layer, editor.activeGroup);
            } else if (PaintLayer* active = editor.active()) {
                // Above the active layer, but outside its group. Above and
                // inside is where a plain "add" used to land whenever the
                // active layer was grouped, and there was then no way to make
                // one that was not. Into the group is a drop or a menu away.
                const ls::GroupId group = groupOf(editor.doc, active->layer);
                int at = indexOfLayer(editor.doc, sprite, active->layer) + 1;
                if (group.valid()) {
                    const std::vector<ls::LayerId> now = layerOrder(editor.doc, sprite);
                    for (size_t i = 0; i < now.size(); ++i) {
                        if (groupOf(editor.doc, now[i]) == group) {
                            at = static_cast<int>(i) + 1;
                        }
                    }
                }
                moveLayer(editor.doc, layer.layer, at);
                if (groupOf(editor.doc, layer.layer).valid()) {
                    removeFromGroup(editor.doc, layer.layer);
                }
            }
            editor.doc.endAction();
            selectLayer(editor, layer.layer);
            canvas.invalidate();
        }
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(editor.activeGroup.valid()
            ? "A new layer inside the selected group"
            : "A new layer above the active one, outside its group.\n"
              "Select a "
              "group's row to add inside it.");
    }
    ImGui::SameLine();
    if (ImGui::Button("Dup", ImVec2(46.f, 0.f))) {
        duplicateActiveLayer(editor, canvas);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Duplicate the active layer  (Ctrl+J)");
    }
    ImGui::SameLine();
    const bool canDelete = order.size() > 1;
    ImGui::BeginDisabled(!canDelete);
    if (ImGui::Button("Delete", ImVec2(56.f, 0.f))) {
        deleteSelectedLayers(editor, canvas);
    }
    ImGui::EndDisabled();
    if (!canDelete && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("A sprite needs at least one layer.");
    }
    ImGui::SameLine();
    if (ImGui::Button("^", ImVec2(22.f, 0.f))) { raiseActiveLayer(editor, canvas); }
    if (ImGui::IsItemHovered()) { ImGui::SetTooltip("Move up  (Ctrl+])"); }
    ImGui::SameLine();
    if (ImGui::Button("v", ImVec2(22.f, 0.f))) { lowerActiveLayer(editor, canvas); }
    if (ImGui::IsItemHovered()) { ImGui::SetTooltip("Move down  (Ctrl+[)"); }

    // --- the properties strip -------------------------------------------------
    //
    // Blend and opacity of whatever is selected: the active layer, or the
    // group whose row was clicked. A group's opacity is not its layers'
    // opacities -- the group composites as one and then blends -- which is
    // why the row exists rather than the group being a folder.
    ImGui::Dummy(ImVec2(0.f, 2.f));
    {
        const bool onGroup = editor.activeGroup.valid();
        LayerProps layerProps;
        GroupProps groupProps;
        bool have = onGroup ? readGroupProps(editor.doc, editor.activeGroup, &groupProps)
                            : (editor.active() != nullptr &&
                               readLayerProps(editor.doc, editor.active()->layer, &layerProps));
        if (have) {
            int blend = static_cast<int>(onGroup ? groupProps.blend : layerProps.blend);
            float opacity = onGroup ? groupProps.opacity : layerProps.opacity;
            ImGui::SetNextItemWidth(104.f);
            if (ImGui::Combo("##blend", &blend, blendModeNames().data(),
                             static_cast<int>(blendModeNames().size()))) {
                editor.doc.beginAction("Blend mode");
                if (onGroup) {
                    setGroupBlend(editor.doc, editor.activeGroup, static_cast<ls::BlendMode>(blend));
                } else {
                    setLayerBlend(editor.doc, editor.active()->layer, static_cast<ls::BlendMode>(blend));
                }
                editor.doc.endAction();
                canvas.invalidate();
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(onGroup ? "How the group, composited as one, lands on what is below"
                                          : "How this layer lands on what is below it");
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(-1.f);
            if (ImGui::SliderFloat("##opacity", &opacity, 0.f, 1.f, "opacity %.2f")) {
                if (onGroup) {
                    setGroupOpacity(editor.doc, editor.activeGroup, opacity);
                } else {
                    setLayerOpacity(editor.doc, editor.active()->layer, opacity);
                }
                canvas.invalidate();
            }
            bracketDrag(editor, editor.draggingLayer, onGroup ? "Group opacity" : "Layer opacity");
            if (!onGroup) {
                bool clipped = layerProps.clipBase.valid();
                if (ImGui::Checkbox("clip to below", &clipped)) {
                    toggleActiveLayerClip(editor, canvas);
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Draw only where the layer below draws: a "
                                      "highlight that stays inside the body. A move "
                                      "clears it, because \"below\" changed.");
                }
                ImGui::SameLine();
                bool locked = layerProps.locked;
                if (ImGui::Checkbox("lock", &locked)) {
                    toggleActiveLayerLock(editor);
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Tools leave a locked layer alone.");
                }
            }
        }
    }
    ImGui::Dummy(ImVec2(0.f, 4.f));

    // --- the stack --------------------------------------------------------------
    //
    // Topmost first, which is how a layer stack reads everywhere else. Groups
    // are runs in the engine's order, so a group row is drawn when its first
    // member (from the top) is met, and its members indent under it.
    const auto isCollapsed = [&](ls::GroupId group) {
        return std::find(editor.collapsedGroups.begin(), editor.collapsedGroups.end(),
                         group.value) != editor.collapsedGroups.end();
    };
    const auto listIndexOf = [&](ls::LayerId id) {
        for (size_t i = 0; i < editor.layers.size(); ++i) {
            if (editor.layers[i].layer == id) { return static_cast<int>(i); }
        }
        return -1;
    };
    // A drop target: the row's position in the engine's order.
    const auto acceptDrop = [&](int toIndex) {
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("layer")) {
                const ls::LayerId moving{ *static_cast<const uint64_t*>(payload->Data) };
                if (moveLayer(editor.doc, moving, toIndex)) {
                    selectLayer(editor, moving);
                    canvas.invalidate();
                }
            }
            ImGui::EndDragDropTarget();
        }
    };

    ls::GroupId openGroup;
    bool openCollapsed = false;
    for (int i = static_cast<int>(order.size()) - 1; i >= 0; --i) {
        const ls::LayerId id = order[static_cast<size_t>(i)];
        LayerProps props;
        if (!readLayerProps(editor.doc, id, &props)) {
            continue;
        }
        ImGui::PushID(static_cast<int>(id.value));

        // A group row, the first time a member is met from the top.
        if (props.group != openGroup) {
            openGroup = props.group;
            openCollapsed = false;
            if (openGroup.valid()) {
                GroupProps group;
                readGroupProps(editor.doc, openGroup, &group);
                openCollapsed = isCollapsed(openGroup);
                ImGui::PushID("group");
                if (ImGui::ArrowButton("##fold", openCollapsed ? ImGuiDir_Right : ImGuiDir_Down)) {
                    if (openCollapsed) {
                        editor.collapsedGroups.erase(
                            std::remove(editor.collapsedGroups.begin(), editor.collapsedGroups.end(),
                                        openGroup.value), editor.collapsedGroups.end());
                    } else {
                        editor.collapsedGroups.push_back(openGroup.value);
                    }
                    openCollapsed = !openCollapsed;
                }
                ImGui::SameLine();
                if (theme::eyeToggle("##gvisible", group.visible, 18.f)) {
                    editor.doc.beginAction(group.visible ? "Hide group" : "Show group");
                    setGroupVisible(editor.doc, openGroup, !group.visible);
                    editor.doc.endAction();
                    canvas.invalidate();
                }
                ImGui::SameLine();
                if (editor.renamingGroup == openGroup) {
                    ImGui::SetNextItemWidth(-1.f);
                    if (ImGui::InputText("##grename", editor.groupNameBuffer,
                                         sizeof(editor.groupNameBuffer),
                                         ImGuiInputTextFlags_EnterReturnsTrue)) {
                        editor.doc.beginAction("Rename group");
                        renameGroup(editor.doc, openGroup, editor.groupNameBuffer);
                        editor.doc.endAction();
                        editor.renamingGroup = ls::GroupId{};
                    }
                    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                        editor.renamingGroup = ls::GroupId{};
                    }
                } else {
                    const std::string label = group.name + "  (" +
                                              std::to_string(group.layers.size()) + ")";
                    if (ImGui::Selectable(label.c_str(), editor.activeGroup == openGroup)) {
                        editor.activeGroup = openGroup;
                        editor.selectedLayers.clear();
                        for (ls::LayerId member : group.layers) {
                            editor.selectedLayers.push_back(member);
                        }
                    }
                    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                        editor.renamingGroup = openGroup;
                        std::snprintf(editor.groupNameBuffer, sizeof(editor.groupNameBuffer),
                                      "%s", group.name.c_str());
                    }
                    // Dropping on the group row puts the layer in the group,
                    // at the top of it -- the obvious way in.
                    if (ImGui::BeginDragDropTarget()) {
                        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("layer")) {
                            const ls::LayerId moving{ *static_cast<const uint64_t*>(payload->Data) };
                            if (addToGroup(editor.doc, moving, openGroup)) {
                                selectLayer(editor, moving);
                                canvas.invalidate();
                                editor.say("Added to " + group.name);
                            }
                        }
                        ImGui::EndDragDropTarget();
                    }
                    if (ImGui::BeginPopupContextItem("gmenu")) {
                        if (ImGui::MenuItem("Ungroup", "Ctrl+Shift+G")) {
                            editor.activeGroup = openGroup;
                            ungroupActiveLayer(editor, canvas);
                        }
                        if (ImGui::MenuItem("Rename")) {
                            editor.renamingGroup = openGroup;
                            std::snprintf(editor.groupNameBuffer, sizeof(editor.groupNameBuffer),
                                          "%s", group.name.c_str());
                        }
                        ImGui::EndPopup();
                    }
                }
                ImGui::PopID();
            }
        }
        if (openGroup.valid() && openCollapsed) {
            ImGui::PopID();
            continue;
        }

        // The layer row.
        if (openGroup.valid()) {
            ImGui::Indent(18.f);
        }
        const bool visible = props.visible;
        if (theme::eyeToggle("##visible", visible, 18.f)) {
            if (ImGui::GetIO().KeyAlt) {
                // Alt: this layer alone -- or, when it already is, all of them
                // again. The quickest way to see what one layer draws.
                const std::vector<ls::LayerId> all = layerOrder(editor.doc, editor.sprite);
                bool alone = visible;
                for (ls::LayerId other : all) {
                    LayerProps otherProps;
                    if (other != id && readLayerProps(editor.doc, other, &otherProps) && otherProps.visible) {
                        alone = false;
                    }
                }
                editor.doc.beginAction(alone ? "Show every layer" : "Show one layer");
                for (ls::LayerId other : all) {
                    setLayerVisible(editor.doc, other, alone || other == id);
                }
                editor.doc.endAction();
            } else {
                editor.doc.beginAction(visible ? "Hide layer" : "Show layer");
                setLayerVisible(editor.doc, id, !visible);
                editor.doc.endAction();
            }
            canvas.invalidate();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s this layer\nAlt+click: this layer alone, or all again",
                              visible ? "Hide" : "Show");
        }
        ImGui::SameLine();

        const int listIndex = listIndexOf(id);
        const bool drawable = listIndex >= 0;
        if (editor.renaming == listIndex && drawable) {
            ImGui::SetNextItemWidth(-1.f);
            if (ImGui::IsWindowAppearing() || ImGui::IsItemDeactivated()) {
                ImGui::SetKeyboardFocusHere();
            }
            if (ImGui::InputText("##rename", editor.renameBuffer,
                                 sizeof(editor.renameBuffer),
                                 ImGuiInputTextFlags_EnterReturnsTrue)) {
                editor.doc.beginAction("Rename layer");
                renameLayer(editor.doc, id, editor.renameBuffer);
                editor.doc.endAction();
                editor.renaming = -1;
            }
            if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                editor.renaming = -1;
            }
        } else {
            // What the layer draws, small, so the stack can be read at a
            // glance rather than by hiding each one. Compiled on its own and
            // cached until it changes.
            drawLayerThumbnail(editor, canvas, sprite, id, props.visible);
            ImGui::SameLine();

            std::string label = props.name;
            if (props.locked) { label += "  [lock]"; }
            if (props.clipBase.valid()) { label += "  [clip]"; }
            if (props.blend != ls::BlendMode::Normal || props.opacity < 1.f) {
                char detail[48];
                std::snprintf(detail, sizeof(detail), "  %s %d%%",
                              props.blend != ls::BlendMode::Normal
                                  ? blendModeNames()[static_cast<int>(props.blend)] : "",
                              static_cast<int>(props.opacity * 100.f + 0.5f));
                label += detail;
            }
            if (!drawable) { label += "  (not editable here)"; }

            const bool selected = drawable && !editor.activeGroup.valid() && layerSelected(editor, id);
            ImGui::BeginDisabled(!drawable);
            // As tall as the thumbnail, so the highlight covers the row.
            if (ImGui::Selectable(label.c_str(), selected, 0, ImVec2(0.f, 22.f))) {
                selectLayer(editor, id, ImGui::GetIO().KeyCtrl);
            }
            ImGui::EndDisabled();
            if (drawable && ImGui::IsItemHovered() &&
                ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                editor.renaming = listIndex;
                std::snprintf(editor.renameBuffer, sizeof(editor.renameBuffer),
                              "%s", props.name.c_str());
            }
            if (drawable && ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceNoPreviewTooltip)) {
                const uint64_t handle = id.value;
                ImGui::SetDragDropPayload("layer", &handle, sizeof(handle));
                ImGui::TextUnformatted(props.name.c_str());
                ImGui::TextDisabled("Drop on a row to move there. On a group's row to join it.\n"
                                    "Hold Ctrl to make a group of the two.");
                ImGui::EndDragDropSource();
            }
            // A drop with Ctrl held groups the two; without, it is a move,
            // and a move into a group's run joins the group.
            if (ImGui::GetIO().KeyCtrl && ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("layer")) {
                    const ls::LayerId moving{ *static_cast<const uint64_t*>(payload->Data) };
                    if (moving != id) {
                        const ls::GroupId made = groupLayers(
                            editor.doc, { id, moving },
                            "Group " + std::to_string(groupOrder(editor.doc, sprite).size() + 1));
                        if (made.valid()) {
                            selectLayer(editor, moving);
                            editor.selectedLayers.push_back(id);
                            canvas.invalidate();
                            editor.say("Grouped the two");
                        }
                    }
                }
                ImGui::EndDragDropTarget();
            } else {
                acceptDrop(i);
            }

            if (drawable && ImGui::BeginPopupContextItem("menu")) {
                if (!layerSelected(editor, id) || editor.activeGroup.valid()) {
                    selectLayer(editor, id);
                }
                if (ImGui::MenuItem("Duplicate", "Ctrl+J")) { duplicateActiveLayer(editor, canvas); }
                if (ImGui::MenuItem("Merge down", "Ctrl+E")) { mergeActiveLayerDown(editor, canvas); }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("This layer's elements join the layer below, on top "
                                      "of its own, each still what it was.");
                }
                if (ImGui::MenuItem("Copy", "Ctrl+C")) { copyActiveLayer(editor); }
                if (ImGui::MenuItem("Paste above", "Ctrl+V", false, editor.clipboard.valid())) {
                    pasteLayerHere(editor, canvas);
                }
                if (ImGui::MenuItem("Delete", nullptr, false, order.size() > 1)) {
                    deleteSelectedLayers(editor, canvas);
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Move up", "Ctrl+]", false, i + 1 < static_cast<int>(order.size()))) {
                    raiseActiveLayer(editor, canvas);
                }
                if (ImGui::MenuItem("Move down", "Ctrl+[", false, i > 0)) {
                    lowerActiveLayer(editor, canvas);
                }
                ImGui::Separator();
                if (ImGui::MenuItem(editor.selectedLayers.size() > 1 ? "Group selected" : "Group",
                                    "Ctrl+G")) {
                    groupSelectedLayers(editor, canvas);
                }
                {
                    const std::vector<ls::GroupId> groups = groupOrder(editor.doc, sprite);
                    bool anyOther = false;
                    for (ls::GroupId g : groups) { anyOther = anyOther || g != props.group; }
                    if (ImGui::BeginMenu("Add to group", anyOther)) {
                        for (ls::GroupId g : groups) {
                            if (g == props.group) { continue; }
                            GroupProps target;
                            if (!readGroupProps(editor.doc, g, &target)) { continue; }
                            ImGui::PushID(static_cast<int>(g.value));
                            if (ImGui::MenuItem(target.name.c_str())) {
                                if (addToGroup(editor.doc, id, g)) {
                                    selectLayer(editor, id);
                                    canvas.invalidate();
                                    editor.say("Added to " + target.name);
                                }
                            }
                            ImGui::PopID();
                        }
                        ImGui::EndMenu();
                    }
                }
                if (ImGui::MenuItem("Remove from group", nullptr, false, props.group.valid())) {
                    if (removeFromGroup(editor.doc, id)) {
                        selectLayer(editor, id);
                        canvas.invalidate();
                        editor.say("Out of the group, still where it was");
                    }
                }
                if (ImGui::MenuItem("Ungroup", "Ctrl+Shift+G", false, props.group.valid())) {
                    ungroupActiveLayer(editor, canvas);
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Clip to layer below", nullptr, props.clipBase.valid(), i > 0)) {
                    toggleActiveLayerClip(editor, canvas);
                }
                if (ImGui::MenuItem("Lock", nullptr, props.locked)) {
                    toggleActiveLayerLock(editor);
                }
                if (ImGui::MenuItem("Rename")) {
                    editor.renaming = listIndex;
                    std::snprintf(editor.renameBuffer, sizeof(editor.renameBuffer),
                                  "%s", props.name.c_str());
                }
                ImGui::EndPopup();
            }
        }
        if (openGroup.valid()) {
            ImGui::Unindent(18.f);
        }
        ImGui::PopID();
    }

    // Space under the stack is the bottom: dropping there sends a layer down.
    ImGui::Dummy(ImVec2(-1.f, 12.f));
    acceptDrop(0);
    if (order.size() > 1) {
        ImGui::PushStyleColor(ImGuiCol_Text, theme::palette().textDim);
        ImGui::TextWrapped("Drag to reorder, or onto a group to join it; "
                           "Ctrl+drop groups two. Right-click for the rest.");
        ImGui::PopStyleColor();
    }
}


// ------------------------------------------------------------- references --

void drawReferences(Editor& editor, CanvasView& canvas, ImDrawList* draw,
                    ImVec2 origin, float zoom, bool behind) {
    for (const Reference& reference : editor.references) {
        if (!reference.visible || reference.behind != behind) {
            continue;
        }
        const ReferenceCache::Entry* entry =
            canvas.referenceTextures().entryFor(editor.doc, reference);
        if (entry == nullptr) {
            continue;
        }
        // Placement is in canvas pixels, so it lines up with the drawing at
        // every zoom and stays lined up when the view moves.
        const ImVec2 at = canvas.pixelToScreen(origin, reference.x, reference.y);
        const ImVec2 corner = canvas.pixelToScreen(
            origin,
            reference.x + static_cast<float>(reference.width) * reference.scale,
            reference.y + static_cast<float>(reference.height) * reference.scale);
        const ImU32 tint = IM_COL32(255, 255, 255,
                                    static_cast<int>(reference.opacity * 255.f + 0.5f));
        draw->AddImage(reinterpret_cast<ImTextureID>(entry->texture), at, corner,
                       ImVec2(0.f, 0.f), ImVec2(1.f, 1.f), tint);

        // The selected one gets a border, so "which am I moving" is answered
        // without hiding the others.
        if (reference.id == editor.activeReference && !reference.locked) {
            draw->AddRect(at, corner, IM_COL32(255, 255, 255, 70), 0.f, 0, 1.f);
        }
    }
    (void)zoom;
}

void drawReferencePanel(Editor& editor, CanvasView& canvas, SDL_Window* window) {
    if (ImGui::Button("Import image...", ImVec2(-1.f, 0.f))) {
        showImportReferenceDialog(editor.files, window, editor.doc);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("A photograph, a sketch, a pose to draw from. It is "
                          "stored in this document, so it travels with the "
                          "work and is never exported.");
    }

    if (editor.references.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, theme::palette().textDim);
        ImGui::TextWrapped("Nothing to draw from yet. An image imported here "
                           "sits over or under the canvas at whatever opacity "
                           "you want, and changes no pixel of the artwork.");
        ImGui::PopStyleColor();
        return;
    }

    // The list. One row each: visible, name, remove.
    for (size_t i = 0; i < editor.references.size(); ++i) {
        Reference reference = editor.references[i];
        ImGui::PushID(reference.id.c_str());
        if (theme::eyeToggle("##visible", reference.visible, 16.f)) {
            reference.visible = !reference.visible;
            editor.doc.beginAction(reference.visible ? "Show reference"
                                                     : "Hide reference");
            updateReference(editor.doc, reference);
            editor.doc.endAction();
            resyncReferences(editor, canvas);
            ImGui::PopID();
            break;                    // the list was just rebuilt
        }
        ImGui::SameLine();
        const bool selected = reference.id == editor.activeReference;
        if (ImGui::Selectable(reference.name.c_str(), selected, 0,
                              ImVec2(ImGui::GetContentRegionAvail().x - 24.f, 0.f))) {
            editor.activeReference = reference.id;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%u x %u", reference.width, reference.height);
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("x")) {
            removeReference(editor.doc, reference);
            resyncReferences(editor, canvas);
            editor.say("Reference removed");
            ImGui::PopID();
            break;
        }
        ImGui::PopID();
    }

    Reference* active = activeReference(editor);
    if (active == nullptr) {
        return;
    }

    // The selected one's placement. Every control writes straight through,
    // and the drag brackets are what make a drag one history entry.
    ImGui::Dummy(ImVec2(0.f, 4.f));
    Reference edited = *active;
    bool changed = false;

    ImGui::SetNextItemWidth(-1.f);
    if (ImGui::SliderFloat("##opacity", &edited.opacity, 0.f, 1.f, "opacity %.2f")) {
        changed = true;
    }
    bracketDrag(editor, editor.draggingReference, "Reference opacity");

    float position[2] = { edited.x, edited.y };
    ImGui::SetNextItemWidth(-42.f);
    if (ImGui::DragFloat2("at", position, 0.25f, 0.f, 0.f, "%.1f")) {
        edited.x = position[0];
        edited.y = position[1];
        changed = true;
    }
    bracketDrag(editor, editor.draggingReference, "Move reference");

    ImGui::SetNextItemWidth(-42.f);
    if (ImGui::DragFloat("scale", &edited.scale, 0.01f, 0.01f, 64.f, "%.3f")) {
        changed = true;
    }
    bracketDrag(editor, editor.draggingReference, "Scale reference");

    if (ImGui::Button("Fit")) {
        auto size = editor.doc.engine().getCanvasSize(editor.doc.id());
        if (size.ok() && size.value.x > 0 && size.value.y > 0) {
            fitReference(edited, static_cast<uint32_t>(size.value.x),
                         static_cast<uint32_t>(size.value.y));
            changed = true;
        }
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Centre it on the canvas at the largest size that fits.");
    }
    ImGui::SameLine();
    if (ImGui::Checkbox("behind", &edited.behind)) {
        changed = true;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Under the artwork to trace from, or over it to "
                          "compare against.");
    }
    ImGui::SameLine();
    if (ImGui::Checkbox("lock", &edited.locked)) {
        changed = true;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("A locked reference will not move when dragged.");
    }

    if (changed) {
        // Always bracketed. While a slider is being dragged bracketDrag holds
        // an action open and this nests inside it, so the drag is one entry;
        // a checkbox or Fit, which no drag covers, gets an entry of its own
        // rather than folding into whatever happened to be open.
        editor.doc.beginAction("Place reference");
        updateReference(editor.doc, edited);
        editor.doc.endAction();
        *active = edited;
    }

    ImGui::PushStyleColor(ImGuiCol_Text, theme::palette().textDim);
    ImGui::TextWrapped("Alt+drag on the canvas moves the selected reference.");
    ImGui::PopStyleColor();
}

// ---------------------------------------------------------------- library --

namespace {

// Decodes per frame, so opening a folder of fifty sprites fills in over the
// next few frames rather than stalling one.
constexpr int kThumbnailsPerFrame = 4;

void drawFolderRow(Editor& editor, SDL_Window* window, bool references) {
    const std::string& folder = references ? editor.libraryFolders.references
                                           : editor.libraryFolders.project;
    ImGui::PushStyleColor(ImGuiCol_Text, theme::palette().textDim);
    ImGui::TextWrapped("%s", folder.empty() ? "No folder chosen." : folder.c_str());
    ImGui::PopStyleColor();
    if (ImGui::Button(folder.empty() ? "Choose a folder..." : "Change folder...")) {
        const std::string startingAt = folder.empty() ? documentsDirectory() : folder;
        if (references) {
            showReferenceFolderDialog(editor.files, window, startingAt);
        } else {
            showProjectFolderDialog(editor.files, window, startingAt);
        }
    }
    if (!folder.empty()) {
        ImGui::SameLine();
        if (ImGui::Button("Reload")) {
            editor.libraryStale = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Up")) {
            const std::string parent = parentDirectory(folder);
            if (!parent.empty() && directoryExists(parent)) {
                (references ? editor.libraryFolders.references
                            : editor.libraryFolders.project) = parent;
                editor.libraryFolders.save();
                editor.libraryStale = true;
            }
        }
    }
}

// One tile: a picture, a name, and what a click does.
bool drawTile(CanvasView& canvas, const ReferenceCache::Entry* picture,
              const char* name, bool selected, float size) {
    ImGui::PushID(name);
    const ImVec2 at = ImGui::GetCursorScreenPos();
    const bool clicked = ImGui::InvisibleButton("tile", ImVec2(size, size + 18.f));
    const bool hovered = ImGui::IsItemHovered();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const theme::Palette& c = theme::palette();

    draw->AddRectFilled(at, ImVec2(at.x + size, at.y + size),
                        ImGui::GetColorU32(c.canvasBackground),
                        theme::metrics().rounding);
    if (picture != nullptr && picture->width > 0 && picture->height > 0) {
        const float fit = std::min(size / static_cast<float>(picture->width),
                                   size / static_cast<float>(picture->height));
        const float w = static_cast<float>(picture->width) * fit;
        const float h = static_cast<float>(picture->height) * fit;
        const ImVec2 origin(at.x + (size - w) * 0.5f, at.y + (size - h) * 0.5f);
        draw->AddImage(reinterpret_cast<ImTextureID>(picture->texture), origin,
                       ImVec2(origin.x + w, origin.y + h));
    } else {
        // No thumbnail: a file saved before they existed, or one still being
        // read. An empty square says "nothing yet" rather than "broken".
        draw->AddRect(at, ImVec2(at.x + size, at.y + size),
                      ImGui::GetColorU32(c.border), theme::metrics().rounding);
    }
    if (selected || hovered) {
        draw->AddRect(at, ImVec2(at.x + size, at.y + size),
                      selected ? ImGui::ColorConvertFloat4ToU32(c.accent)
                               : ImGui::GetColorU32(c.text),
                      theme::metrics().rounding, 0, selected ? 2.f : 1.f);
    }
    draw->AddText(ImVec2(at.x, at.y + size + 3.f),
                  ImGui::GetColorU32(hovered ? c.text : c.textDim), name);
    ImGui::PopID();
    (void)canvas;
    return clicked;
}

} // namespace

void drawLibraryPanel(Editor& editor, CanvasView& canvas, SDL_Window* window) {
    if (!editor.libraryOpen) {
        return;
    }
    ImGui::SetNextWindowSize(ImVec2(520.f, 420.f), ImGuiCond_Appearing);
    if (!ImGui::Begin("Library", &editor.libraryOpen)) {
        ImGui::End();
        return;
    }

    if (ImGui::BeginTabBar("libraryTabs")) {
        const bool sprites = ImGui::BeginTabItem("Sprites");
        if (sprites) {
            if (editor.libraryShowingReferences) {
                editor.libraryShowingReferences = false;
                editor.libraryStale = true;
            }
            ImGui::EndTabItem();
        }
        const bool images = ImGui::BeginTabItem("References");
        if (images) {
            if (!editor.libraryShowingReferences) {
                editor.libraryShowingReferences = true;
                editor.libraryStale = true;
            }
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    if (editor.libraryStale) {
        refreshLibrary(editor);
    }
    drawFolderRow(editor, window, editor.libraryShowingReferences);
    ImGui::Separator();

    const float tile = 84.f;
    const float step = tile + theme::metrics().itemSpacing;
    const int perRow = std::max(1, static_cast<int>(
        (ImGui::GetContentRegionAvail().x + theme::metrics().itemSpacing) / step));
    int budget = kThumbnailsPerFrame;
    int column = 0;

    ImGui::BeginChild("items", ImVec2(0.f, 0.f), false);
    if (editor.libraryShowingReferences) {
        std::vector<std::string> live;
        for (const LibraryImage& image : editor.libraryImages) {
            live.push_back(image.path);
        }
        canvas.libraryThumbnails().retainOnlyKeys(live);

        for (const LibraryImage& image : editor.libraryImages) {
            const ReferenceCache::Entry* picture = nullptr;
            if (canvas.libraryThumbnails().holds(image.path) || budget > 0) {
                std::vector<uint8_t> bytes;
                std::string error;
                if (canvas.libraryThumbnails().holds(image.path)) {
                    picture = canvas.libraryThumbnails().entryForBytes(image.path, bytes);
                } else if (readFile(image.path, bytes, &error)) {
                    picture = canvas.libraryThumbnails().entryForBytes(image.path, bytes,
                                                                       &budget);
                }
            }
            if (column > 0 && column < perRow) {
                ImGui::SameLine(0.f, theme::metrics().itemSpacing);
            }
            if (drawTile(canvas, picture, image.name.c_str(), false, tile)) {
                std::vector<uint8_t> bytes;
                std::string error;
                Reference made;
                if (readFile(image.path, bytes, &error) &&
                    addReference(editor.doc, image.name, bytes, &made, &error)) {
                    resyncReferences(editor, canvas);
                    editor.activeReference = made.id;
                    editor.say("Imported " + made.name + " as a reference");
                } else {
                    editor.say("Could not import " + image.name + ": " + error);
                }
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s\nClick to bring it into this document as a "
                                  "reference.", image.path.c_str());
            }
            column = (column + 1) % perRow;
        }
        if (editor.libraryImages.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, theme::palette().textDim);
            ImGui::TextWrapped("No images in this folder. Point it at wherever "
                               "you keep the pictures you draw from -- the same "
                               "pose sheet gets used across a dozen sprites.");
            ImGui::PopStyleColor();
        }
    } else {
        std::vector<std::string> live;
        for (const LibraryDocument& document : editor.libraryDocuments) {
            live.push_back(document.path);
        }
        canvas.libraryThumbnails().retainOnlyKeys(live);

        for (const LibraryDocument& document : editor.libraryDocuments) {
            const ReferenceCache::Entry* picture = nullptr;
            if (canvas.libraryThumbnails().holds(document.path)) {
                std::vector<uint8_t> none;
                picture = canvas.libraryThumbnails().entryForBytes(document.path, none);
            } else if (budget > 0) {
                std::vector<uint8_t> png;
                if (readThumbnail(document.path, &png)) {
                    picture = canvas.libraryThumbnails().entryForBytes(document.path, png,
                                                                       &budget);
                } else {
                    --budget;         // the read cost something even with no picture
                }
            }
            const bool isOpen = document.path == editor.doc.path();
            if (column > 0 && column < perRow) {
                ImGui::SameLine(0.f, theme::metrics().itemSpacing);
            }
            if (drawTile(canvas, picture, document.name.c_str(), isOpen, tile) && !isOpen) {
                // Through the same door a recent entry uses, so an unsaved
                // document still gets asked about first.
                requestAction(editor, canvas, window, PendingAction::OpenPath,
                              document.path);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s\n%s", document.path.c_str(),
                                  isOpen ? "Open in this window."
                                         : "Click to open.");
            }
            column = (column + 1) % perRow;
        }
        if (editor.libraryDocuments.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, theme::palette().textDim);
            ImGui::TextWrapped("No sprites in this folder. A project here is "
                               "just a folder: point it at the one your work "
                               "lives in and everything beside this file is one "
                               "click away.");
            ImGui::PopStyleColor();
        }
    }
    ImGui::EndChild();
    ImGui::End();
}

// -------------------------------------------------------------- transform --

void drawTransformPanel(Editor& editor, CanvasView& canvas) {
    PaintLayer* layer = editor.active();
    if (layer == nullptr) {
        ImGui::TextDisabled("No layer selected.");
        return;
    }

    const ls::Vec2f centre = [&] {
        auto size = editor.doc.engine().getCanvasSize(editor.doc.id());
        if (size.fail()) { return ls::Vec2f{0.f, 0.f}; }
        return ls::Vec2f{ static_cast<float>(size.value.x) * 0.5f,
                          static_cast<float>(size.value.y) * 0.5f };
    }();

    const float third = (ImGui::GetContentRegionAvail().x -
                         theme::metrics().itemSpacing * 2.f) / 3.f;

    if (ImGui::Button("Rotate", ImVec2(third, 0.f))) {
        editor.doc.beginAction("Add rotate");
        addRotate(editor.doc, layer->layer, 0.f, centre);
        editor.doc.endAction();
        canvas.invalidate();
    }
    ImGui::SameLine();
    if (ImGui::Button("Scale", ImVec2(third, 0.f))) {
        editor.doc.beginAction("Add scale");
        addScale(editor.doc, layer->layer, {1.f, 1.f}, centre);
        editor.doc.endAction();
        canvas.invalidate();
    }
    ImGui::SameLine();
    if (ImGui::Button("Mirror", ImVec2(third, 0.f))) {
        editor.doc.beginAction("Add mirror");
        addMirror(editor.doc, layer->layer, ls::MirrorAxis::X, centre);
        editor.doc.endAction();
        canvas.invalidate();
    }

    const std::vector<TransformEntry> transforms =
        listTransforms(editor.doc, layer->layer);

    if (transforms.empty()) {
        ImGui::Dummy(ImVec2(0.f, 6.f));
        ImGui::PushStyleColor(ImGuiCol_Text, theme::palette().textDim);
        ImGui::TextWrapped("Nothing applied. Add a rotation and drag it: the "
                           "picture is rebuilt from the drawing each time, so "
                           "returning to zero returns the original pixels "
                           "exactly.");
        ImGui::PopStyleColor();
        return;
    }

    ImGui::Dummy(ImVec2(0.f, 4.f));

    ls::OperationId toRemove;
    for (size_t i = 0; i < transforms.size(); ++i) {
        const TransformEntry& entry = transforms[i];
        ImGui::PushID(static_cast<int>(i));

        ImGui::PushStyleColor(ImGuiCol_Text, theme::palette().textDim);
        ImGui::TextUnformatted(entry.label().c_str());
        ImGui::PopStyleColor();

        bool changed = false;
        ImGui::SetNextItemWidth(-30.f);
        if (entry.kind == TransformKind::Rotate) {
            float angle = entry.angleDegrees;
            if (ImGui::SliderFloat("##angle", &angle, -360.f, 360.f, "%.1f deg")) {
                setRotateAngle(editor.doc, entry.id, angle);
                changed = true;
            }
            bracketDrag(editor, editor.draggingTransform, "Rotate");
        } else if (entry.kind == TransformKind::Scale) {
            float factor[2] = { entry.factor.x, entry.factor.y };
            if (ImGui::SliderFloat2("##factor", factor, 0.1f, 8.f, "%.2f")) {
                setScaleFactor(editor.doc, entry.id, {factor[0], factor[1]});
                changed = true;
            }
            bracketDrag(editor, editor.draggingTransform, "Scale");
        } else {
            ImGui::TextDisabled("no parameters");
        }

        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, theme::palette().danger);
        if (ImGui::SmallButton("x")) {
            toRemove = entry.id;
        }
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Remove. The original returns exactly.");
        }

        if (changed) {
            canvas.invalidate();
            editor.say("Recompiled from the drawing, not from the last frame");
        }
        ImGui::PopID();
    }

    // Removed after the loop rather than during it, so the list being walked is
    // not mutated underneath.
    if (toRemove.valid()) {
        editor.doc.beginAction("Remove transform");
        removeTransform(editor.doc, layer->layer, toRemove);
        editor.doc.endAction();
        canvas.invalidate();
    }

    ImGui::Dummy(ImVec2(0.f, 4.f));
    if (ImGui::Button("Remove all", ImVec2(-1.f, 0.f))) {
        editor.doc.beginAction("Remove transforms");
        clearTransforms(editor.doc, layer->layer);
        editor.doc.endAction();
        canvas.invalidate();
    }
}

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

void drawPreviewOverlay(Editor& editor, const CanvasView& canvas) {
    if (!editor.preview.visible || canvas.compiledWidth() == 0) {
        return;
    }

    const theme::Palette& c = theme::palette();
    const float scale = static_cast<float>(editor.preview.scale);
    const float artWidth = static_cast<float>(canvas.compiledWidth()) * scale;
    const float artHeight = static_cast<float>(canvas.compiledHeight()) * scale;

    const float padding = 10.f;
    const float swatchRow = 16.f;

    // Two rows of controls, not one: the scales and the colour, then the
    // backdrops. Sizing the box for one leaves the second hanging outside it.
    const float controlsHeight =
        ImGui::GetFrameHeight() + theme::metrics().itemSpacing + swatchRow + 12.f;

    // Wide enough for the controls even when the sprite is tiny, which it
    // usually is: a 16x16 at 1x is smaller than the buttons under it.
    // Wide enough for eight backdrop swatches in a row, whatever the sprite.
    const float backdropRow = 8.f * swatchRow + 7.f * 3.f;
    const float boxWidth = std::max(artWidth + padding * 2.f,
                                    backdropRow + padding * 2.f + 4.f);
    const float boxHeight = artHeight + controlsHeight + padding * 2.f;

    // Bottom right of the canvas, out of the way of the artwork, which is
    // usually centred.
    const ImVec2 windowMin = ImGui::GetWindowPos();
    const ImVec2 windowSize = ImGui::GetWindowSize();
    const ImVec2 at(windowMin.x + windowSize.x - boxWidth - 14.f,
                    windowMin.y + windowSize.y - boxHeight - 14.f);

    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(at, ImVec2(at.x + boxWidth, at.y + boxHeight),
                        ImGui::GetColorU32(c.windowBackground), 4.f);
    draw->AddRect(at, ImVec2(at.x + boxWidth, at.y + boxHeight),
                  ImGui::GetColorU32(c.border), 4.f);

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

    // The controls sit inside the box as a real ImGui region, so they can be
    // clicked rather than only looked at.
    ImGui::SetCursorScreenPos(ImVec2(at.x + padding, at.y + artHeight + padding + 4.f));
    ImGui::BeginGroup();
    ImGui::PushID("preview");

    for (int step = 1; step <= 4; ++step) {
        if (step > 1) {
            ImGui::SameLine(0.f, 3.f);
        }
        const bool selected = editor.preview.scale == step;
        ImGui::PushStyleColor(ImGuiCol_Button,
                              selected ? c.accentDim : ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_Text, selected ? c.accent : c.textDim);
        char label[8];
        std::snprintf(label, sizeof(label), "%dx", step);
        if (ImGui::Button(label, ImVec2(26.f, 0.f))) {
            editor.preview.scale = step;
        }
        ImGui::PopStyleColor(2);
    }

    ImGui::SameLine(0.f, 8.f);
    if (theme::swatch("transparent", IM_COL32(0, 0, 0, 0),
                      editor.preview.transparent, 18.f)) {
        editor.preview.transparent = true;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("No background");
    }

    ImGui::SameLine(0.f, 3.f);
    ImGui::SetNextItemWidth(40.f);
    if (ImGui::ColorEdit4("##bg", editor.preview.color,
                          ImGuiColorEditFlags_NoInputs |
                          ImGuiColorEditFlags_NoLabel)) {
        editor.preview.transparent = false;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Background colour");
    }

    // The situations a sprite has to survive, one click each.
    for (size_t i = 0; i < sizeof(kBackdrops) / sizeof(kBackdrops[0]); ++i) {
        if (i % 8 != 0) {
            ImGui::SameLine(0.f, 3.f);
        }
        const ImU32 colour = kBackdrops[i].colour;
        // Packed without ImGui::GetColorU32, which multiplies in the global
        // style alpha and so would never compare equal to a literal colour.
        const bool selected =
            !editor.preview.transparent &&
            colour == ImGui::ColorConvertFloat4ToU32(
                ImVec4(editor.preview.color[0], editor.preview.color[1],
                       editor.preview.color[2], editor.preview.color[3]));
        if (theme::swatch(kBackdrops[i].name, colour, selected, 16.f)) {
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

void drawStatusBar(Editor& editor, const CanvasView& canvas) {
    char buffer[128];

    // The message first, since it is what changed most recently.
    ImGui::PushStyleColor(ImGuiCol_Text,
                          editor.doc.modified() ? theme::palette().accent
                                                : theme::palette().textDim);
    ImGui::TextUnformatted(editor.doc.modified() ? "*" : " ");
    ImGui::PopStyleColor();
    ImGui::SameLine(0.f, 4.f);
    ImGui::PushStyleColor(ImGuiCol_Text, theme::palette().text);
    ImGui::TextUnformatted(editor.status.c_str());
    ImGui::PopStyleColor();

    // The rest right-aligned, in a fixed order, so the eye learns where to look.
    auto size = editor.doc.engine().getCanvasSize(editor.doc.id());
    const int width = size.ok() ? size.value.x : 0;
    const int height = size.ok() ? size.value.y : 0;

    std::snprintf(buffer, sizeof(buffer), "%d x %d", width, height);
    const std::string canvasSize = buffer;

    std::string cursor = "-";
    if (editor.hovered.x >= 0) {
        std::snprintf(buffer, sizeof(buffer), "%d, %d",
                      editor.hovered.x, editor.hovered.y);
        cursor = buffer;
    }

    std::snprintf(buffer, sizeof(buffer), "%.0fx", static_cast<double>(canvas.zoom()));
    const std::string zoom = buffer;

    std::snprintf(buffer, sizeof(buffer), "%.2f ms", canvas.lastCompileMs());
    const std::string compile = buffer;

    // How many compiles this UI frame actually cost. It is the number that says
    // whether the frame cache is doing its job: 0 at rest, 1 while drawing, and
    // never once per frame on screen.
    std::snprintf(buffer, sizeof(buffer), "%d", canvas.compilesThisFrame());
    const std::string compiles = buffer;

    std::string frameLabel = "-";
    if (!editor.frames.empty()) {
        std::snprintf(buffer, sizeof(buffer), "%d / %d",
                      editor.timeline.activeFrame + 1,
                      static_cast<int>(editor.frames.size()));
        frameLabel = buffer;
    }

    const float rightWidth = 580.f;
    ImGui::SameLine(ImGui::GetWindowWidth() - rightWidth);

    theme::statusItem("Canvas", canvasSize.c_str());
    ImGui::SameLine(0.f, 18.f);
    theme::statusItem("Cursor", cursor.c_str());
    ImGui::SameLine(0.f, 18.f);
    theme::statusItem("Zoom", zoom.c_str());
    ImGui::SameLine(0.f, 18.f);
    theme::statusItem("Frame", frameLabel.c_str());
    ImGui::SameLine(0.f, 18.f);
    theme::statusItem("Compile", compile.c_str(), false);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("How long the last real compile took. Cached frames "
                          "cost nothing, so this only moves when something "
                          "changed.");
    }
    ImGui::SameLine(0.f, 10.f);
    // Bright only when it is not zero, so at rest it does not draw the eye.
    theme::statusItem("x", compiles.c_str(), canvas.compilesThisFrame() > 0);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Compiles this frame. Zero at rest and one while "
                          "drawing: every other frame on screen is a texture "
                          "that already exists, not a second compile.");
    }
}

} // namespace fast
