// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

#include "ui/panels.h"
#include "ui/theme.h"

#include "app/shape.h"
#include "app/transform.h"

#include <cstdio>
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
    float from[4], to[4];
    fromColor(settings.from, from);
    fromColor(settings.to, to);
    if (ImGui::ColorEdit4("dark", from, ImGuiColorEditFlags_NoInputs)) {
        settings.from = toColor(from);
        changed = true;
    }
    bracketDrag(editor, editor.draggingDither, "Ramp colour");
    if (ImGui::ColorEdit4("light", to, ImGuiColorEditFlags_NoInputs)) {
        settings.to = toColor(to);
        changed = true;
    }

    if (changed) {
        applyDitherSettings(editor.doc, layer, settings);
        editor.dither = settings;
        canvas.invalidate();
        editor.say("Recompiled from the drawing");
    }
}

} // namespace

void drawToolPanel(Editor& editor, CanvasView& canvas) {
    PaintLayer* layer = editor.active();

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

    if (layer == nullptr) {
        ImGui::TextDisabled("No layer selected.");
        return;
    }

    // Solid or dithered is a property of the layer, since it is the rule that
    // colours the drawing. Switching does not touch the drawing.
    bool dithered = layerIsDithered(editor.doc, *layer);
    if (ImGui::Checkbox("Dithered fill", &dithered)) {
        editor.doc.beginAction(dithered ? "Dither the layer" : "Solid fill");
        if (dithered) {
            setLayerDithered(editor.doc, *layer, editor.dither);
        } else {
            setLayerSolid(editor.doc, *layer, toColor(editor.color));
        }
        editor.doc.endAction();
        canvas.invalidate();
        editor.say(dithered ? "The drawing is unchanged; only the rule that "
                              "colours it is different"
                            : "Back to a solid fill, drawing intact");
    }
    ImGui::SameLine();
    theme::hint("A dither compares a value against a threshold matrix and picks "
                "between two ramp stops. Switching back and forth costs "
                "nothing: the drawing is never touched.");

    ImGui::Dummy(ImVec2(0.f, 4.f));

    if (dithered) {
        drawDitherControls(editor, canvas, *layer);
        return;
    }

    theme::sectionHeader("COLOUR");

    const ls::ColorRole role = layerRole(editor.doc, *layer);
    if (role != ls::kColorRoleNone) {
        // The layer paints through the palette, so its colour is not the
        // layer's to change here -- saying so beats a control that silently
        // does nothing.
        ImGui::PushStyleColor(ImGuiCol_Text, theme::palette().textDim);
        ImGui::TextWrapped("Painting through palette slot %u. Edit the swatch "
                           "to recolour every layer using it.", role);
        ImGui::PopStyleColor();
        if (ImGui::Button("Use its own colour", ImVec2(-1.f, 0.f))) {
            editor.doc.beginAction("Detach from palette");
            // Keep what is on screen: detaching should not move the colour.
            setPaintColor(editor.doc, *layer,
                          effectiveLayerColor(editor.doc, editor.sprite, *layer));
            setLayerRole(editor.doc, *layer, ls::kColorRoleNone);
            editor.doc.endAction();
            syncColorFromLayer(editor);
            canvas.invalidate();
            editor.say("This layer now carries its own colour");
        }
        return;
    }

    if (ImGui::ColorPicker4("##colour", editor.color,
                            ImGuiColorEditFlags_NoSidePreview |
                            ImGuiColorEditFlags_NoSmallPreview |
                            ImGuiColorEditFlags_DisplayHex)) {
        setPaintColor(editor.doc, *layer, toColor(editor.color));
        canvas.invalidate();
        editor.say("Recoloured without touching the drawing");
    }
    bracketDrag(editor, editor.recolouring, "Recolour");
}

// ---------------------------------------------------------------- palette --

