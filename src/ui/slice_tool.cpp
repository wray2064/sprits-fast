// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

#include "ui/slice_tool.h"

#include "app/grid_snap.h"
#include "app/slices.h"
#include "ui/theme.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace fast {

namespace {

constexpr float kGrab = 7.f;

// A pixel boundary under the pointer: on the grid when snapping, the nearest
// line between pixels otherwise.
ls::Vec2i boundaryHere(const Editor& editor, const CanvasView& canvas) {
    const ls::Vec2f at = canvas.pointerExact();
    if (editor.snapToGrid) {
        return nearestGridPoint(at, editor.snapGrid);
    }
    return { static_cast<int32_t>(std::lround(at.x)), static_cast<int32_t>(std::lround(at.y)) };
}

bool near(ls::Vec2f a, ls::Vec2i b, float zoom) {
    const float dx = (a.x - static_cast<float>(b.x)) * zoom;
    const float dy = (a.y - static_cast<float>(b.y)) * zoom;
    return dx * dx + dy * dy <= kGrab * kGrab;
}

// The four corners, top-left first, clockwise.
ls::Vec2i corner(const ls::Rect2i& r, int i) {
    switch (i) {
        case 0: return r.min;
        case 1: return { r.max.x, r.min.y };
        case 2: return r.max;
        default: return { r.min.x, r.max.y };
    }
}

ls::Rect2i ordered(ls::Vec2i a, ls::Vec2i b) {
    return { { std::min(a.x, b.x), std::min(a.y, b.y) }, { std::max(a.x, b.x), std::max(a.y, b.y) } };
}

} // namespace

bool handleSliceInput(Editor& editor, CanvasView& canvas, bool overCanvas) {
    if (editor.tool != Tool::Slice) {
        return false;
    }
    std::vector<Slice> slices = readSlices(editor.doc);
    const float zoom = canvas.zoom();

    if (editor.draggingSlice) {
        const int index = editor.activeSlice;
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left) || index < 0 ||
            index >= static_cast<int>(slices.size())) {
            editor.draggingSlice = false;
            editor.doc.endAction();
            return true;
        }
        const ls::Vec2i here = boundaryHere(editor, canvas);
        Slice& s = slices[static_cast<size_t>(index)];
        const ls::Rect2i start = editor.sliceStart;
        if (editor.sliceHandle >= 0 && editor.sliceHandle < 4) {
            // The corner dragged goes to the pointer; the opposite one stays.
            const ls::Vec2i fixed = corner(start, (editor.sliceHandle + 2) % 4);
            ls::Rect2i box = ordered(fixed, here);
            if (box.width() < 1) { box.max.x = box.min.x + 1; }
            if (box.height() < 1) { box.max.y = box.min.y + 1; }
            s.bounds = box;
        } else {
            const ls::Vec2i by { here.x - editor.sliceGrab.x, here.y - editor.sliceGrab.y };
            s.bounds = { { start.min.x + by.x, start.min.y + by.y },
                         { start.max.x + by.x, start.max.y + by.y } };
        }
        // A centre is kept inside what it centres.
        if (s.nine) {
            s.centre.max.x = std::min(s.centre.max.x, s.bounds.width());
            s.centre.max.y = std::min(s.centre.max.y, s.bounds.height());
            s.centre.min.x = std::min(s.centre.min.x, s.centre.max.x - 1);
            s.centre.min.y = std::min(s.centre.min.y, s.centre.max.y - 1);
            s.nine = !s.centre.empty() && s.centre.min.x >= 0 && s.centre.min.y >= 0;
        }
        writeSlices(editor.doc, slices);
        return true;
    }

    if (!overCanvas || !ImGui::IsMouseClicked(ImGuiMouseButton_Left) ||
        ImGui::IsKeyDown(ImGuiKey_Space)) {
        return false;
    }
    const ls::Vec2f at = canvas.pointerExact();
    const ls::Vec2i here = boundaryHere(editor, canvas);

    // A corner of the slice being edited, then the inside of any slice (the
    // last made on top), then a new one.
    int handle = -1;
    int index = -1;
    if (editor.activeSlice >= 0 && editor.activeSlice < static_cast<int>(slices.size())) {
        const ls::Rect2i& r = slices[static_cast<size_t>(editor.activeSlice)].bounds;
        for (int i = 0; i < 4; ++i) {
            if (near(at, corner(r, i), zoom)) {
                handle = i;
                index = editor.activeSlice;
                break;
            }
        }
    }
    if (index < 0) {
        for (int i = static_cast<int>(slices.size()) - 1; i >= 0; --i) {
            const ls::Rect2i& r = slices[static_cast<size_t>(i)].bounds;
            if (at.x >= static_cast<float>(r.min.x) && at.x < static_cast<float>(r.max.x) &&
                at.y >= static_cast<float>(r.min.y) && at.y < static_cast<float>(r.max.y)) {
                index = i;
                handle = 4;
                break;
            }
        }
    }
    editor.doc.beginAction(index >= 0 ? "Edit slice" : "Add slice");
    if (index < 0) {
        Slice made;
        made.name = freeSliceName(slices);
        made.bounds = { here, { here.x + 1, here.y + 1 } };
        slices.push_back(made);
        writeSlices(editor.doc, slices);
        index = static_cast<int>(slices.size()) - 1;
        handle = 2;             // drag out its far corner
        editor.say("Drag out " + made.name + "; name it in the slices window");
    }
    editor.activeSlice = index;
    editor.sliceHandle = handle;
    editor.sliceGrab = here;
    editor.sliceStart = slices[static_cast<size_t>(index)].bounds;
    editor.draggingSlice = true;
    return true;
}

