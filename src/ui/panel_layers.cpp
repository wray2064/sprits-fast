// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

// panel_layers.cpp -- the layer stack and one layer's properties.
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

// ----------------------------------------------------------------- layers --

namespace {

// A layer's picture at thumbnail size over the chequer, the frame's shape
// kept: a wide canvas gives a wide thumbnail, not a squashed one. Hidden
// layers draw faint so the eye can find them without the checkbox.
void drawLayerThumbnail(Editor& editor, CanvasView& canvas, ls::SpriteId sprite,
                        ls::LayerId layer, bool visible) {
    const float kBox = theme::px(22.f);
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
    // The tiles' are kept: the Element panel shows them for any frame.
    {
        std::vector<ls::LayerId> keep = order;
        for (ls::SpriteId tileset : tilesetsOf(editor.doc)) {
            for (ls::LayerId tile : layerOrder(editor.doc, tileset)) {
                keep.push_back(tile);
            }
        }
        canvas.frames().retainOnlyLayers(keep);
    }

    // --- the buttons ---------------------------------------------------------
    //
    // As wide as their words need (a longer translation, a larger interface),
    // and never narrower than they are at 100%; a row too narrow for all five
    // wraps instead of cutting a word.
    const float addWidth = std::max(theme::px(52.f), widgets::buttonWidth("Add"));
    const float dupWidth = std::max(theme::px(46.f), widgets::buttonWidth("Dup"));
    const float deleteWidth = std::max(theme::px(56.f), widgets::buttonWidth("Delete"));
    const float arrowWidth = theme::px(22.f);
    if (ImGui::Button(tr("Add"), ImVec2(addWidth, 0.f))) {
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
    widgets::sameLineIfFits(dupWidth);
    if (ImGui::Button(tr("Dup"), ImVec2(dupWidth, 0.f))) {
        duplicateActiveLayer(editor, canvas);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", tr("Duplicate the active layer  (Ctrl+J)"));
    }
    widgets::sameLineIfFits(deleteWidth);
    const bool canDelete = order.size() > 1;
    ImGui::BeginDisabled(!canDelete);
    if (ImGui::Button(tr("Delete"), ImVec2(deleteWidth, 0.f))) {
        deleteSelectedLayers(editor, canvas);
    }
    ImGui::EndDisabled();
    if (!canDelete && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("%s", tr("A sprite needs at least one layer."));
    }
    widgets::sameLineIfFits(arrowWidth);
    if (ImGui::Button("^", ImVec2(arrowWidth, 0.f))) { raiseActiveLayer(editor, canvas); }
    if (ImGui::IsItemHovered()) { ImGui::SetTooltip("%s", tr("Move up  (Ctrl+])")); }
    widgets::sameLineIfFits(arrowWidth);
    if (ImGui::Button(tr("v"), ImVec2(arrowWidth, 0.f))) { lowerActiveLayer(editor, canvas); }
    if (ImGui::IsItemHovered()) { ImGui::SetTooltip("%s", tr("Move down  (Ctrl+[)")); }

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
            ImGui::SetNextItemWidth(theme::px(104.f));
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
            // Beside the blend mode while the slider keeps room to say its value,
            // under it when a narrow panel would squeeze the words out of it.
            widgets::sameLineIfFits(ImGui::CalcTextSize("opacity 1.00").x +
                                    ImGui::GetStyle().FramePadding.x * 4.f);
            ImGui::SetNextItemWidth(-1.f);
            if (widgets::sliderFloat("##opacity", &opacity, 0.f, 1.f, "opacity %.2f")) {
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
                if (ImGui::Checkbox(tr("clip to below"), &clipped)) {
                    toggleActiveLayerClip(editor, canvas);
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s", tr("Draw only where the layer below draws: a "
                                      "highlight that stays inside the body. A move "
                                      "clears it, because \"below\" changed."));
                }
                // Beside it when it fits, under it when it does not.
                widgets::sameLineIfFits(ImGui::GetFrameHeight() +
                                        ImGui::GetStyle().ItemInnerSpacing.x +
                                        ImGui::CalcTextSize(tr("lock")).x);
                bool locked = layerProps.locked;
                if (ImGui::Checkbox(tr("lock"), &locked)) {
                    toggleActiveLayerLock(editor);
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s", tr("Tools leave a locked layer alone."));
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
                if (theme::eyeToggle("##gvisible", group.visible, theme::px(18.f))) {
                    editor.doc.beginAction(group.visible ? "Hide group" : "Show group");
                    setGroupVisible(editor.doc, openGroup, !group.visible);
                    editor.doc.endAction();
                    canvas.invalidate();
                }
                ImGui::SameLine();
                if (editor.renamingGroup == openGroup) {
                    ImGui::SetNextItemWidth(-1.f);
                    if (editor.renameFocus) {
                        ImGui::SetKeyboardFocusHere();
                        editor.renameFocus = false;
                    }
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
                        editor.renameFocus = true;
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
                        if (ImGui::MenuItem(tr("Ungroup"), "Ctrl+Shift+G")) {
                            editor.activeGroup = openGroup;
                            ungroupActiveLayer(editor, canvas);
                        }
                        if (ImGui::MenuItem(tr("Rename"))) {
                            editor.renameFocus = true;
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
            ImGui::Indent(theme::px(18.f));
        }
        const bool visible = props.visible;
        if (theme::eyeToggle("##visible", visible, theme::px(18.f))) {
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
            if (editor.renameFocus) {
                ImGui::SetKeyboardFocusHere();
                editor.renameFocus = false;
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
            // The tag, a stripe down the row's edge, so a colour-coded stack
            // reads the way it was coded. Always the room for it, so tagged
            // and untagged rows line up.
            {
                const ImVec2 at = ImGui::GetCursorScreenPos();
                if (props.tagged) {
                    ImGui::GetWindowDrawList()->AddRectFilled(
                        at, ImVec2(at.x + theme::px(3.f), at.y + theme::px(22.f)),
                        IM_COL32(props.tag.r, props.tag.g, props.tag.b, 255));
                }
                ImGui::Dummy(ImVec2(theme::px(3.f), theme::px(22.f)));
                if (props.tagged && !props.notes.empty() && ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s", props.notes.c_str());
                }
                ImGui::SameLine(0.f, theme::px(3.f));
            }
            drawLayerThumbnail(editor, canvas, sprite, id, props.visible);
            ImGui::SameLine();

            std::string label = props.name;
            if (props.locked) { label += "  [lock]"; }
            if (props.reference) { label += "  [ref]"; }
            if (!linkOf(editor.doc, id).empty()) { label += "  [linked]"; }
            if (props.clipBase.valid()) { label += "  [clip]"; }
            if (props.blend != ls::BlendMode::Normal || props.opacity < 1.f) {
                char detail[48];
                std::snprintf(detail, sizeof(detail), "  %s %d%%",
                              props.blend != ls::BlendMode::Normal
                                  ? blendModeNames()[static_cast<int>(props.blend)] : "",
                              static_cast<int>(props.opacity * 100.f + 0.5f));
                label += detail;
            }
            if (const float cel = celOpacity(editor.doc, id); cel < 0.999f) {
                label += "  cel " + std::to_string(static_cast<int>(cel * 100.f + 0.5f)) + "%";
            }
            if (!props.notes.empty()) { label += "  *"; }
            if (!drawable) { label += "  (not editable here)"; }

            const bool selected = drawable && !editor.activeGroup.valid() && layerSelected(editor, id);
            ImGui::BeginDisabled(!drawable);
            // As tall as the thumbnail, so the highlight covers the row.
            if (ImGui::Selectable(label.c_str(), selected, 0, ImVec2(0.f, theme::px(22.f)))) {
                selectLayer(editor, id, ImGui::GetIO().KeyCtrl);
            }
            ImGui::EndDisabled();
            if (drawable && ImGui::IsItemHovered() &&
                ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                editor.renaming = listIndex;
                editor.renameFocus = true;
                std::snprintf(editor.renameBuffer, sizeof(editor.renameBuffer),
                              "%s", props.name.c_str());
            }
            if (drawable && ImGui::BeginDragDropSource()) {
                const uint64_t handle = id.value;
                ImGui::SetDragDropPayload("layer", &handle, sizeof(handle));
                ImGui::TextUnformatted(props.name.c_str());
                ImGui::TextDisabled("%s", tr("Drop on a row to move there. On a group's row to join it.\n"
                                    "Hold Ctrl to make a group of the two."));
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
                if (ImGui::MenuItem(tr("Duplicate"), "Ctrl+J")) { duplicateActiveLayer(editor, canvas); }
                if (ImGui::MenuItem(tr("Merge down"), "Ctrl+E")) { mergeActiveLayerDown(editor, canvas); }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s", tr("This layer's elements join the layer below, on top "
                                      "of its own, each still what it was."));
                }
                if (ImGui::MenuItem(tr("Copy"), "Ctrl+C")) { copyActiveLayer(editor); }
                if (ImGui::MenuItem(tr("Paste above"), "Ctrl+V", false, editor.clipboard.valid())) {
                    pasteLayerHere(editor, canvas);
                }
                if (ImGui::MenuItem(tr("Delete"), nullptr, false, order.size() > 1)) {
                    deleteSelectedLayers(editor, canvas);
                }
                ImGui::Separator();
                if (ImGui::MenuItem(tr("Move up"), "Ctrl+]", false, i + 1 < static_cast<int>(order.size()))) {
                    raiseActiveLayer(editor, canvas);
                }
                if (ImGui::MenuItem(tr("Move down"), "Ctrl+[", false, i > 0)) {
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
                    if (ImGui::BeginMenu(tr("Add to group"), anyOther)) {
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
                if (ImGui::MenuItem(tr("Remove from group"), nullptr, false, props.group.valid())) {
                    if (removeFromGroup(editor.doc, id)) {
                        selectLayer(editor, id);
                        canvas.invalidate();
                        editor.say("Out of the group, still where it was");
                    }
                }
                if (ImGui::MenuItem(tr("Ungroup"), "Ctrl+Shift+G", false, props.group.valid())) {
                    ungroupActiveLayer(editor, canvas);
                }
                ImGui::Separator();
                if (ImGui::MenuItem(tr("Clip to layer below"), nullptr, props.clipBase.valid(), i > 0)) {
                    toggleActiveLayerClip(editor, canvas);
                }
                if (ImGui::MenuItem(tr("Lock"), nullptr, props.locked)) {
                    toggleActiveLayerLock(editor);
                }
                if (tracksOn(editor.doc) && editor.frames.size() > 1) {
                    int first = 0;
                    int last = 0;
                    const bool ranged = frameRange(editor, &first, &last);
                    const std::string linkLabel = ranged
                        ? "Link in frames " + std::to_string(first + 1) + " to " +
                              std::to_string(last + 1)
                        : std::string("Link in every frame");
                    if (ImGui::MenuItem(linkLabel.c_str())) {
                        std::vector<ls::SpriteId> into;
                        for (int f = ranged ? first : 0;
                             f <= (ranged ? last : static_cast<int>(editor.frames.size()) - 1); ++f) {
                            into.push_back(editor.frames[static_cast<size_t>(f)].sprite);
                        }
                        editor.doc.beginAction("Link cels");
                        const int linked = linkCels(editor.doc, id, into);
                        editor.doc.endAction();
                        canvas.invalidate();
                        editor.say(linked > 0
                            ? "Linked in " + std::to_string(linked + 1) +
                                  " frames: this frame's cel replaced theirs, and drawing on "
                                  "one now draws on all"
                            : std::string("Nothing to link"));
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("%s", tr("This layer in those frames becomes this frame's cel -- "
                                          "what they held is replaced. Select a run in the "
                                          "frame strip to choose which."));
                    }
                    if (!linkOf(editor.doc, id).empty() && ImGui::MenuItem(tr("Unlink"))) {
                        editor.doc.beginAction("Unlink cel");
                        const ls::LayerId own = unlinkCel(editor.doc, id);
                        editor.doc.endAction();
                        resyncLayers(editor);
                        selectLayer(editor, own);
                        canvas.invalidate();
                        editor.say("This frame's cel is its own again");
                    }
                }
                if (ImGui::MenuItem(tr("Reference layer"), nullptr, props.reference)) {
                    editor.doc.beginAction(props.reference ? "Not a reference" : "Reference layer");
                    setReferenceLayer(editor.doc, id, !props.reference);
                    editor.doc.endAction();
                    canvas.invalidate();
                    editor.say(props.reference ? "Exported again"
                                               : "A reference now: drawn on the canvas, left out of every export");
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s", tr("For a sketch or a guide to draw over: shown here, never "
                                      "in a PNG, a sheet, an animation or the outline round "
                                      "the figure."));
                }
                if (ImGui::MenuItem(tr("Rename"))) {
                    editor.renaming = listIndex;
                    editor.renameFocus = true;
                    std::snprintf(editor.renameBuffer, sizeof(editor.renameBuffer),
                                  "%s", props.name.c_str());
                }
                if (ImGui::MenuItem(tr("Properties..."))) {
                    editor.propertiesLayer = id;
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
        ImGui::TextWrapped("%s", tr("Drag to reorder, or onto a group to join it; "
                           "Ctrl+drop groups two. Right-click for the rest."));
        ImGui::PopStyleColor();
    }
}


// ------------------------------------------------------- layer properties --

namespace {

// Aseprite's tag colours are its own; these are the eight a stack needs to be
// told apart at a glance, muted enough to sit beside the artwork.
struct TagChoice { const char* name; ls::Color colour; };
const TagChoice kTagChoices[] = {
    { "Red",    ls::Color{ 214,  84,  68, 255 } },
    { "Orange", ls::Color{ 232, 146,  52, 255 } },
    { "Yellow", ls::Color{ 222, 200,  72, 255 } },
    { "Green",  ls::Color{  96, 176,  88, 255 } },
    { "Teal",   ls::Color{  64, 170, 170, 255 } },
    { "Blue",   ls::Color{  76, 128, 214, 255 } },
    { "Purple", ls::Color{ 150,  98, 200, 255 } },
    { "Grey",   ls::Color{ 128, 136, 146, 255 } },
};

} // namespace

void drawLayerPropertiesPanel(Editor& editor, CanvasView& canvas) {
    if (!editor.propertiesLayer.valid()) {
        editor.propertiesLoaded = ls::LayerId{};
        return;
    }
    const ls::LayerId id = editor.propertiesLayer;
    LayerProps props;
    if (!readLayerProps(editor.doc, id, &props)) {
        // Deleted, or gone with an undo: nothing to show.
        editor.propertiesLayer = ls::LayerId{};
        return;
    }
    if (editor.propertiesLoaded != id) {
        std::snprintf(editor.propertiesName, sizeof(editor.propertiesName), "%s",
                      props.name.c_str());
        std::snprintf(editor.propertiesNotes, sizeof(editor.propertiesNotes), "%s",
                      props.notes.c_str());
        editor.propertiesLoaded = id;
    }
    bool open = true;
    ImGui::SetNextWindowSize(ImVec2(theme::px(340.f), 0.f), ImGuiCond_Appearing);
    if (!ImGui::Begin("Layer properties", &open,
                      ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::End();
        return;
    }
    // The name: kept when the field is left or Enter pressed, one undo step.
    ImGui::SetNextItemWidth(-widgets::labelRoom(tr("name")));
    ImGui::InputText(tr("name"), editor.propertiesName, sizeof(editor.propertiesName));
    if (ImGui::IsItemDeactivatedAfterEdit() && props.name != editor.propertiesName &&
        editor.propertiesName[0] != '\0') {
        editor.doc.beginAction("Rename layer");
        renameLayer(editor.doc, id, editor.propertiesName);
        editor.doc.endAction();
    }

    int blend = static_cast<int>(props.blend);
    ImGui::SetNextItemWidth(-widgets::labelRoom(tr("blend")));
    if (ImGui::Combo(tr("blend"), &blend, blendModeNames().data(),
                     static_cast<int>(blendModeNames().size()))) {
        editor.doc.beginAction("Blend mode");
        setLayerBlend(editor.doc, id, static_cast<ls::BlendMode>(blend));
        editor.doc.endAction();
        canvas.invalidate();
    }
    float opacity = props.opacity;
    ImGui::SetNextItemWidth(-widgets::labelRoom(tr("opacity")));
    if (widgets::sliderFloat(tr("opacity"), &opacity, 0.f, 1.f, "%.2f")) {
        setLayerOpacity(editor.doc, id, opacity);
        canvas.invalidate();
    }
    bracketDrag(editor, editor.draggingLayerProperties, "Layer opacity");
    float cel = celOpacity(editor.doc, id);
    ImGui::SetNextItemWidth(-widgets::labelRoom(tr("cel opacity")));
    if (widgets::sliderFloat(tr("cel opacity"), &cel, 0.f, 1.f, "%.2f")) {
        setCelOpacity(editor.doc, id, cel);
        canvas.invalidate();
    }
    bracketDrag(editor, editor.draggingLayerProperties, "Cel opacity");
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", tr("This frame's drawing of the layer only, faded under the layer's "
                          "opacity -- which is the same in every frame."));
    }

    bool visible = props.visible;
    if (ImGui::Checkbox(tr("Visible"), &visible)) {
        editor.doc.beginAction(visible ? "Show layer" : "Hide layer");
        setLayerVisible(editor.doc, id, visible);
        editor.doc.endAction();
        canvas.invalidate();
    }
    ImGui::SameLine();
    bool locked = props.locked;
    if (ImGui::Checkbox(tr("Locked"), &locked)) {
        editor.doc.beginAction(locked ? "Lock layer" : "Unlock layer");
        setLayerLocked(editor.doc, id, locked);
        editor.doc.endAction();
    }
    bool reference = props.reference;
    if (ImGui::Checkbox(tr("Reference -- never exported"), &reference)) {
        editor.doc.beginAction(reference ? "Reference layer" : "Not a reference");
        setReferenceLayer(editor.doc, id, reference);
        editor.doc.endAction();
        canvas.invalidate();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", tr("Drawn on the canvas like any layer, left out of every export "
                          "and of the outline round the whole figure."));
    }

    theme::sectionHeader(tr("TAG"));
    // None, the eight, and any colour at all.
    {
        const bool none = !props.tagged;
        if (ImGui::RadioButton(tr("none"), none) && !none) {
            editor.doc.beginAction("Untag layer");
            setLayerTag(editor.doc, id, nullptr);
            editor.doc.endAction();
        }
        for (const TagChoice& choice : kTagChoices) {
            ImGui::SameLine();
            ImGui::PushID(choice.name);
            const ImVec4 colour(choice.colour.r / 255.f, choice.colour.g / 255.f,
                                choice.colour.b / 255.f, 1.f);
            const bool chosen = props.tagged && props.tag.r == choice.colour.r &&
                                props.tag.g == choice.colour.g && props.tag.b == choice.colour.b;
            if (chosen) {
                ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 2.f);
                ImGui::PushStyleColor(ImGuiCol_Border, theme::palette().textBright);
            }
            if (ImGui::ColorButton(choice.name, colour, ImGuiColorEditFlags_NoTooltip,
                                   ImVec2(18.f, 18.f))) {
                editor.doc.beginAction("Tag layer");
                setLayerTag(editor.doc, id, &choice.colour);
                editor.doc.endAction();
            }
            if (chosen) {
                ImGui::PopStyleColor();
                ImGui::PopStyleVar();
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", choice.name);
            }
            ImGui::PopID();
        }
        ImGui::SameLine();
        float custom[3] = { props.tag.r / 255.f, props.tag.g / 255.f, props.tag.b / 255.f };
        if (ImGui::ColorEdit3("##custom-tag", custom,
                              ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel)) {
            const ls::Color tag{ static_cast<uint8_t>(custom[0] * 255.f + 0.5f),
                                 static_cast<uint8_t>(custom[1] * 255.f + 0.5f),
                                 static_cast<uint8_t>(custom[2] * 255.f + 0.5f), 255 };
            editor.doc.beginAction("Tag layer");
            setLayerTag(editor.doc, id, &tag);
            editor.doc.endAction();
        }
    }

    theme::sectionHeader(tr("NOTES"));
    ImGui::InputTextMultiline("##notes", editor.propertiesNotes, sizeof(editor.propertiesNotes),
                              ImVec2(-1.f, 90.f));
    if (ImGui::IsItemDeactivatedAfterEdit() && props.notes != editor.propertiesNotes) {
        editor.doc.beginAction("Layer notes");
        setLayerNotes(editor.doc, id, editor.propertiesNotes);
        editor.doc.endAction();
    }
    ImGui::TextDisabled("%s", tr("Kept with the layer, shown when its tag is hovered."));
    ImGui::End();
    if (!open) {
        editor.propertiesLayer = ls::LayerId{};
    }
}


} // namespace fast
