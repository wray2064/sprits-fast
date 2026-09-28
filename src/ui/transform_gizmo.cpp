// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

#include "ui/transform_gizmo.h"

#include "app/layers.h"
#include "app/transform.h"
#include "ui/canvas_view.h"
#include "ui/editor.h"
#include "ui/theme.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace fast {

namespace {

// Screen sizes, so the gizmo is the same to the hand at any zoom.
constexpr float kRing = 64.f;         // the ring's radius
constexpr float kRingReach = 7.f;     // how near the ring a press takes it
constexpr float kArrow = 44.f;        // an arrow's length, to the tip
constexpr float kArrowReach = 7.f;
constexpr float kCentre = 6.f;        // half the middle square
constexpr float kPi = 3.14159265f;

constexpr float kMinFactor = 0.05f;
constexpr float kMaxFactor = 64.f;

enum Part { kNone = -1, kRingPart = 0, kAcross = 1, kDown = 2, kBoth = 3 };

// What the gizmo acts on: the layer's last Rotate and last Scale, and the
// pivot it sits on -- the rotation's, or else the scale's.
struct Target {
    ls::LayerId    layer;
    bool           hasRotate = false;
    TransformEntry rotate;
    bool           hasScale = false;
    TransformEntry scale;
    ls::Vec2f      pivot;
};

bool targetOf(Editor& editor, Target* out) {
    if (editor.tool != Tool::Move) {
        return false;
    }
    PaintLayer* layer = editor.active();
    if (layer == nullptr) {
        return false;
    }
    // A layer scaled by its box (Scale freely) has its own handles.
    FreeScale free;
    if (readFreeScale(editor.doc, layer->layer, &free)) {
        return false;
    }
    Target t;
    t.layer = layer->layer;
    for (const TransformEntry& entry : listTransforms(editor.doc, layer->layer)) {
        if (entry.kind == TransformKind::Rotate) {
            t.hasRotate = true;
            t.rotate = entry;
        } else if (entry.kind == TransformKind::Scale) {
            t.hasScale = true;
            t.scale = entry;
        }
    }
    if (!t.hasRotate && !t.hasScale) {
        return false;
    }
    t.pivot = t.hasRotate ? t.rotate.pivot : t.scale.pivot;
    *out = t;
    return true;
}

ImVec2 onScreen(CanvasView& canvas, ls::Vec2f p) {
    const ImVec2 origin = canvas.artworkOrigin();
    const float zoom = canvas.zoom();
    return ImVec2(origin.x + p.x * zoom, origin.y + p.y * zoom);
}

float lengthOf(ImVec2 v) {
    return std::sqrt(v.x * v.x + v.y * v.y);
}

// How far `p` is from the segment a-b.
float distanceToSegment(ImVec2 p, ImVec2 a, ImVec2 b) {
    const ImVec2 ab(b.x - a.x, b.y - a.y);
    const float len2 = ab.x * ab.x + ab.y * ab.y;
    float t = len2 > 0.f ? ((p.x - a.x) * ab.x + (p.y - a.y) * ab.y) / len2 : 0.f;
    t = std::clamp(t, 0.f, 1.f);
    return lengthOf(ImVec2(p.x - (a.x + ab.x * t), p.y - (a.y + ab.y * t)));
}

// The part under the pointer: the middle first, then the arrows, then the
// ring -- the small things inside the big one win.
Part partAt(ImVec2 centre, ImVec2 pointer) {
    const ImVec2 d(pointer.x - centre.x, pointer.y - centre.y);
    if (std::fabs(d.x) <= kCentre + 2.f && std::fabs(d.y) <= kCentre + 2.f) {
        return kBoth;
    }
    if (distanceToSegment(pointer, ImVec2(centre.x + kCentre, centre.y),
                          ImVec2(centre.x + kArrow, centre.y)) <= kArrowReach) {
        return kAcross;
    }
    if (distanceToSegment(pointer, ImVec2(centre.x, centre.y + kCentre),
                          ImVec2(centre.x, centre.y + kArrow)) <= kArrowReach) {
        return kDown;
    }
    if (std::fabs(lengthOf(d) - kRing) <= kRingReach) {
        return kRingPart;
    }
    return kNone;
}

float degreesOf(ImVec2 centre, ImVec2 p) {
    return std::atan2(p.y - centre.y, p.x - centre.x) * 180.f / kPi;
}

// The nearest multiple of `step`.
float snapped(float value, float step) {
    return std::round(value / step) * step;
}

std::string angleText(float degrees) {
    char text[32];
    std::snprintf(text, sizeof(text), "%.1f\xC2\xB0", static_cast<double>(degrees));
    return text;
}

void letGo(Editor& editor) {
    editor.gizmoPart = -1;
    editor.gizmoOp = ls::OperationId{};
}

} // namespace

