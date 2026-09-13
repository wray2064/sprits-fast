// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

#include "ui/panels.h"
#include "ui/theme.h"

#include "app/palette_io.h"
#include "app/shape.h"
#include "app/transform.h"

#include <algorithm>
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

    // One row per end: a selector for which end the palette assigns to, the
    // colour, and the slot it follows if any.
    const auto rampEnd = [&](int index, const char* label, ls::Color& colour,
                             ls::ColorRole& role) {
        ImGui::PushID(label);
        if (ImGui::RadioButton("##end", editor.rampEnd == index)) {
            editor.rampEnd = index;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Clicks in the palette set this end");
        }
        ImGui::SameLine();
        float rgba[4];
        fromColor(colour, rgba);
        if (ImGui::ColorEdit4(label, rgba, ImGuiColorEditFlags_NoInputs)) {
            colour = toColor(rgba);
            role = ls::kColorRoleNone;         // a picked colour is a value
            changed = true;
        }
        bracketDrag(editor, editor.draggingDither, "Ramp colour");
        if (role != ls::kColorRoleNone) {
            ImGui::SameLine();
            const std::string slot = "slot " + std::to_string(role);
            ImGui::TextColored(theme::palette().accent, "%s", slot.c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton("detach")) {
                role = ls::kColorRoleNone;
                changed = true;
            }
        }
        ImGui::PopID();
    };
    rampEnd(0, "dark",  settings.from, settings.fromRole);
    ImGui::SameLine();
    theme::hint("Each end is a colour or a palette slot. Select an end, then "
                "click a slot in the palette to point it there -- the dither "
                "then recolours with a palette change like everything else. "
                "Pick a colour to make it a value again.");
    rampEnd(1, "light", settings.to,   settings.toRole);

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
                    syncColorFromLayer(editor);
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
                syncColorFromLayer(editor);
                canvas.invalidate();
            }
        }
        for (const PaletteInfo& info : palettes) {
            ImGui::PushID(static_cast<int>(info.id.value));
            if (ImGui::Selectable(info.name.c_str(), info.id == own) && info.id != own) {
                editor.doc.beginAction("Frame palette");
                bindFrame(editor.doc, sprite, info.id);
                editor.doc.endAction();
                syncColorFromLayer(editor);
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

    const bool dithered = layer != nullptr && layerIsDithered(editor.doc, *layer);
    DitherSettings ditherNow;
    if (dithered) {
        readDitherSettings(editor.doc, *layer, &ditherNow);
        ImGui::PushStyleColor(ImGuiCol_Text, theme::palette().textDim);
        ImGui::TextWrapped("This layer is dithered. Clicking a slot sets its %s "
                           "end; choose the end in the Tool panel.",
                           editor.rampEnd == 0 ? "dark" : "light");
        ImGui::PopStyleColor();
        ImGui::Dummy(ImVec2(0.f, 4.f));
    }

    // Which slot is "current" -- ringed -- is the layer's role for a solid
    // layer, and the selected ramp end's role for a dithered one.
    const ls::ColorRole current = layer == nullptr ? ls::kColorRoleNone
        : dithered ? (editor.rampEnd == 0 ? ditherNow.fromRole : ditherNow.toRole)
                   : layerRole(editor.doc, *layer);

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
            if (layer != nullptr && dithered) {
                // Point the selected end of the ramp at this slot. The colour
                // is kept as the fallback, so removing the slot later reverts
                // rather than breaks.
                DitherSettings settings = ditherNow;
                if (editor.rampEnd == 0) {
                    settings.fromRole = entry.role;
                    settings.from = entry.color;
                } else {
                    settings.toRole = entry.role;
                    settings.to = entry.color;
                }
                editor.doc.beginAction("Ramp end from palette");
                applyDitherSettings(editor.doc, *layer, settings);
                editor.doc.endAction();
                editor.dither = settings;
                canvas.invalidate();
                editor.say(std::string(editor.rampEnd == 0 ? "Dark" : "Light") +
                           " end follows slot " + std::to_string(entry.role));
            } else if (layer != nullptr) {
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
            if (entry.label.empty()) {
                ImGui::SetTooltip("Slot %u  -  #%02X%02X%02X\nClick to use it. "
                                  "Double-click to edit it.",
                                  entry.role, entry.color.r, entry.color.g, entry.color.b);
            } else {
                ImGui::SetTooltip("%s  (slot %u)  -  #%02X%02X%02X\nClick to use it. "
                                  "Double-click to edit it.",
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

    // A layer naming a slot the palette no longer has. It still draws -- in
    // the colour it showed when the slot went -- but no swatch is ringed, and
    // without this line nothing says why, or offers the way back. Putting the
    // slot back at that colour changes no pixel and re-attaches every layer
    // that named it, on every frame.
    if (current != ls::kColorRoleNone && layer != nullptr) {
        bool present = false;
        for (const PaletteEntry& entry : entries) {
            present = present || entry.role == current;
        }
        if (!present) {
            ImGui::Dummy(ImVec2(0.f, 4.f));
            ImGui::PushStyleColor(ImGuiCol_Text, theme::palette().textDim);
            ImGui::TextWrapped("%s names slot %u, which the palette no longer "
                               "has, so it shows its own colour.",
                               dithered ? (editor.rampEnd == 0 ? "The dark end"
                                                               : "The light end")
                                        : "This layer",
                               current);
            ImGui::PopStyleColor();
            if (ImGui::Button("Put the slot back", ImVec2(-1.f, 0.f))) {
                const ls::Color shownColour = dithered
                    ? (editor.rampEnd == 0 ? ditherNow.from : ditherNow.to)
                    : effectiveLayerColor(editor.doc, editor.sprite, *layer);
                editor.doc.beginAction("Restore palette slot");
                setPaletteEntry(editor.doc, shown, current, shownColour);
                editor.doc.endAction();
                canvas.invalidate();
                editor.say("Slot " + std::to_string(current) +
                           " is back; everything that named it follows it again");
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("At the colour it shows now, so nothing changes "
                                  "until you edit the slot.");
            }
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
        ImGui::SetTooltip(".gpl keeps the slot names; .hex is just the colours.");
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

void drawLayerPanel(Editor& editor, CanvasView& canvas) {
    const ls::SpriteId sprite = editor.activeSprite();
    const std::vector<ls::LayerId> order = layerOrder(editor.doc, sprite);

    // --- the buttons ---------------------------------------------------------
    if (ImGui::Button("Add", ImVec2(52.f, 0.f))) {
        PaintLayer layer;
        const std::string name = "Layer " + std::to_string(order.size() + 1);
        if (createPaintLayer(editor.doc, sprite, name, toColor(editor.color), &layer)) {
            // Right above the active layer, in its group, like every editor.
            if (PaintLayer* active = editor.active()) {
                const int at = indexOfLayer(editor.doc, sprite, active->layer);
                moveLayer(editor.doc, layer.layer, at + 1);
            }
            selectLayer(editor, layer.layer);
            canvas.invalidate();
        }
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
                bool visible = group.visible;
                if (ImGui::Checkbox("##gvisible", &visible)) {
                    editor.doc.beginAction(visible ? "Show group" : "Hide group");
                    setGroupVisible(editor.doc, openGroup, visible);
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
                    acceptDrop(i);          // dropping on the group row: top of the group
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
        bool visible = props.visible;
        if (ImGui::Checkbox("##visible", &visible)) {
            editor.doc.beginAction(visible ? "Show layer" : "Hide layer");
            setLayerVisible(editor.doc, id, visible);
            editor.doc.endAction();
            canvas.invalidate();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s this layer", visible ? "Hide" : "Show");
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
            // A swatch of what the layer resolves to, so the stack can be read
            // at a glance rather than by selecting each one.
            ImU32 colour = IM_COL32(90, 90, 90, 255);
            if (drawable) {
                const ls::Color shown = effectiveLayerColor(
                    editor.doc, sprite, editor.layers[static_cast<size_t>(listIndex)]);
                colour = IM_COL32(shown.r, shown.g, shown.b, shown.a);
            }
            theme::swatch("layer", colour, false, 14.f);
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
            if (ImGui::Selectable(label.c_str(), selected)) {
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
                ImGui::EndDragDropSource();
            }
            acceptDrop(i);

            if (drawable && ImGui::BeginPopupContextItem("menu")) {
                if (!layerSelected(editor, id) || editor.activeGroup.valid()) {
                    selectLayer(editor, id);
                }
                if (ImGui::MenuItem("Duplicate", "Ctrl+J")) { duplicateActiveLayer(editor, canvas); }
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
        ImGui::TextWrapped("Drag to reorder; right-click for the rest.");
        ImGui::PopStyleColor();
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