bool handleSliceKeys(Editor& editor) {
    if (editor.tool != Tool::Slice || editor.activeSlice < 0) {
        return false;
    }
    if (!ImGui::IsKeyPressed(ImGuiKey_Delete, false) &&
        !ImGui::IsKeyPressed(ImGuiKey_Backspace, false)) {
        return false;
    }
    std::vector<Slice> slices = readSlices(editor.doc);
    if (editor.activeSlice >= static_cast<int>(slices.size())) {
        editor.activeSlice = -1;
        return false;
    }
    const std::string name = slices[static_cast<size_t>(editor.activeSlice)].name;
    slices.erase(slices.begin() + editor.activeSlice);
    editor.doc.beginAction("Delete slice");
    writeSlices(editor.doc, slices);
    editor.doc.endAction();
    editor.activeSlice = -1;
    editor.say("Deleted " + name);
    return true;
}

void drawSliceOverlay(Editor& editor, const CanvasView& canvas, ImDrawList* draw, ImVec2 origin,
                      float zoom) {
    const std::vector<Slice> slices = readSlices(editor.doc);
    if (slices.empty()) {
        return;
    }
    const bool editing = editor.tool == Tool::Slice || editor.slicesOpen;
    const auto screen = [&](ls::Vec2i p) {
        return ImVec2(origin.x + static_cast<float>(p.x) * zoom,
                      origin.y + static_cast<float>(p.y) * zoom);
    };
    for (size_t i = 0; i < slices.size(); ++i) {
        const Slice& s = slices[i];
        const bool active = static_cast<int>(i) == editor.activeSlice;
        const ImU32 colour = IM_COL32(s.colour.r, s.colour.g, s.colour.b,
                                      editing ? (active ? 255 : 170) : 70);
        const ImVec2 a = screen(s.bounds.min);
        const ImVec2 b = screen(s.bounds.max);
        draw->AddRect(a, b, colour, 0.f, 0, active ? 2.f : 1.f);
        if (!editing) {
            continue;
        }
        draw->AddText(ImVec2(a.x + 3.f, a.y + 2.f), colour, s.name.c_str());
        if (s.nine) {
            const ImU32 thin = IM_COL32(s.colour.r, s.colour.g, s.colour.b, 140);
            const ls::Vec2i c0 { s.bounds.min.x + s.centre.min.x, s.bounds.min.y + s.centre.min.y };
            const ls::Vec2i c1 { s.bounds.min.x + s.centre.max.x, s.bounds.min.y + s.centre.max.y };
            const ImVec2 p0 = screen(c0);
            const ImVec2 p1 = screen(c1);
            draw->AddLine(ImVec2(p0.x, a.y), ImVec2(p0.x, b.y), thin);
            draw->AddLine(ImVec2(p1.x, a.y), ImVec2(p1.x, b.y), thin);
            draw->AddLine(ImVec2(a.x, p0.y), ImVec2(b.x, p0.y), thin);
            draw->AddLine(ImVec2(a.x, p1.y), ImVec2(b.x, p1.y), thin);
        }
        if (s.hasPivot) {
            const ImVec2 p = screen({ s.bounds.min.x + s.pivot.x, s.bounds.min.y + s.pivot.y });
            draw->AddCircle(p, 4.f, colour, 12, 1.5f);
        }
        if (active && editor.tool == Tool::Slice) {
            for (int c = 0; c < 4; ++c) {
                const ImVec2 at = screen(corner(s.bounds, c));
                draw->AddRectFilled(ImVec2(at.x - 4.f, at.y - 4.f), ImVec2(at.x + 4.f, at.y + 4.f),
                                    IM_COL32(0, 0, 0, 200));
                draw->AddRectFilled(ImVec2(at.x - 3.f, at.y - 3.f), ImVec2(at.x + 3.f, at.y + 3.f),
                                    colour);
            }
        }
    }
    (void)canvas;
}