bool handleTransformGizmo(Editor& editor, CanvasView& canvas, bool overCanvas) {
    Target t;
    if (!targetOf(editor, &t)) {
        if (editor.gizmoPart >= 0) {
            editor.doc.endAction();
            letGo(editor);
        }
        return false;
    }
    const ImGuiIO& io = ImGui::GetIO();
    const ImVec2 centre = onScreen(canvas, t.pivot);
    const ImVec2 pointer = io.MousePos;

    if (editor.gizmoPart < 0) {
        const Part part = overCanvas ? partAt(centre, pointer) : kNone;
        if (part == kNone) {
            return false;
        }
        const ImGuiMouseCursor cursors[] = { ImGuiMouseCursor_Hand, ImGuiMouseCursor_ResizeEW,
                                             ImGuiMouseCursor_ResizeNS, ImGuiMouseCursor_ResizeAll };
        ImGui::SetMouseCursor(cursors[part]);
        if (!ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsKeyDown(ImGuiKey_Space)) {
            return true;
        }
        if (layerLocked(editor.doc, t.layer)) {
            editor.say("This layer is locked -- unlock it in the Layers panel");
            return true;
        }
        editor.doc.beginAction(part == kRingPart ? "Rotate" : "Scale");
        if (part == kRingPart) {
            editor.gizmoOp = t.hasRotate
                ? t.rotate.id
                : addRotate(editor.doc, t.layer, 0.f, t.pivot, ls::SamplingPolicy::RotSprite);
            editor.gizmoStartAngle = t.hasRotate ? t.rotate.angleDegrees : 0.f;
        } else {
            editor.gizmoOp = t.hasScale
                ? t.scale.id
                : addScale(editor.doc, t.layer, { 1.f, 1.f }, t.pivot);
            editor.gizmoStartFactor = t.hasScale ? t.scale.factor : ls::Vec2f{ 1.f, 1.f };
        }
        if (!editor.gizmoOp.valid()) {
            editor.doc.abandonAction();
            letGo(editor);
            return true;
        }
        editor.gizmoPart = part;
        editor.gizmoGrab = { pointer.x, pointer.y };
        editor.gizmoTurned = 0.f;
        editor.gizmoLastPointer = degreesOf(centre, pointer);
        return true;
    }

    // Esc: as it was when grabbed, and nothing to undo.
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        editor.doc.abandonAction();
        letGo(editor);
        canvas.invalidate();
        editor.say("Put back as it was");
        return true;
    }

    const Part part = static_cast<Part>(editor.gizmoPart);
    if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        if (part == kRingPart) {
            // Round with the pointer, however many times: each frame's step,
            // taken the short way, added up.
            const float now = degreesOf(centre, pointer);
            float step = now - editor.gizmoLastPointer;
            while (step > 180.f) { step -= 360.f; }
            while (step < -180.f) { step += 360.f; }
            editor.gizmoTurned += step;
            editor.gizmoLastPointer = now;
            float angle = editor.gizmoStartAngle + editor.gizmoTurned;
            if (io.KeyShift) {
                angle = snapped(angle, 15.f);
            }
            setRotateAngle(editor.doc, editor.gizmoOp, angle);
        } else {
            const ls::Vec2f grab = editor.gizmoGrab;
            const ls::Vec2f from = editor.gizmoStartFactor;
            ls::Vec2f factor = from;
            if (part == kAcross || part == kDown) {
                // As far along the arrow as the pointer is, against where it
                // was grabbed.
                const bool across = part == kAcross;
                const float was = std::max(4.f, across ? grab.x - centre.x : grab.y - centre.y);
                const float is = across ? pointer.x - centre.x : pointer.y - centre.y;
                const float ratio = is / was;
                if (across) { factor.x = from.x * ratio; } else { factor.y = from.y * ratio; }
            } else {
                // The middle: both at once, larger to the right.
                const float ratio = std::exp((pointer.x - grab.x) / 120.f);
                factor = { from.x * ratio, from.y * ratio };
            }
            if (io.KeyShift) {
                factor = { snapped(factor.x, 0.25f), snapped(factor.y, 0.25f) };
            }
            factor = { std::clamp(factor.x, kMinFactor, kMaxFactor),
                       std::clamp(factor.y, kMinFactor, kMaxFactor) };
            setScaleFactor(editor.doc, editor.gizmoOp, factor);
        }
        canvas.invalidate();
        return true;
    }

    editor.doc.endAction();
    if (part == kRingPart) {
        editor.say("Turned to " + angleText(t.hasRotate ? t.rotate.angleDegrees : 0.f) +
                   " -- the drawing is untouched, so any angle comes back exactly");
    } else {
        char text[96];
        std::snprintf(text, sizeof(text), "Scaled to %.2f x %.2f",
                      static_cast<double>(t.hasScale ? t.scale.factor.x : 1.f),
                      static_cast<double>(t.hasScale ? t.scale.factor.y : 1.f));
        editor.say(text);
    }
    letGo(editor);
    return true;
}

