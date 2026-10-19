// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

// panel_transform.cpp -- the transform list.
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

// -------------------------------------------------------------- transform --

void drawTransformPanel(Editor& editor, CanvasView& canvas) {
    PaintLayer* layer = editor.active();
    if (layer == nullptr) {
        ImGui::TextDisabled("%s", tr("No layer selected."));
        return;
    }

    const ls::Vec2f centre = [&] {
        auto size = editor.doc.engine().getCanvasSize(editor.doc.id());
        if (size.fail()) { return ls::Vec2f{0.f, 0.f}; }
        return ls::Vec2f{ static_cast<float>(size.value.x) * 0.5f,
                          static_cast<float>(size.value.y) * 0.5f };
    }();

    // Four buttons that share the width, and wrap two and two when the panel is
    // too narrow for the longest word (a larger interface, a longer language).
    const char* const addLabels[] = { "Rotate", "Scale", "Mirror", "Offset" };
    const widgets::ButtonRow add = widgets::buttonRow(addLabels, 4);
    const float third = add.width;

    // A rotation or a scale comes with the gizmo to drive it: the Move tool
    // shows it on the canvas (transform_gizmo.h).
    if (ImGui::Button(tr("Rotate"), ImVec2(third, 0.f))) {
        editor.doc.beginAction("Add rotate");
        addRotate(editor.doc, layer->layer, 0.f, centre, ls::SamplingPolicy::RotSprite);
        editor.doc.endAction();
        editor.tool = Tool::Move;
        editor.say("Drag the ring to turn it, Shift for steps of 15 degrees -- or type the angle");
        canvas.invalidate();
    }
    widgets::beside(add, 1);
    if (ImGui::Button(tr("Scale"), ImVec2(third, 0.f))) {
        editor.doc.beginAction("Add scale");
        addScale(editor.doc, layer->layer, {1.f, 1.f}, centre);
        editor.doc.endAction();
        editor.tool = Tool::Move;
        editor.say("Drag an arrow to stretch it, the square for both, Shift for quarter steps");
        canvas.invalidate();
    }
    widgets::beside(add, 2);
    if (ImGui::Button(tr("Mirror"), ImVec2(third, 0.f))) {
        editor.doc.beginAction("Add mirror");
        addMirror(editor.doc, layer->layer, ls::MirrorAxis::X, centre);
        editor.doc.endAction();
        canvas.invalidate();
    }
    widgets::beside(add, 3);
    if (ImGui::Button(tr("Offset"), ImVec2(third, 0.f))) {
        editor.doc.beginAction("Add offset");
        addOffset(editor.doc, layer->layer, {0.f, 0.f});
        editor.doc.endAction();
        canvas.invalidate();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", tr("Move what the layer draws by whole pixels, without "
                          "touching the drawing -- the motion a tween moves."));
    }

    const std::vector<TransformEntry> transforms =
        listTransforms(editor.doc, layer->layer);

    if (transforms.empty()) {
        ImGui::Dummy(ImVec2(0.f, 6.f));
        ImGui::PushStyleColor(ImGuiCol_Text, theme::palette().textDim);
        ImGui::TextWrapped("%s", tr("Nothing applied. Add a rotation and turn it with the "
                           "ring on the canvas, or type its angle: the picture "
                           "is rebuilt from the drawing each time, so returning "
                           "to zero returns the original pixels exactly."));
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
        ImGui::SetNextItemWidth(-theme::px(30.f));
        // Numbers typed, not slid: a slider's steps are the panel's width,
        // and 90 is where a typed 90 lands. The gizmo drags them by hand.
        if (entry.kind == TransformKind::Rotate) {
            float angle = entry.angleDegrees;
            if (ImGui::InputFloat("##angle", &angle, 0.f, 0.f, "%.2f deg")) {
                setRotateAngle(editor.doc, entry.id, angle);
                changed = true;
            }
            bracketDrag(editor, editor.draggingTransform, "Rotate");
        } else if (entry.kind == TransformKind::Scale) {
            float factor[2] = { entry.factor.x, entry.factor.y };
            if (ImGui::InputFloat2("##factor", factor, "%.3f")) {
                setScaleFactor(editor.doc, entry.id,
                               { std::clamp(factor[0], 0.05f, 64.f), std::clamp(factor[1], 0.05f, 64.f) });
                changed = true;
            }
            bracketDrag(editor, editor.draggingTransform, "Scale");
        } else if (entry.kind == TransformKind::Offset) {
            float delta[2] = { entry.delta.x, entry.delta.y };
            if (ImGui::InputFloat2("##delta", delta, "%.0f px")) {
                setOffsetDelta(editor.doc, entry.id,
                               { std::round(std::clamp(delta[0], -4096.f, 4096.f)),
                                 std::round(std::clamp(delta[1], -4096.f, 4096.f)) });
                changed = true;
            }
            bracketDrag(editor, editor.draggingTransform, "Offset");
        } else {
            ImGui::TextDisabled("%s", tr("no parameters"));
        }
        if (entry.kind == TransformKind::Rotate || entry.kind == TransformKind::Scale) {
            // How each pixel is picked: RotSprite for pixel art, or the
            // engine's general ones.
            const struct { ls::SamplingPolicy policy; const char* name; } kinds[] = {
                { ls::SamplingPolicy::RotSprite, "Pixel art (RotSprite)" },
                { ls::SamplingPolicy::Coverage, "Coverage" },
                { ls::SamplingPolicy::Majority, "Majority" },
                { ls::SamplingPolicy::Center, "Nearest" },
            };
            const char* current = "Coverage";
            for (const auto& k : kinds) {
                if (k.policy == entry.sampling) { current = k.name; }
            }
            ImGui::SetNextItemWidth(-theme::px(30.f));
            if (ImGui::BeginCombo("##sampling", current)) {
                for (const auto& k : kinds) {
                    if (ImGui::Selectable(k.name, k.policy == entry.sampling)) {
                        editor.doc.beginAction("Transform sampling");
                        setTransformSampling(editor.doc, entry.id, k.policy);
                        editor.doc.endAction();
                        changed = true;
                    }
                }
                ImGui::EndCombo();
            }
        }

        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, theme::palette().danger);
        if (ImGui::SmallButton(tr("x"))) {
            toRemove = entry.id;
        }
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", tr("Remove. The original returns exactly."));
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
    if (ImGui::Button(tr("Remove all"), ImVec2(-1.f, 0.f))) {
        editor.doc.beginAction("Remove transforms");
        clearTransforms(editor.doc, layer->layer);
        editor.doc.endAction();
        canvas.invalidate();
    }
}


} // namespace fast
