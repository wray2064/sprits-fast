// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

// panel_palette.cpp -- the palette panel.
// Declared in panels.h; what the panel files share is in panels_internal.h.

#include "ui/panels_internal.h"
#include "app/i18n.h"
#include "ui/theme.h"
#include "ui/widgets.h"
#include "ui/ui_script.h"

#include "app/palette_io.h"
#include "app/palette_tools.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace fast {

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
    const float swapWidth = theme::px(30.f);
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
        ImGui::SetTooltip("%s", tr("The palette every frame uses. Switching it recolours "
                          "the whole animation in one step, from the drawing."));
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
    const bool open = ImGui::CollapsingHeader(tr("Palettes"));
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
                ImGui::SetTooltip("%s", tr("Use this palette for every frame"));
            }
            ImGui::SameLine();
            ImGui::Selectable(info.name.c_str(), false,
                              ImGuiSelectableFlags_AllowDoubleClick,
                              ImVec2(ImGui::GetContentRegionAvail().x - theme::px(96.f), 0.f));
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                editor.renamingPalette = info.id;
                std::snprintf(editor.paletteNameBuffer, sizeof(editor.paletteNameBuffer),
                              "%s", info.name.c_str());
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%zu colour(s). Double-click to rename.", info.colours);
            }
            ImGui::SameLine();
            if (ImGui::SmallButton(tr("copy"))) {
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
            if (ImGui::SmallButton(tr("x"))) {
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

    if (ImGui::Button(tr("New palette"), ImVec2(-1.f, 0.f))) {
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
        ImGui::SetTooltip("%s", tr("A copy of the current palette, to change some slots "
                          "of. Every layer keeps its slots, so the copy is a "
                          "recolour waiting to happen."));
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
        if (ImGui::Selectable(tr("the document's"), !own.valid())) {
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
        ImGui::SetTooltip("%s", tr("A palette for this frame alone. It sits out the "
                          "swap above -- a flash frame -- and one per frame "
                          "is colour cycling."));
    }
}

} // namespace

// The colours painted with lately that the palette lacks: a colour is kept
// once it has settled (no button down, so a slider being dragged is one
// colour and not a hundred) and only when it is not a slot, which is already
// one click away. Click one to paint with it, right-click for the right button.
static void drawRecentColours(Editor& editor) {
    const ls::Color painting = toColor(editor.color);
    if (editor.recentSeen.a == 0) {
        editor.recentSeen = painting;      // the colour it started with was not chosen
    }
    if (!(painting == editor.recentSeen) && !ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
        !ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
        editor.recentSeen = painting;
        bool inPalette = false;
        for (const PaletteEntry& entry :
             paletteEntries(editor.doc, paletteFor(editor.doc, editor.sprite))) {
            inPalette = inPalette || entry.color == painting;
        }
        if (editor.inkRole == ls::kColorRoleNone && !inPalette) {
            editor.recentColours.add(painting);
            editor.prefs.recentColours = encodeRecentColours(editor.recentColours.entries());
            if (editor.settingsLoaded) {
                saveSettings(editor);
            }
        }
    }
    if (editor.recentColours.entries().empty()) {
        return;
    }
    theme::sectionHeader("RECENT");
    const float gap = theme::px(2.f);
    const float swatch = std::round(ImGui::GetFrameHeight() * 0.7f);
    const float width = ImGui::GetContentRegionAvail().x;
    const int perRow = std::max(1, static_cast<int>((width + gap) / (swatch + gap)));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(gap, gap));
    int column = 0;
    for (size_t i = 0; i < editor.recentColours.entries().size(); ++i) {
        const ls::Color colour = editor.recentColours.entries()[i];
        if (column > 0) {
            ImGui::SameLine();
        }
        ImGui::PushID(static_cast<int>(2000 + i));
        const bool current = colour == painting && editor.inkRole == ls::kColorRoleNone;
        if (current) {
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 2.f);
            ImGui::PushStyleColor(ImGuiCol_Border, theme::palette().accent);
        }
        if (ImGui::ColorButton("##recent",
                               ImVec4(colour.r / 255.f, colour.g / 255.f, colour.b / 255.f,
                                      colour.a / 255.f),
                               ImGuiColorEditFlags_NoTooltip, ImVec2(swatch, swatch))) {
            Ink ink;
            ink.colour = colour;
            setForegroundInk(editor, ink);
        }
        nameForScripts("##recent");
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
            Ink ink;
            ink.colour = colour;
            setBackgroundInk(editor, ink);
        }
        if (current) {
            ImGui::PopStyleColor();
            ImGui::PopStyleVar();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("#%02X%02X%02X -- click to paint with it, right-click "
                              "for the right button", colour.r, colour.g, colour.b);
        }
        ImGui::PopID();
        column = (column + 1) % perRow;
    }
    ImGui::PopStyleVar();
    ImGui::Dummy(ImVec2(0.f, 4.f));
}

