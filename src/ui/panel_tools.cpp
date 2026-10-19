// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

// panel_tools.cpp -- the toolbar and the tool panel.
// Declared in panels.h; what the panel files share is in panels_internal.h.

#include "ui/panels_internal.h"
#include "app/i18n.h"
#include "ui/theme.h"
#include "ui/widgets.h"
#include "ui/ui_script.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace fast {

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
        { Tool::Text, theme::Icon::Text, "Text", "Shift+T",
          "Click where the text goes and type. It stays text: retype, resize or "
          "move it in the Element panel, and it is rebuilt from the words." },
        { Tool::Contour, theme::Icon::Contour, "Contour", "D",
          "Draw round an area; on release it is filled with the current colour." },
        { Tool::Polygon, theme::Icon::Polygon, "Polygon", "Shift+D",
          "Click corner after corner; Enter, a double-click or the first corner "
          "finishes it. It stays a polygon: drag its corners afterwards." },
        { Tool::Slice, theme::Icon::Slice, "Slice", "C",
          "Drag out a named rectangle for a game to read -- a button, a hitbox, a "
          "panel to stretch by its middle. Drag inside to move it, a corner to "
          "resize it; name it and give it a nine-slice centre in View > Slices." },
        { Tool::Curve, theme::Icon::Curve, "Curve", "Shift+L",
          "Click to place points, drag to pull out a point's handles; Enter or "
          "a double-click ends it, the first point closes it. It stays a curve: "
          "drag its points and handles afterwards." },
        { Tool::Select, theme::Icon::Marquee, "Select", "M",
          "Drag a rectangle to select. Shift adds, Alt subtracts, both "
          "intersect. Drag inside the selection to move what it holds." },
        { Tool::SelectEllipse, theme::Icon::EllipseMarquee, "Select ellipse", "Shift+M",
          "Drag an ellipse to select, with the same modifiers." },
        { Tool::Lasso, theme::Icon::Lasso, "Lasso", "Q",
          "Draw round what to select. The outline counts as well as the inside." },
        { Tool::PolygonLasso, theme::Icon::PolygonLasso, "Polygon lasso", "Shift+Q",
          "Click corner after corner; click the first corner, double-click or press "
          "Enter to close it, Escape to let it go. The same modifiers as the marquee." },
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

    // Four groups, a hairline between them: to draw with, to make shapes with,
    // to select with, and to look with. Twenty-two tools in one column read as
    // a wall; in four they can be found by place.
    const auto groupOf = [](Tool tool) {
        switch (tool) {
            case Tool::Pencil: case Tool::Spray: case Tool::Eraser: case Tool::Bucket:
            case Tool::Picker:
                return 0;
            case Tool::Rectangle: case Tool::Ellipse: case Tool::Line: case Tool::Gradient:
            case Tool::Text: case Tool::Contour: case Tool::Polygon: case Tool::Curve:
            case Tool::Slice:
                return 1;
            case Tool::Select: case Tool::SelectEllipse: case Tool::Lasso:
            case Tool::PolygonLasso: case Tool::Wand: case Tool::Move:
                return 2;
            default:
                return 3;
        }
    };
    int lastGroup = -1;
    for (const Entry& entry : kTools) {
        const int group = groupOf(entry.tool);
        if (lastGroup >= 0 && group != lastGroup) {
            theme::railDivider();
        }
        lastGroup = group;
        if (theme::toolButton(entry.icon, tr(entry.name), entry.shortcut,
                              editor.tool == entry.tool, entry.description)) {
            if (entry.tool == Tool::Picker && editor.tool != Tool::Picker) {
                editor.toolBeforePicker = editor.tool;
            }
            editor.tool = entry.tool;
        }
    }
}

// ------------------------------------------------------------- tool panel --