void drawTransformGizmo(Editor& editor, CanvasView& canvas, ImDrawList* draw, ImVec2 origin,
                        float zoom) {
    (void)canvas;       // placed by origin and zoom, as the other overlays are
    Target t;
    if (!targetOf(editor, &t)) {
        return;
    }
    const ImVec2 centre(origin.x + t.pivot.x * zoom, origin.y + t.pivot.y * zoom);
    const Part held = static_cast<Part>(editor.gizmoPart);
    const Part hovered = held != kNone ? held : partAt(centre, ImGui::GetIO().MousePos);
    const ImU32 shadow = IM_COL32(0, 0, 0, 170);
    const ImU32 ringColour = ImGui::GetColorU32(theme::palette().accent);
    const ImU32 across = IM_COL32(224, 82, 72, 255);
    const ImU32 down = IM_COL32(92, 196, 104, 255);
    const ImU32 bright = IM_COL32(255, 255, 255, 255);
    const auto width = [&](Part p) { return hovered == p ? 3.f : 1.75f; };

    // The ring, and while it is held the angle it has reached.
    draw->AddCircle(centre, kRing, shadow, 96, width(kRingPart) + 2.f);
    draw->AddCircle(centre, kRing, hovered == kRingPart ? bright : ringColour, 96, width(kRingPart));
    if (t.hasRotate) {
        const float a = t.rotate.angleDegrees * kPi / 180.f;
        const ImVec2 mark(centre.x + std::cos(a) * kRing, centre.y + std::sin(a) * kRing);
        draw->AddLine(centre, mark, IM_COL32(255, 255, 255, 90), 1.f);
        draw->AddCircleFilled(mark, 4.f, ringColour);
        if (held == kRingPart) {
            // Just outside the ring, beyond the mark, clear of both.
            const std::string text = angleText(t.rotate.angleDegrees);
            const ImVec2 size = ImGui::CalcTextSize(text.c_str());
            const float out = kRing + 10.f + std::max(size.x, size.y) * 0.5f;
            const ImVec2 at(centre.x + std::cos(a) * out - size.x * 0.5f,
                            centre.y + std::sin(a) * out - size.y * 0.5f);
            draw->AddText(ImVec2(at.x + 1.f, at.y + 1.f), shadow, text.c_str());
            draw->AddText(at, bright, text.c_str());
        }
    }

    // The arrows, each with its head.
    const auto arrow = [&](ImVec2 dir, ImU32 colour, Part p) {
        const ImVec2 from(centre.x + dir.x * kCentre, centre.y + dir.y * kCentre);
        const ImVec2 tip(centre.x + dir.x * kArrow, centre.y + dir.y * kArrow);
        const ImVec2 side(-dir.y, dir.x);
        const ImVec2 back(tip.x - dir.x * 10.f, tip.y - dir.y * 10.f);
        const ImVec2 l(back.x + side.x * 5.f, back.y + side.y * 5.f);
        const ImVec2 r(back.x - side.x * 5.f, back.y - side.y * 5.f);
        draw->AddLine(from, back, shadow, width(p) + 2.f);
        draw->AddLine(from, back, hovered == p ? bright : colour, width(p));
        draw->AddTriangleFilled(tip, l, r, hovered == p ? bright : colour);
        draw->AddTriangle(tip, l, r, shadow, 1.f);
    };
    arrow(ImVec2(1.f, 0.f), across, kAcross);
    arrow(ImVec2(0.f, 1.f), down, kDown);

    // The middle: both axes at once.
    const ImVec2 a(centre.x - kCentre, centre.y - kCentre);
    const ImVec2 b(centre.x + kCentre, centre.y + kCentre);
    draw->AddRectFilled(a, b, hovered == kBoth ? bright : IM_COL32(230, 230, 236, 220));
    draw->AddRect(a, b, shadow, 0.f, 0, 1.5f);
    if (held == kAcross || held == kDown || held == kBoth) {
        char text[48];
        std::snprintf(text, sizeof(text), "%.2f x %.2f",
                      static_cast<double>(t.hasScale ? t.scale.factor.x : 1.f),
                      static_cast<double>(t.hasScale ? t.scale.factor.y : 1.f));
        const ImVec2 at(centre.x + 12.f, centre.y + 12.f);
        draw->AddText(ImVec2(at.x + 1.f, at.y + 1.f), shadow, text);
        draw->AddText(at, bright, text);
    }
}

} // namespace fast