void drawSlicesPanel(Editor& editor, CanvasView& canvas) {
    if (!editor.slicesOpen) {
        return;
    }
    ImGui::SetNextWindowSize(ImVec2(340.f, 420.f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Slices", &editor.slicesOpen)) {
        ImGui::End();
        return;
    }
    std::vector<Slice> slices = readSlices(editor.doc);
    if (slices.empty()) {
        ImGui::TextWrapped("No slices. Take the slice tool (C) and drag out a rectangle: a "
                           "button, a hitbox, a panel to stretch by its middle. They go "
                           "out with the sheet's description, Aseprite's layout included.");
        ImGui::End();
        return;
    }
    if (editor.activeSlice >= static_cast<int>(slices.size())) {
        editor.activeSlice = -1;
    }
    ImGui::BeginChild("list", ImVec2(0.f, 120.f), true);
    for (size_t i = 0; i < slices.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        const Slice& s = slices[i];
        char label[128];
        std::snprintf(label, sizeof(label), "%s   %d,%d  %dx%d", s.name.c_str(), s.bounds.min.x,
                      s.bounds.min.y, s.bounds.width(), s.bounds.height());
        if (ImGui::Selectable(label, editor.activeSlice == static_cast<int>(i))) {
            editor.activeSlice = static_cast<int>(i);
            editor.sliceNameLoaded = -1;
        }
        ImGui::PopID();
    }
    ImGui::EndChild();

    if (editor.activeSlice >= 0) {
        Slice s = slices[static_cast<size_t>(editor.activeSlice)];
        const Slice before = s;
        if (editor.sliceNameLoaded != editor.activeSlice) {
            std::snprintf(editor.sliceName, sizeof(editor.sliceName), "%s", s.name.c_str());
            editor.sliceNameLoaded = editor.activeSlice;
        }
        bool changed = false;
        // A drag is one undo step: the action opens as a field is taken and
        // closes as it is let go.
        const auto bracket = [&editor]() {
            if (ImGui::IsItemActivated() && !editor.editingSlice) {
                editor.doc.beginAction("Edit slice");
                editor.editingSlice = true;
            }
            if (editor.editingSlice && ImGui::IsItemDeactivated()) {
                editor.doc.endAction();
                editor.editingSlice = false;
            }
        };
        ImGui::SetNextItemWidth(-60.f);
        ImGui::InputText("name", editor.sliceName, sizeof(editor.sliceName));
        if (ImGui::IsItemDeactivatedAfterEdit() && editor.sliceName[0] != '\0') {
            s.name = editor.sliceName;
            changed = true;
        }
        int bounds[4] = { s.bounds.min.x, s.bounds.min.y, s.bounds.width(), s.bounds.height() };
        ImGui::SetNextItemWidth(-60.f);
        if (ImGui::DragInt4("x y w h", bounds, 0.2f)) {
            bounds[2] = std::max(1, bounds[2]);
            bounds[3] = std::max(1, bounds[3]);
            s.bounds = { { bounds[0], bounds[1] }, { bounds[0] + bounds[2], bounds[1] + bounds[3] } };
            changed = true;
        }
        bracket();
        if (ImGui::Checkbox("Nine-slice centre", &s.nine)) {
            if (s.nine && s.centre.empty()) {
                s.centre = { { s.bounds.width() / 4, s.bounds.height() / 4 },
                             { s.bounds.width() - s.bounds.width() / 4,
                               s.bounds.height() - s.bounds.height() / 4 } };
                if (s.centre.empty()) {
                    s.nine = false;
                }
            }
            changed = true;
        }
        if (s.nine) {
            int centre[4] = { s.centre.min.x, s.centre.min.y, s.centre.width(), s.centre.height() };
            ImGui::SetNextItemWidth(-60.f);
            if (ImGui::DragInt4("centre", centre, 0.2f, 0, std::max(s.bounds.width(), s.bounds.height()))) {
                centre[2] = std::clamp(centre[2], 1, s.bounds.width());
                centre[3] = std::clamp(centre[3], 1, s.bounds.height());
                centre[0] = std::clamp(centre[0], 0, s.bounds.width() - centre[2]);
                centre[1] = std::clamp(centre[1], 0, s.bounds.height() - centre[3]);
                s.centre = { { centre[0], centre[1] }, { centre[0] + centre[2], centre[1] + centre[3] } };
                changed = true;
            }
            bracket();
        }
        if (ImGui::Checkbox("Pivot", &s.hasPivot)) {
            if (s.hasPivot) {
                s.pivot = { s.bounds.width() / 2, s.bounds.height() };
            }
            changed = true;
        }
        if (s.hasPivot) {
            int pivot[2] = { s.pivot.x, s.pivot.y };
            ImGui::SetNextItemWidth(-60.f);
            if (ImGui::DragInt2("pivot", pivot, 0.2f)) {
                s.pivot = { pivot[0], pivot[1] };
                changed = true;
            }
            bracket();
        }
        float rgb[3] = { s.colour.r / 255.f, s.colour.g / 255.f, s.colour.b / 255.f };
        if (ImGui::ColorEdit3("colour##slice", rgb, ImGuiColorEditFlags_NoInputs)) {
            s.colour = { static_cast<uint8_t>(rgb[0] * 255.f + 0.5f),
                         static_cast<uint8_t>(rgb[1] * 255.f + 0.5f),
                         static_cast<uint8_t>(rgb[2] * 255.f + 0.5f), 255 };
            changed = true;
        }
        bracket();
        ImGui::SameLine();
        if (ImGui::Button("Delete")) {
            slices.erase(slices.begin() + editor.activeSlice);
            editor.doc.beginAction("Delete slice");
            writeSlices(editor.doc, slices);
            editor.doc.endAction();
            editor.activeSlice = -1;
            ImGui::End();
            return;
        }
        (void)before;
        if (changed) {
            slices[static_cast<size_t>(editor.activeSlice)] = s;
            if (editor.editingSlice) {
                writeSlices(editor.doc, slices);            // inside the drag's action
            } else {
                editor.doc.beginAction("Edit slice");
                writeSlices(editor.doc, slices);
                editor.doc.endAction();
            }
            canvas.invalidate();
        }
    }
    ImGui::End();
}

} // namespace fast