void drawDitherControls(Editor& editor, CanvasView& canvas, const PaintLayer& layer) {
    DitherSettings settings;
    if (!readDitherSettings(editor.doc, layer, &settings)) {
        ImGui::TextDisabled("%s", tr("This layer is not dithered."));
        return;
    }

    bool changed = false;

    theme::sectionHeader(tr("PATTERN"));
    int pattern = static_cast<int>(settings.pattern);
    ImGui::SetNextItemWidth(-theme::hintWidth());
    if (ImGui::Combo("##pattern", &pattern, ditherPatternNames().data(),
                     static_cast<int>(ditherPatternNames().size()))) {
        settings.pattern = static_cast<ls::DitherPatternKind>(pattern);
        changed = true;
        singleAction(editor, "Dither pattern");
    }
    ImGui::SameLine();
    theme::hint(tr("Threshold matrices, not stamps. The same tile works at any "
                "density and at every step of a gradient."));

    theme::sectionHeader(tr("VALUE"));
    int modulation = static_cast<int>(settings.modulation);
    ImGui::SetNextItemWidth(-theme::hintWidth());
    if (ImGui::Combo("##modulation", &modulation, ditherModulationNames().data(),
                     static_cast<int>(ditherModulationNames().size()))) {
        settings.modulation = static_cast<ls::DitherModulation>(modulation);
        changed = true;
        singleAction(editor, "Dither value");
    }
    ImGui::SameLine();
    theme::hint(tr("Constant is a flat screen. The others vary the value across "
                "the shape, which is what makes a gradient out of dithered "
                "colour."));

    if (settings.modulation == ls::DitherModulation::Constant) {
        ImGui::SetNextItemWidth(-1.f);
        if (widgets::sliderFloat("##density", &settings.density, 0.f, 1.f,
                               "density  %.2f")) {
            changed = true;
        }
        bracketDrag(editor, editor.draggingDither, "Dither density");
    } else {
        float start[2] = { settings.gradientStart.x, settings.gradientStart.y };
        float end[2]   = { settings.gradientEnd.x, settings.gradientEnd.y };
        ImGui::SetNextItemWidth(-widgets::labelRoom("from"));
        if (ImGui::DragFloat2("from", start, 0.25f, 0.f, 0.f, "%.0f")) {
            settings.gradientStart = { start[0], start[1] };
            changed = true;
        }
        bracketDrag(editor, editor.draggingDither, "Gradient");
        ImGui::SetNextItemWidth(-widgets::labelRoom("to"));
        if (ImGui::DragFloat2("to", end, 0.25f, 0.f, 0.f, "%.0f")) {
            settings.gradientEnd = { end[0], end[1] };
            changed = true;
        }
    }

    theme::sectionHeader(tr("ANCHOR"));
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

    theme::sectionHeader(tr("RAMP"));

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
            if (ImGui::SmallButton(tr("detach"))) {
                role = ls::kColorRoleNone;
                changed = true;
            }
            ImGui::SameLine();
        }
        if (ImGui::SmallButton(tr("= current"))) {
            const Ink ink = foregroundInk(editor);
            colour = ink.colour;
            role = ink.role;
            changed = true;
            singleAction(editor, "Ramp end from the current colour");
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", tr("Set this end to the current colour -- its palette "
                              "slot, when it has one."));
        }
        ImGui::PopID();
    };
    rampEnd("dark",  settings.from, settings.fromRole);
    ImGui::SameLine();
    theme::hint(tr("Each end is a colour or a palette slot. Pick a slot in the "
                "palette, then press = current on an end to point it there -- "
                "the dither then recolours with a palette change like "
                "everything else."));
    rampEnd("light", settings.to,   settings.toRole);

    if (changed) {
        applyDitherSettings(editor.doc, layer, settings);
        editor.dither = settings;
        canvas.invalidate();
        editor.say("Recompiled from the drawing");
    }
}

namespace {
void drawInkControls(Editor& editor);
} // namespace