void drawPalettePanel(Editor& editor, CanvasView& canvas) {
    PaintLayer* layer = editor.active();
    const std::vector<PaletteEntry> entries = paletteEntries(editor.doc);

    if (entries.empty()) {
        ImGui::TextDisabled("No palette.");
        return;
    }

    const bool dithered = layer != nullptr && layerIsDithered(editor.doc, *layer);
    if (dithered) {
        ImGui::PushStyleColor(ImGuiCol_Text, theme::palette().textDim);
        ImGui::TextWrapped("This layer is dithered, so its colours come from its "
                           "ramp rather than a palette slot.");
        ImGui::PopStyleColor();
        ImGui::Dummy(ImVec2(0.f, 4.f));
    }

    const ls::ColorRole current = layer != nullptr ? layerRole(editor.doc, *layer)
                                                   : ls::kColorRoleNone;

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
            if (layer != nullptr && !dithered) {
                editor.doc.beginAction("Use palette colour");
                setLayerRole(editor.doc, *layer, entry.role);
                editor.doc.endAction();
                fromColor(entry.color, editor.color);
                canvas.invalidate();
                editor.say("Layer paints through slot " +
                           std::to_string(entry.role));
            }
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Slot %u  -  #%02X%02X%02X\nClick to use it. "
                              "Double-click to edit it.",
                              entry.role, entry.color.r, entry.color.g, entry.color.b);
        }
        // Double-click opens the editor for the entry itself, which changes it
        // for every layer using it.
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            ImGui::OpenPopup(("edit" + id).c_str());
        }
        if (ImGui::BeginPopup(("edit" + id).c_str())) {
            float rgba[4];
            fromColor(entry.color, rgba);
            ImGui::TextUnformatted("Palette slot");
            ImGui::SameLine();
            ImGui::TextDisabled("%u", entry.role);
            if (ImGui::ColorPicker4("##edit", rgba,
                                    ImGuiColorEditFlags_NoSidePreview |
                                    ImGuiColorEditFlags_DisplayHex)) {
                setPaletteEntry(editor.doc, entry.role, toColor(rgba));
                canvas.invalidate();
                editor.say("Every layer using this slot recoloured");
            }
            bracketDrag(editor, editor.draggingPalette, "Palette colour");
            ImGui::EndPopup();
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
            editor.say("Added as slot " + std::to_string(added));
        }
    }

    if (current != ls::kColorRoleNone) {
        ImGui::PushStyleColor(ImGuiCol_Text, theme::palette().textDim);
        ImGui::TextWrapped("Editing a swatch changes every layer that uses it, "
                           "from the drawing rather than over it.");
        ImGui::PopStyleColor();
    }
}

// ------------------------------------------------------------------ shape --

void drawShapePanel(Editor& editor, CanvasView& canvas) {
    PaintLayer* layer = editor.active();
    if (layer == nullptr) {
        ImGui::TextDisabled("No layer selected.");
        return;
    }

    ShapeLayer shape;
    const bool isShape = shapeOfLayer(editor.doc, *layer, &shape);

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
                ImGui::SetNextItemWidth(-42.f);
                if (ImGui::SliderFloat("round", &editor.shapeCorner, 0.f, 12.f,
                                       "%.1f")) {
                    changed = true;
                }
                bracketDrag(editor, editor.editingShape, "Corner radius");
                params.cornerRadius = editor.shapeCorner;
            }

            if (changed) {
                updateShape(editor.doc, shape, params);
                canvas.invalidate();
                editor.say("The shape is still a shape");
            }
        }
    } else {
        ImGui::PushStyleColor(ImGuiCol_Text, theme::palette().textDim);
        ImGui::TextWrapped("This layer was drawn by hand, so there is no shape "
                           "to edit. Draw with a shape tool to get one that "
                           "stays adjustable.");
        ImGui::PopStyleColor();
    }

    ImGui::Dummy(ImVec2(0.f, theme::metrics().sectionGap));
    theme::sectionHeader("OUTLINE");

    bool outlined = hasOutline(editor.doc, *layer);
    if (ImGui::Checkbox("Outline this layer", &outlined)) {
        editor.doc.beginAction(outlined ? "Add outline" : "Remove outline");
        if (outlined) {
            addOutline(editor.doc, *layer, ls::Color{20, 22, 28, 255}, 1);
        } else {
            removeOutline(editor.doc, *layer);
        }
        editor.doc.endAction();
        canvas.invalidate();
    }
    ImGui::SameLine();
    theme::hint("Generated during the compile from whatever the layer draws, "
                "so it follows the artwork instead of being stamped where the "
                "artwork used to be. Move the shape and the outline moves.");

    if (!outlined) {
        return;
    }

    int thickness = outlineThickness(editor.doc, *layer);
    ImGui::SetNextItemWidth(-1.f);
    if (ImGui::SliderInt("##thickness", &thickness, 1, 8, "thickness  %d")) {
        setOutlineThickness(editor.doc, *layer, thickness);
        canvas.invalidate();
    }
    bracketDrag(editor, editor.editingShape, "Outline thickness");

    const ls::Color current = outlineColor(editor.doc, *layer);
    float rgba[4];
    fromColor(current, rgba);
    if (ImGui::ColorEdit4("colour", rgba, ImGuiColorEditFlags_NoInputs)) {
        setOutlineColor(editor.doc, *layer, toColor(rgba));
        canvas.invalidate();
    }
    bracketDrag(editor, editor.editingShape, "Outline colour");
}