void drawPalettePanel(Editor& editor, CanvasView& canvas, SDL_Window* window) {
    PaintLayer* layer = editor.active();
    const std::vector<PaletteInfo> palettes = listPalettes(editor.doc);
    const ls::PaletteId documents = documentPalette(editor.doc);
    // The palette this frame draws with is the one the swatches edit.
    const ls::PaletteId shown = paletteFor(editor.doc, editor.activeSprite());
    const std::vector<PaletteEntry> entries = paletteEntries(editor.doc, shown);

    if (palettes.empty() || !shown.valid()) {
        ImGui::TextDisabled("%s", tr("No palette."));
        return;
    }

    drawPaletteQuickRow(editor, canvas, palettes, documents, shown);
    ImGui::Dummy(ImVec2(0.f, 4.f));

    // The ringed slot is the one the left button paints through.
    const ls::ColorRole current = editor.inkRole;
    (void)layer;

    // A swatch is a little under a control tall, and the gap is the panel's
    // own spacing, so a palette follows the font, the density and the
    // interface size like everything else.
    const float swatchSize = std::round(ImGui::GetFrameHeight() * 0.95f);
    const float spacing = std::max(theme::px(4.f), theme::metrics().itemSpacing - theme::px(1.f));
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
            ImGui::SetNextItemWidth(theme::px(150.f));
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
                ImGui::TextWrapped("%s", tr("Layers use this slot. They will keep the "
                                   "colour they show now and stop following "
                                   "the palette."));
                ImGui::PopStyleColor();
                if (ImGui::Button(tr("Remove anyway"))) {
                    editor.doc.beginAction("Remove palette slot");
                    removePaletteEntry(editor.doc, shown, entry.role);
                    editor.doc.endAction();
                    editor.confirmRemoveSlot = ls::kColorRoleNone;
                    canvas.invalidate();
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ImGui::Button(tr("Keep it"))) {
                    editor.confirmRemoveSlot = ls::kColorRoleNone;
                }
            } else if (ImGui::Button(tr("Remove slot"))) {
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
                ImGui::SetTooltip("%s", tr("Something paints through this slot; you will "
                                  "be asked first."));
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

    // Shades of the current colour, hue-shifted the way pixel art ramps are:
    // click one to paint with it, or keep the lot as slots.
    ImGui::Dummy(ImVec2(0.f, 2.f));
    theme::sectionHeader("SHADES");
    {
        const ls::Color painting = toColor(editor.color);
        const auto sameColour = [](ls::Color a, ls::Color b) {
            return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
        };
        if (!sameColour(painting, editor.shadePicked)) {
            editor.shadeBase = painting;
        }
        const std::vector<ls::Color> shades = shadesOf(editor.shadeBase, 3);
        const float gap = theme::px(2.f);
        const float width = ImGui::GetContentRegionAvail().x;
        const float swatch = std::floor((width - gap * static_cast<float>(shades.size() - 1)) /
                                        static_cast<float>(shades.size()));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(gap, gap));
        for (size_t i = 0; i < shades.size(); ++i) {
            const ls::Color& shade = shades[i];
            ImGui::PushID(static_cast<int>(1000 + i));
            if (i > 0) {
                ImGui::SameLine();
            }
            const ImVec4 colour(shade.r / 255.f, shade.g / 255.f, shade.b / 255.f,
                                shade.a / 255.f);
            const bool isCurrent = sameColour(shade, painting);
            if (isCurrent) {
                ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 2.f);
                ImGui::PushStyleColor(ImGuiCol_Border, theme::palette().accent);
            }
            if (ImGui::ColorButton("##shade", colour, ImGuiColorEditFlags_NoTooltip,
                                   ImVec2(swatch, theme::px(14.f)))) {
                Ink ink;
                ink.colour = shade;
                // A shade the palette already has paints through its slot.
                for (const PaletteEntry& entry : entries) {
                    if (sameColour(entry.color, shade)) {
                        ink.role = entry.role;
                        break;
                    }
                }
                setForegroundInk(editor, ink);
                editor.shadePicked = shade;
            }
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
                Ink ink;
                ink.colour = shade;
                setBackgroundInk(editor, ink);
            }
            if (isCurrent) {
                ImGui::PopStyleColor();
                ImGui::PopStyleVar();
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("#%02X%02X%02X -- click to paint with it, right-click "
                                  "for the right button", shade.r, shade.g, shade.b);
            }
            ImGui::PopID();
        }
        ImGui::PopStyleVar();
        if (ImGui::SmallButton(tr("Keep these shades as slots"))) {
            editor.doc.beginAction("Add shades");
            int added = 0;
            for (const ls::Color& shade : shades) {
                bool have = false;
                for (const PaletteEntry& entry : paletteEntries(editor.doc, shown)) {
                    have = have || sameColour(entry.color, shade);
                }
                if (!have && addPaletteEntry(editor.doc, editor.sprite, shade) !=
                                 ls::kColorRoleNone) {
                    ++added;
                }
            }
            editor.doc.endAction();
            editor.say(added == 0 ? "The palette has every one of these already"
                                  : "Added " + std::to_string(added) + " shade(s) to the palette");
        }
    }
    ImGui::Dummy(ImVec2(0.f, 4.f));

    drawRecentColours(editor);

    ImGui::Dummy(ImVec2(0.f, 2.f));
    theme::sectionHeader("PALETTE");
    if (ImGui::Button(tr("Add current colour"), ImVec2(-1.f, 0.f))) {
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
    const char* const fileLabels[] = { "Load...", "Save..." };
    const widgets::ButtonRow files = widgets::buttonRow(fileLabels, 2);
    if (ImGui::Button(tr("Load..."), ImVec2(files.width, 0.f))) {
        showImportPaletteDialog(editor.files, window, editor.doc);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", tr(".gpl from GIMP or Aseprite, or .hex from Lospec.\n"
                          "Replaces the palette this frame uses; layers drawn "
                          "through slots recolour."));
    }
    widgets::beside(files, 1);
    if (ImGui::Button(tr("Save..."), ImVec2(files.width, 0.f))) {
        showExportPaletteDialog(editor.files, window, editor.doc);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", tr(".gpl keeps the slot names; .hex, .pal and .act are just "
                          "the colours."));
    }

    // Arranging and building: sort, a ramp between the two colours, an
    // adjustment of the whole palette, and the presets that ship with Fast.
    const char* const toolLabels[] = { "Sort", "Ramp", "Adjust", "Presets" };
    const widgets::ButtonRow tools = widgets::buttonRow(toolLabels, 4);
    const float quarter = tools.width;
    if (ImGui::Button(tr("Sort"), ImVec2(quarter, 0.f))) {
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
    widgets::beside(tools, 1);
    const bool rampable = editor.inkRole != ls::kColorRoleNone &&
                          editor.backRole != ls::kColorRoleNone &&
                          editor.inkRole != editor.backRole;
    ImGui::BeginDisabled(!rampable);
    if (ImGui::Button(tr("Ramp"), ImVec2(quarter, 0.f))) {
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
        ImGui::SetNextItemWidth(theme::px(120.f));
        widgets::sliderInt(tr("steps"), &steps, 1, 16);
        if (ImGui::Button(tr("Add the ramp"))) {
            editor.doc.beginAction("Palette ramp");
            const std::vector<ls::ColorRole> made =
                addRampBetween(editor.doc, shown, editor.inkRole, editor.backRole, steps);
            editor.doc.endAction();
            editor.say("Added " + std::to_string(made.size()) + " slot(s) between them");
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    widgets::beside(tools, 2);
    if (ImGui::Button(tr("Adjust"), ImVec2(quarter, 0.f))) {
        editor.paletteAdjust = Editor::PaletteAdjust{};
        editor.paletteAdjust.base = entries;
        ImGui::OpenPopup("adjust-palette");
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", tr("Turn the hue, and push saturation and lightness, of every "
                          "slot at once -- a recolour of everything painted through "
                          "them, from the drawing."));
    }
    if (ImGui::BeginPopup("adjust-palette")) {
        Editor::PaletteAdjust& adjust = editor.paletteAdjust;
        bool changed = false;
        ImGui::SetNextItemWidth(theme::px(200.f));
        changed |= widgets::sliderFloat(tr("hue"), &adjust.hue, -180.f, 180.f, "%.0f deg");
        bracketDrag(editor, editor.draggingPalette, "Adjust palette");
        ImGui::SetNextItemWidth(theme::px(200.f));
        changed |= widgets::sliderFloat(tr("saturation"), &adjust.saturation, -1.f, 1.f, "%.2f");
        bracketDrag(editor, editor.draggingPalette, "Adjust palette");
        ImGui::SetNextItemWidth(theme::px(200.f));
        changed |= widgets::sliderFloat(tr("lightness"), &adjust.lightness, -1.f, 1.f, "%.2f");
        bracketDrag(editor, editor.draggingPalette, "Adjust palette");
        if (changed) {
            adjustPalette(editor.doc, shown, adjust.base, adjust.hue, adjust.saturation,
                          adjust.lightness);
            refreshInks(editor);
            canvas.invalidate();
        }
        ImGui::TextDisabled("%s", tr("From the palette as it was when this opened."));
        ImGui::EndPopup();
    }
    widgets::beside(tools, 3);
    if (ImGui::Button(tr("Presets"), ImVec2(quarter, 0.f))) {
        ImGui::OpenPopup("palette-presets");
    }
    if (ImGui::BeginPopup("palette-presets")) {
        ImGui::TextDisabled("%s", tr("Replaces this palette, as loading a file does."));
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

    if (ImGui::Button(tr("Make every colour a slot"), ImVec2(-1.f, 0.f))) {
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
        ImGui::SetTooltip("%s", tr("Every colour painted as a value of its own, on every "
                          "frame, becomes a palette slot -- one that already has "
                          "the colour, or a new one. After this the palette "
                          "recolours the whole sprite."));
    }

    if (current != ls::kColorRoleNone) {
        ImGui::PushStyleColor(ImGuiCol_Text, theme::palette().textDim);
        ImGui::TextWrapped("%s", tr("Editing a swatch changes every layer that uses it, "
                           "from the drawing rather than over it."));
        ImGui::PopStyleColor();
    }

    ImGui::Dummy(ImVec2(0.f, 6.f));
    drawPaletteList(editor, canvas, palettes, documents);
}


} // namespace fast