void drawToolPanel(Editor& editor) {
    if (isSelectionTool(editor.tool) || editor.tool == Tool::Move) {
        theme::sectionHeader(tr("SELECTION"));
        ImGui::PushStyleColor(ImGuiCol_Text, theme::palette().textDim);
        ImGui::TextWrapped("%s", tr("Shift adds, Alt subtracts, Shift+Alt intersects. Drag "
                           "inside the selection to move what it holds; arrows "
                           "nudge a pixel, Shift+arrows eight. Enter drops a move, "
                           "Escape takes it back. Ctrl+C, Ctrl+X and Ctrl+V work on "
                           "the selected pixels; Delete clears them."));
        ImGui::PopStyleColor();
        if (editor.tool == Tool::Wand) {
            ImGui::Checkbox(tr("Follow diagonals##wand"), &editor.wand.diagonal);
            ImGui::Checkbox(tr("Whole canvas##wand"), &editor.wand.global);
            ImGui::SameLine();
            theme::hint(tr("Every pixel of the colour clicked, wherever it is, rather "
                        "than only the area touching the click."));
            ImGui::SetNextItemWidth(-1.f);
            widgets::sliderInt("##wandtolerance", &editor.wand.tolerance, 0, 64,
                             "tolerance  %d");
        }
        if (!editor.selection.empty()) {
            const ls::Rect2i box = editor.selection.bounds();
            ImGui::Text("%d x %d at %d, %d", box.width(), box.height(), box.min.x, box.min.y);
        }
        ImGui::Dummy(ImVec2(0.f, theme::metrics().sectionGap));
    }

    if (editor.tool == Tool::Bucket) {
        theme::sectionHeader(tr("FILL"));
        ImGui::Checkbox(tr("Follow diagonals"), &editor.bucket.diagonal);
        ImGui::SameLine();
        theme::hint(tr("Off by default. A one-pixel diagonal is a wall in pixel "
                    "art, and leaking through it is the classic paint-bucket "
                    "annoyance."));
        ImGui::Checkbox(tr("Whole canvas"), &editor.bucket.global);
        ImGui::SetNextItemWidth(-1.f);
        widgets::sliderInt("##tolerance", &editor.bucket.tolerance, 0, 64,
                         "tolerance  %d");
        ImGui::Dummy(ImVec2(0.f, theme::metrics().sectionGap));
    }

    if (editor.tool == Tool::Pencil || editor.tool == Tool::Spray) {
        theme::sectionHeader(tr("INK"));
        const char* modes[] = { "Simple", "Lock alpha", "Replace colour", "Shading" };
        int mode = static_cast<int>(editor.inkMode);
        ImGui::SetNextItemWidth(-1.f);
        if (ImGui::Combo("##inkmode", &mode, modes, 4)) {
            editor.inkMode = static_cast<InkMode>(mode);
        }
        nameForScripts("Ink mode");
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
        theme::sectionHeader(tr("GRADIENT"));
        int pattern = static_cast<int>(editor.dither.pattern);
        ImGui::SetNextItemWidth(-1.f);
        if (ImGui::Combo("##gradpattern", &pattern, ditherPatternNames().data(),
                         static_cast<int>(ditherPatternNames().size()))) {
            editor.dither.pattern = static_cast<ls::DitherPatternKind>(pattern);
        }
        ImGui::PushStyleColor(ImGuiCol_Text, theme::palette().textDim);
        ImGui::TextWrapped("%s", tr("From the left colour to the right one, along the drag. "
                           "Inside the selection when there is one, otherwise the "
                           "area of the colour under the press, as a fill would "
                           "find it."));
        ImGui::PopStyleColor();
        ImGui::Dummy(ImVec2(0.f, theme::metrics().sectionGap));
    }

    if (editor.tool == Tool::Spray) {
        theme::sectionHeader(tr("SPRAY"));
        ImGui::SetNextItemWidth(-1.f);
        widgets::sliderInt("##sprayradius", &editor.sprayRadius, 1, 32, "reach  %d");
        ImGui::SetNextItemWidth(-1.f);
        widgets::sliderInt("##spraydensity", &editor.sprayDensity, 1, 60, "density  %d");
        ImGui::Dummy(ImVec2(0.f, theme::metrics().sectionGap));
    }

    if (editor.tool == Tool::Pencil || editor.tool == Tool::Eraser) {
        theme::sectionHeader(tr("BRUSH"));
        ImGui::SetNextItemWidth(-theme::hintWidth());
        widgets::sliderInt("##size", &editor.brush.size, 1, kMaxBrushSize, "size  %d");
        ImGui::SameLine();
        theme::hint(tr("Shift+] and Shift+[ change it from the keyboard. Even "
                    "sizes hang right and down from the pointer's pixel."));
        ImGui::BeginDisabled(editor.brush.size < 3);
        ImGui::Checkbox(tr("Round"), &editor.brush.round);
        ImGui::EndDisabled();
        if (editor.tool == Tool::Pencil) {
            ImGui::SameLine();
            ImGui::BeginDisabled(editor.brush.size != 1);
            ImGui::Checkbox(tr("Pixel-perfect"), &editor.brush.pixelPerfect);
            ImGui::EndDisabled();
            ImGui::SameLine();
            theme::hint(tr("At one pixel: the corner of every L in the stroke is "
                        "dropped, so a diagonal reads as a line rather than a "
                        "staircase with doubled steps. The last pixel lands when "
                        "the stroke ends."));
        }
        if (!editor.customBrush.empty() && editor.tool == Tool::Pencil) {
            ImGui::Checkbox(tr("Stamp the custom brush"), &editor.customBrushOn);
            if (editor.customBrushOn) {
                ImGui::SameLine();
                if (ImGui::SmallButton(tr("forget it"))) {
                    editor.customBrush = PixelClip{};
                    editor.customBrushOn = false;
                }
                if (ImGui::RadioButton(tr("its own colours"), editor.customBrushOwnColours)) {
                    editor.customBrushOwnColours = true;
                }
                ImGui::SameLine();
                if (ImGui::RadioButton(tr("the current colour"), !editor.customBrushOwnColours)) {
                    editor.customBrushOwnColours = false;
                }
            }
        } else if (editor.tool == Tool::Pencil) {
            ImGui::PushStyleColor(ImGuiCol_Text, theme::palette().textMuted);
            ImGui::TextWrapped("%s", tr("Select pixels and press Ctrl+B for a brush of them."));
            ImGui::PopStyleColor();
        }
        ImGui::SetNextItemWidth(-1.f);
        widgets::sliderInt("##stabiliser", &editor.brush.stabiliser, 0, 32,
                         editor.brush.stabiliser == 0 ? "stabiliser off" : "stabiliser %d");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", tr("The line follows a point pulled along behind the pointer "
                              "on a string this long,\nso a shaky hand draws a smooth "
                              "curve. 0 turns it off."));
        }
        if (editor.pen.seen) {
            ImGui::Checkbox(tr("Pen pressure sets size"), &editor.brush.pressureSize);
            ImGui::SameLine();
            theme::hint(tr("Light touch, one pixel; full pressure, the size above. "
                        "The pen's eraser end erases while it touches."));
        } else {
            ImGui::PushStyleColor(ImGuiCol_Text, theme::palette().textDim);
            ImGui::TextWrapped("%s", tr("A pen tablet works as is; bring one near and "
                               "pressure appears here."));
            ImGui::PopStyleColor();
        }
        ImGui::Dummy(ImVec2(0.f, theme::metrics().sectionGap));
    }

    if (editor.tool == Tool::Rectangle || editor.tool == Tool::Ellipse ||
        editor.tool == Tool::Line) {
        theme::sectionHeader(tr("SHAPES"));
        if (editor.tool != Tool::Line) {
            ImGui::Checkbox(tr("Outline only"), &editor.shapeOutline);
            if (editor.shapeOutline) {
                ImGui::SameLine();
                ImGui::SetNextItemWidth(-1.f);
                widgets::sliderFloat("##outlinewidth", &editor.shapeOutlineWidth, 1.f, 8.f,
                                   "width %.0f");
            }
        }
        ImGui::Checkbox(tr("Each shape on its own layer"), &editor.shapesOnOwnLayer);
        ImGui::SameLine();
        theme::hint(tr("Off: a shape joins the active layer as one of its "
                    "elements, beside the pixels and the other shapes, and "
                    "stays editable in the Shape panel. On: every shape is a "
                    "layer of its own, listed in the stack."));
        ImGui::Dummy(ImVec2(0.f, theme::metrics().sectionGap));
    }

    if (editor.tool == Tool::Pencil || editor.tool == Tool::Eraser ||
        editor.tool == Tool::Bucket || editor.tool == Tool::Spray) {
        theme::sectionHeader(tr("SYMMETRY"));
        ImGui::Checkbox(tr("Across"), &editor.symmetryAcross);
        ImGui::SameLine();
        ImGui::Checkbox(tr("Down"), &editor.symmetryDown);
        ImGui::SameLine();
        theme::hint(tr("Drawing on one side of an axis draws on the other too. The "
                    "axes start at the middle of the canvas; move them here. "
                    "Shift+click with the pencil draws a straight line from "
                    "where the last stroke ended."));
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
    theme::sectionHeader(tr("COLOUR"));

    const auto toU32 = [](const float rgba[4]) {
        return ImGui::GetColorU32(ImVec4(rgba[0], rgba[1], rgba[2], rgba[3]));
    };
    theme::swatch("##fore", toU32(editor.color), true, theme::px(30.f));
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", tr("What the left button paints with"));
    }
    ImGui::SameLine();
    if (theme::swatch("##back", toU32(editor.backColor), false, theme::px(22.f))) {
        swapInks(editor);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", tr("What the right button paints with. Click, or press X, "
                          "to swap the two."));
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
    theme::hint(tr("A colour taken from the palette paints through its slot, so "
                "what you draw with it recolours when the slot changes. A colour "
                "picked here is a value of its own. Right-click paints with the "
                "second colour."));

    // Smaller than the column: the palette under it is used more often than
    // the picker, and a picker the width of the panel pushes it off screen.
    ImGui::SetNextItemWidth(std::min(ImGui::GetContentRegionAvail().x, theme::px(150.f)));
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


} // namespace fast