// ----------------------------------------------------------------- layers --

void drawLayerPanel(Editor& editor, CanvasView& canvas) {
    if (ImGui::Button("Add", ImVec2(60.f, 0.f))) {
        PaintLayer layer;
        const std::string name = "Layer " + std::to_string(editor.layers.size() + 1);
        if (createPaintLayer(editor.doc, editor.sprite, name,
                             toColor(editor.color), &layer)) {
            editor.layers.push_back(layer);
            editor.activeLayer = static_cast<int>(editor.layers.size()) - 1;
            canvas.invalidate();
        }
    }
    ImGui::SameLine();

    const bool canDelete = editor.layers.size() > 1;
    ImGui::BeginDisabled(!canDelete);
    if (ImGui::Button("Delete", ImVec2(70.f, 0.f))) {
        if (PaintLayer* layer = editor.active()) {
            editor.doc.beginAction("Delete layer");
            editor.doc.engine().deleteLayer(layer->layer);
            editor.doc.endAction();
            resyncLayers(editor);
            canvas.invalidate();
            editor.say("Layer deleted");
        }
    }
    ImGui::EndDisabled();
    if (!canDelete && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("A sprite needs at least one layer.");
    }

    ImGui::Dummy(ImVec2(0.f, 4.f));

    // Topmost first, which is how a layer stack reads everywhere else.
    for (int i = static_cast<int>(editor.layers.size()) - 1; i >= 0; --i) {
        const size_t index = static_cast<size_t>(i);
        ImGui::PushID(i);

        auto info = editor.doc.engine().getLayerInfo(editor.layers[index].layer);
        const std::string name = info.ok() ? info.value.name : "?";

        bool visible = info.ok() ? info.value.visible : true;
        if (ImGui::Checkbox("##visible", &visible)) {
            editor.doc.beginAction(visible ? "Show layer" : "Hide layer");
            editor.doc.engine().setLayerVisibility(editor.layers[index].layer, visible);
            editor.doc.endAction();
            canvas.invalidate();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s this layer", visible ? "Hide" : "Show");
        }
        ImGui::SameLine();

        if (editor.renaming == i) {
            ImGui::SetNextItemWidth(-1.f);
            if (ImGui::IsWindowAppearing() || ImGui::IsItemDeactivated()) {
                ImGui::SetKeyboardFocusHere();
            }
            if (ImGui::InputText("##rename", editor.renameBuffer,
                                 sizeof(editor.renameBuffer),
                                 ImGuiInputTextFlags_EnterReturnsTrue)) {
                editor.doc.beginAction("Rename layer");
                editor.doc.engine().setLayerName(editor.layers[index].layer,
                                                 editor.renameBuffer);
                editor.doc.endAction();
                editor.renaming = -1;
            }
            if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                editor.renaming = -1;
            }
        } else {
            // A swatch of what the layer resolves to, so the stack can be read
            // at a glance rather than by selecting each one.
            const ls::Color shown =
                effectiveLayerColor(editor.doc, editor.sprite, editor.layers[index]);
            const ImU32 colour = IM_COL32(shown.r, shown.g, shown.b, shown.a);
            theme::swatch(("layer" + std::to_string(i)).c_str(), colour, false, 14.f);
            ImGui::SameLine();

            if (ImGui::Selectable(name.c_str(), editor.activeLayer == i)) {
                editor.activeLayer = i;
                syncColorFromLayer(editor);
            }
            if (ImGui::IsItemHovered() &&
                ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                editor.renaming = i;
                std::snprintf(editor.renameBuffer, sizeof(editor.renameBuffer),
                              "%s", name.c_str());
            }
        }
        ImGui::PopID();
    }
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

    const float rightWidth = 430.f;
    ImGui::SameLine(ImGui::GetWindowWidth() - rightWidth);

    theme::statusItem("Canvas", canvasSize.c_str());
    ImGui::SameLine(0.f, 18.f);
    theme::statusItem("Cursor", cursor.c_str());
    ImGui::SameLine(0.f, 18.f);
    theme::statusItem("Zoom", zoom.c_str());
    ImGui::SameLine(0.f, 18.f);
    theme::statusItem("Compile", compile.c_str(), false);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("How long the last real compile took. Cached frames "
                          "cost nothing, so this only moves when something "
                          "changed.");
    }
}

} // namespace fast
