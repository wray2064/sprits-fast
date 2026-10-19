// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

// panel_elements.cpp -- the elements of a layer and the picked one's properties.
// Declared in panels.h; what the panel files share is in panels_internal.h.

#include "ui/panels_internal.h"
#include "app/i18n.h"
#include "ui/theme.h"
#include "ui/widgets.h"
#include "ui/tile_tools.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace fast {

// ------------------------------------------------------------------ shape --

namespace {

// What an element looks like, for its row: the slot's colour when it paints
// through one, its own otherwise, and the middle of the ramp for a dither.
ImU32 elementChip(Editor& editor, ls::LayerId layer, const Element& element) {
    if (element.kind == ElementKind::Erase) {
        return ImGui::GetColorU32(theme::palette().textDim);
    }
    PaintLayer as;
    as.layer = layer;
    as.fill = element.fill;
    as.region = element.region;
    const ls::Color c = effectiveLayerColor(editor.doc, editor.sprite, as);
    return IM_COL32(c.r, c.g, c.b, c.a == 0 ? 255 : c.a);
}

std::string elementLabel(Editor& editor, const Element& element) {
    std::string label = elementKindName(element.kind);
    if (element.kind == ElementKind::Erase) {
        auto pixels = editor.doc.engine().getRegionIntervals(element.region);
        return label + "  " +
               std::to_string(pixels.ok() ? ls::geom::pixelCount(pixels.value) : 0) + " px";
    }
    if (element.kind != ElementKind::Paint && element.kind != ElementKind::Fill) {
        return element.outlined ? label + "  outline" : label;
    }
    Ink ink;
    if (!inkOfElement(editor.doc, element.fill, &ink)) {
        // A fill coloured by a dither is what the gradient tool makes.
        return element.kind == ElementKind::Fill ? "Gradient" : "Dither";
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
    if (ImGui::Checkbox(tr("Dithered fill"), &dithered)) {
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
    theme::hint(tr("A dither compares a value against a threshold matrix and picks "
                "between two ramp stops. Switching back and forth costs "
                "nothing: the drawing is never touched."));

    if (dithered) {
        ImGui::Checkbox(tr("Paint with this dither"), &editor.paintIntoElement);
        ImGui::SameLine();
        theme::hint(tr("On: the pencil and the bucket paint into this element, so "
                    "what you draw takes the dither. Off: they paint the "
                    "current colour, as usual."));
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
            if (ImGui::Button(tr("Put the slot back"), ImVec2(-1.f, 0.f))) {
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
            if (ImGui::SmallButton(tr("Use its own colour"))) {
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
        if (ImGui::ColorEdit4(tr("colour##pixels"), rgba, ImGuiColorEditFlags_NoInputs)) {
            Ink changed = ink;
            changed.colour = toColor(rgba);
            setElementInk(editor.doc, target.fill, changed);
            canvas.invalidate();
            editor.say("Recoloured without touching the drawing");
        }
        bracketDrag(editor, editor.recolouring, "Recolour");
    }

    ImGui::SameLine();
    if (ImGui::SmallButton(tr("= current"))) {
        editor.doc.beginAction("Recolour pixels");
        setElementInk(editor.doc, target.fill, foregroundInk(editor));
        editor.doc.endAction();
        canvas.invalidate();
        editor.say("These pixels take the current colour; the drawing is untouched");
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", tr("Give these pixels the current colour -- its slot, when "
                          "it has one. A replace-colour that can be changed back."));
    }
}

} // namespace

// Which element of the layer the panels are about: the one picked, or the
// first when none is.
static const Element* pickedElement(Editor& editor, const std::vector<Element>& elements) {
    const Element* selected = nullptr;
    for (const Element& element : elements) {
        if (element.fill == editor.activeElement) { selected = &element; }
    }
    if (selected == nullptr && !elements.empty()) {
        selected = &elements.front();
        editor.activeElement = selected->fill;
        editor.paintIntoElement = false;
    }
    return selected;
}

// The layer's elements, one row each, topmost first: the stack of marks the
// layer is made of -- a run of one colour, a fill, a gradient, a shape, text.
// The picked one is what Properties edits.
static void drawElementList(Editor& editor, CanvasView& canvas, PaintLayer* layer) {
    const std::vector<Element> elements = elementsOf(editor.doc, layer->layer);
    const Element* selected = pickedElement(editor, elements);
    // Topmost first, the way the layer stack reads.
    for (size_t n = elements.size(); n-- > 0;) {
        const Element& element = elements[n];
        ImGui::PushID(static_cast<int>(element.fill.value));
        ImGui::ColorButton("##chip", ImGui::ColorConvertU32ToFloat4(
                               elementChip(editor, layer->layer, element)),
                           ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoBorder,
                           ImVec2(theme::px(12.f), theme::px(12.f)));
        ImGui::SameLine();
        const std::string label = elementLabel(editor, element) + "##" +
                                  std::to_string(n);
        if (ImGui::Selectable(label.c_str(), &element == selected,
                              0, ImVec2(ImGui::GetContentRegionAvail().x -
                                        (widgets::buttonWidth("x") +
                                         ImGui::GetStyle().ItemSpacing.x), 0.f))) {
            if (editor.activeElement != element.fill) {
                editor.paintIntoElement = false;
            }
            editor.activeElement = element.fill;
            selected = &element;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton(tr("x"))) {
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
            ImGui::SetTooltip("%s", tr("Remove this element. The others stay."));
        }
        ImGui::PopID();
    }
    // Beside the last row when there is room for it, under it when the row
    // takes the width.
    widgets::sameLineIfFits(theme::hintWidth());
    theme::hint(tr("A layer holds several marks: one element per colour its "
                "pixels are painted in, and any shapes drawn onto it. Each "
                "shape stays a shape; each colour stays a rule, so it can be "
                "changed without repainting."));
    ImGui::Dummy(ImVec2(0.f, 4.f));
}

// The picked element's own controls, under a line that says what it is.
static void drawElementsPart(Editor& editor, CanvasView& canvas, PaintLayer* layer) {
    const std::vector<Element> elements = elementsOf(editor.doc, layer->layer);
    const Element* selected = pickedElement(editor, elements);
    if (selected != nullptr) {
        ImGui::ColorButton("##picked", ImGui::ColorConvertU32ToFloat4(
                               elementChip(editor, layer->layer, *selected)),
                           ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoBorder,
                           ImVec2(theme::px(14.f), theme::px(14.f)));
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, theme::palette().textBright);
        ImGui::TextUnformatted(elementLabel(editor, *selected).c_str());
        ImGui::PopStyleColor();
        ImGui::Separator();
        ImGui::Dummy(ImVec2(0.f, 2.f));
    }

    if (selected != nullptr && selected->kind == ElementKind::Text) {
        TextSpec spec;
        if (readTextElement(editor.doc, selected->region, &spec)) {
            theme::sectionHeader(tr("TEXT"));
            char buffer[kMaxTextLength + 1] = {};
            std::snprintf(buffer, sizeof(buffer), "%s", spec.text.c_str());
            bool changed = false;
            ImGui::InputTextMultiline("##text", buffer, sizeof(buffer),
                                      ImVec2(-1.f, ImGui::GetTextLineHeight() * 3.5f));
            // Written when the field lets go, so a word is one undo step
            // rather than one per letter.
            if (ImGui::IsItemDeactivatedAfterEdit() && buffer[0] != '\0' && spec.text != buffer) {
                spec.text = buffer;
                editor.doc.beginAction("Retype");
                updateTextElement(editor.doc, selected->region, spec);
                editor.doc.endAction();
                canvas.invalidate();
            }
            int at[2] = { spec.at.x, spec.at.y };
            ImGui::SetNextItemWidth(-widgets::labelRoom("at"));
            if (ImGui::DragInt2("at", at, 0.2f)) {
                spec.at = { at[0], at[1] };
                changed = true;
            }
            bracketDrag(editor, editor.editingShape, "Move text");
            ImGui::SetNextItemWidth(-widgets::labelRoom(tr("size")));
            if (widgets::sliderInt(tr("size"), &spec.scale, 1, 8, "%dx")) {
                changed = true;
            }
            bracketDrag(editor, editor.editingShape, "Text size");
            if (changed) {
                updateTextElement(editor.doc, selected->region, spec);
                canvas.invalidate();
            }
            ImGui::PushStyleColor(ImGuiCol_Text, theme::palette().textDim);
            ImGui::TextWrapped("%s", tr("Still text: the pixels are rebuilt from the words. Its "
                               "colour is set like any other -- pick one and press = "
                               "current in the list's pixels, or edit its slot."));
            ImGui::PopStyleColor();
            if (ImGui::SmallButton(tr("Current colour##text"))) {
                editor.doc.beginAction("Recolour text");
                setElementInk(editor.doc, selected->fill, foregroundInk(editor));
                editor.doc.endAction();
                canvas.invalidate();
            }
        }
        ImGui::Dummy(ImVec2(0.f, theme::metrics().sectionGap));
    }

    ShapeLayer shape;
    const bool isShape = selected != nullptr && selected->isGeometry();
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
            const bool path = shape.kind == ShapeKind::Polygon || shape.kind == ShapeKind::Curve;

            if (path) {
                // A polygon or a curve is its points, edited on the canvas;
                // here it moves as a whole, by its top-left corner.
                ImGui::SetNextItemWidth(-widgets::labelRoom("at"));
                if (ImGui::DragFloat2("at", from, 0.25f, 0.f, 0.f, "%.0f")) {
                    const ls::Vec2f by { std::round(from[0]) - params.from.x,
                                         std::round(from[1]) - params.from.y };
                    mapShapePoints(params, [by](ls::Vec2f p) {
                        return ls::Vec2f{ p.x + by.x, p.y + by.y };
                    });
                    changed = true;
                }
                bracketDrag(editor, editor.editingShape, "Move shape");
                const size_t count = shape.kind == ShapeKind::Curve
                    ? (params.points.size() + 2) / 3 - (params.closed ? 1 : 0)
                    : params.points.size();
                ImGui::TextDisabled("%zu %s -- drag them on the canvas", count,
                                    shape.kind == ShapeKind::Curve ? "points" : "corners");
                if (shape.kind == ShapeKind::Curve) {
                    bool closed = params.closed;
                    if (ImGui::Checkbox(tr("Closed##curve"), &closed)) {
                        editor.doc.beginAction(closed ? "Close the curve" : "Open the curve");
                        params.closed = closed;
                        updateShape(editor.doc, shape, params);
                        editor.doc.endAction();
                        canvas.invalidate();
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("%s", tr("Joined back to its first point and filled: a "
                                          "shape with curved sides that stays editable."));
                    }
                }
            } else {
                // A line's ends are the middles of pixels (linePoint); shown
                // and typed as the pixels themselves.
                const bool line = shape.kind == ShapeKind::Line;
                const auto shown = [line](float v) { return line ? std::floor(v) : v; };
                const auto stored = [line](float a, float b) {
                    return line ? linePoint({ std::round(a), std::round(b) }) : ls::Vec2f{ a, b };
                };
                from[0] = shown(from[0]); from[1] = shown(from[1]);
                to[0] = shown(to[0]);     to[1] = shown(to[1]);
                ImGui::SetNextItemWidth(-widgets::labelRoom("from"));
                if (ImGui::DragFloat2("from", from, 0.25f, 0.f, 0.f, "%.0f")) {
                    params.from = stored(from[0], from[1]);
                    changed = true;
                }
                bracketDrag(editor, editor.editingShape, "Edit shape");

                ImGui::SetNextItemWidth(-widgets::labelRoom("to"));
                if (ImGui::DragFloat2("to", to, 0.25f, 0.f, 0.f, "%.0f")) {
                    params.to = stored(to[0], to[1]);
                    changed = true;
                }
                bracketDrag(editor, editor.editingShape, "Edit shape");
            }

            if (shape.kind == ShapeKind::Rectangle) {
                // The rectangle's own radius, read back from its geometry. A
                // radius set here is also the one the next rectangle starts
                // with.
                ImGui::SetNextItemWidth(-widgets::labelRoom(tr("round")));
                if (widgets::sliderFloat(tr("round"), &params.cornerRadius, 0.f, 12.f,
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

            // Its area, or its edge: the same shape either way. Lines and
            // curves have no area.
            if (shape.kind != ShapeKind::Line && shape.kind != ShapeKind::Curve) {
                float width = 1.f;
                bool outlined = shapeIsOutlined(editor.doc, selected->fill, &width);
                ls::OperationId op = selected->fill;
                if (ImGui::Checkbox(tr("Outline only##element"), &outlined)) {
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
                    if (widgets::sliderFloat("##elementwidth", &width, 1.f, 8.f, "width %.0f")) {
                        setShapeOutlined(editor.doc, layer->layer, &op, true, std::round(width));
                        canvas.invalidate();
                    }
                    bracketDrag(editor, editor.editingShape, "Outline width");
                }
            }

            if (ImGui::SmallButton(tr("Current colour"))) {
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
                ImGui::SetTooltip("%s", tr("Give this shape the current colour -- its slot, "
                                  "when it has one."));
            }
        }
    } else if (selected != nullptr && selected->kind == ElementKind::Erase) {
        theme::sectionHeader(tr("ERASED"));
        ImGui::PushStyleColor(ImGuiCol_Text, theme::palette().textDim);
        ImGui::TextWrapped("%s", tr("What the eraser or Delete took from the shapes under "
                           "it. They are still shapes: this is a mask over them, kept "
                           "as pixels of its own, and it clears only what was drawn "
                           "before it. Remove it (x) and they come back whole."));
        ImGui::PopStyleColor();
    } else if (selected != nullptr && selected->kind == ElementKind::Paint) {
        PaintLayer target;
        target.layer = layer->layer;
        target.fill = selected->fill;
        target.region = selected->region;
        theme::sectionHeader(tr("PIXELS"));
        drawPixelsProperties(editor, canvas, target);
    }

}

namespace {

// A drop shadow cast by what the layer draws.
void drawShadowSection(Editor& editor, CanvasView& canvas, PaintLayer* layer) {
    bool shadowed = hasShadow(editor.doc, *layer);
    if (ImGui::Checkbox(tr("Drop shadow"), &shadowed)) {
        editor.doc.beginAction(shadowed ? "Add shadow" : "Remove shadow");
        if (shadowed) {
            setShadow(editor.doc, *layer, ShadowSettings{});
        } else {
            removeShadow(editor.doc, *layer);
        }
        editor.doc.endAction();
        canvas.invalidate();
    }
    ImGui::SameLine();
    theme::hint(tr("Cast by whatever the layer draws -- or the whole figure -- and "
                "worked out as the sprite is drawn, so it follows the artwork. "
                "It falls only where nothing else is."));
    if (shadowed) {
        ShadowSettings settings = shadowOf(editor.doc, *layer);
        bool changed = false;
        const char* scopes[] = { "Cast by this layer", "Cast by the figure" };
        int scope = static_cast<int>(settings.scope);
        ImGui::SetNextItemWidth(-1.f);
        if (ImGui::Combo("##shadowscope", &scope, scopes, 2)) {
            settings.scope = static_cast<OutlineScope>(scope);
            changed = true;
        }
        int offset[2] = { settings.dx, settings.dy };
        ImGui::SetNextItemWidth(-widgets::labelRoom("offset"));
        if (ImGui::SliderInt2("offset", offset, -kMaxShadowOffset, kMaxShadowOffset)) {
            settings.dx = offset[0];
            settings.dy = offset[1];
            changed = true;
        }
        bracketDrag(editor, editor.editingShape, "Shadow offset");
        float rgba[4];
        fromColor(settings.colour, rgba);
        if (ImGui::ColorEdit3("##shadowcolour", rgba, ImGuiColorEditFlags_NoInputs)) {
            settings.colour = toColor(rgba);
            settings.colour.a = 255;
            settings.role = ls::kColorRoleNone;
            changed = true;
        }
        bracketDrag(editor, editor.editingShape, "Shadow colour");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-widgets::labelRoom(tr("opacity##shadow")));
        if (widgets::sliderFloat(tr("opacity##shadow"), &settings.opacity, 0.f, 1.f, "%.2f")) {
            changed = true;
        }
        bracketDrag(editor, editor.editingShape, "Shadow opacity");
        if (changed) {
            setShadow(editor.doc, *layer, settings);
            canvas.invalidate();
        }
    }
}

// An outline generated round what the layer draws.
void drawOutlineSection(Editor& editor, CanvasView& canvas, PaintLayer* layer) {
    const bool wasOutlined = hasOutline(editor.doc, *layer);
    bool outlined = wasOutlined;
    if (ImGui::Checkbox(tr("Outline"), &outlined)) {
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
    theme::hint(tr("Generated during the compile from whatever is drawn, so it "
                "follows the artwork instead of being stamped where the artwork "
                "used to be. Move the shape and the outline moves."));

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
        ImGui::SetTooltip("%s", tr("This layer: a line round what this layer draws, which\n"
                          "is what one part of a character wants.\n"
                          "Whole sprite: one line round the figure however many\n"
                          "layers it is built from, with no seam where they meet."));
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
    widgets::sliderInt("##thickness", &settings.thickness, 1, kMaxOutlineThickness,
                     "thickness  %d");
    bracketDrag(editor, editor.editingShape, "Outline thickness");

    // The colour, as a value or as a palette slot. A slot is the better answer
    // when there is one: the outline then joins a palette swap instead of being
    // the one thing left behind by it.
    float rgba[4];
    fromColor(settings.colour, rgba);
    if (ImGui::ColorEdit4(tr("colour"), rgba, ImGuiColorEditFlags_NoInputs)) {
        settings.colour = toColor(rgba);
        settings.role = ls::kColorRoleNone;      // a picked colour is a value
    }
    bracketDrag(editor, editor.editingShape, "Outline colour");

    // Beside the colour when the longer of the two answers fits, under it when not.
    widgets::sameLineIfFits(widgets::buttonWidth("use a palette slot"));
    const bool usingRole = settings.role != ls::kColorRoleNone;
    if (usingRole) {
        const std::string label = "slot " + std::to_string(settings.role);
        ImGui::TextColored(theme::palette().accent, "%s", label.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton(tr("detach"))) {
            settings.role = ls::kColorRoleNone;
        }
    } else if (ImGui::SmallButton(tr("use a palette slot"))) {
        // The slot the layer already paints through, so the outline and the
        // fill move together under a palette swap unless told otherwise.
        const ls::ColorRole fillRole = layerRole(editor.doc, *layer);
        settings.role = fillRole != ls::kColorRoleNone ? fillRole : 0;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", tr("Take the colour from a palette slot, so changing what "
                          "the slot means recolours every outline using it -- "
                          "from the drawing rather than over it."));
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

} // namespace

void drawElementsPanel(Editor& editor, CanvasView& canvas) {
    PaintLayer* layer = editor.active();
    if (layer == nullptr) {
        ImGui::TextDisabled("%s", tr("No layer selected."));
        return;
    }
    // A tilemap layer's elements are its tiles, which want the room
    // Properties has.
    TilemapLayer tilemap;
    if (readTilemapLayer(editor.doc, layer->layer, &tilemap)) {
        ImGui::TextDisabled("%s", tr("A tilemap layer: its tiles are in Properties."));
        return;
    }
    drawElementList(editor, canvas, layer);
}

void drawPropertiesPanel(Editor& editor, CanvasView& canvas) {
    PaintLayer* layer = editor.active();
    if (layer == nullptr) {
        ImGui::TextDisabled("%s", tr("No layer selected."));
        return;
    }
    TilemapLayer tilemap;
    if (readTilemapLayer(editor.doc, layer->layer, &tilemap)) {
        drawTilesSection(editor, canvas);
    } else {
        drawElementsPart(editor, canvas, layer);
    }
    ImGui::Dummy(ImVec2(0.f, theme::metrics().sectionGap));
    // The layer's effects, under the picked element's own controls: they
    // belong to the whole layer, and fold away when they are not wanted.
    if (theme::foldingHeader(tr("SHADOW"))) {
        drawShadowSection(editor, canvas, layer);
    }
    if (theme::foldingHeader(tr("OUTLINE"))) {
        drawOutlineSection(editor, canvas, layer);
    }
}


} // namespace fast
