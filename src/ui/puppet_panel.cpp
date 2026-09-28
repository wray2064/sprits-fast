// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

#include "ui/puppet_panel.h"

#include "app/i18n.h"
#include "app/layers.h"
#include "app/puppet.h"
#include "ui/canvas_view.h"
#include "ui/editor.h"
#include "ui/panels.h"
#include "ui/theme.h"
#include "ui/ui_script.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace fast {

namespace {

using Mode = Editor::PuppetView::Mode;
using Placing = Editor::PuppetView::Placing;

constexpr float kRing = 48.f;          // the pose ring's radius, screen pixels
constexpr float kRingReach = 7.f;
constexpr float kSquare = 6.f;         // half the root's middle square
constexpr float kMarkReach = 8.f;      // a joint or socket mark
constexpr float kPi = 3.14159265f;

enum Drag { kNone = -1, kTurn = 0, kMoveRoot = 1, kJoint = 2, kSocket = 3 };

ImVec2 onScreen(CanvasView& canvas, ls::Vec2f p) {
    const ImVec2 origin = canvas.artworkOrigin();
    return ImVec2(origin.x + p.x * canvas.zoom(), origin.y + p.y * canvas.zoom());
}

float distance(ImVec2 a, ImVec2 b) {
    return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y));
}

float degreesOf(ImVec2 centre, ImVec2 p) {
    return std::atan2(p.y - centre.y, p.x - centre.x) * 180.f / kPi;
}

// Where a part's joint shows: through the whole chain in Pose, where it is
// drawn when drawing on the part.
ls::Vec2f jointShown(Editor& editor, ls::SpriteId part) {
    const ls::Vec2f joint = jointPosition(editor.doc, part);
    if (editor.puppet.mode != Mode::Pose) {
        return joint;
    }
    return partPlacement(editor.doc, part).transformPoint(joint);
}

bool isRoot(Editor& editor, ls::SpriteId part) {
    return !parentPart(editor.doc, part).valid();
}

// Whether the root's own transform is the identity: it is drawn at rest.
bool atRest(Editor& editor, ls::SpriteId root) {
    auto own = editor.doc.engine().getSpriteTransform(root);
    if (own.fail()) {
        return true;
    }
    const ls::Mat3f one;
    for (int i = 0; i < 9; ++i) {
        if (std::fabs(own.value.m[i] - one.m[i]) > 1e-5f) {
            return false;
        }
    }
    return true;
}

// On the same line as the last control when `label`'s radio button fits
// there, on the next when it does not: a translation can be longer.
void besideIfItFits(const char* label) {
    const ImGuiStyle& style = ImGui::GetStyle();
    const float wide = ImGui::GetFrameHeight() + style.ItemInnerSpacing.x +
                       ImGui::CalcTextSize(label).x;
    const float right = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
    if (ImGui::GetItemRectMax().x + style.ItemSpacing.x + wide <= right) {
        ImGui::SameLine();
    }
}

void setMode(Editor& editor, Mode mode) {
    if (editor.puppet.mode == mode) {
        return;
    }
    editor.puppet.mode = mode;
    editor.puppet.placing = Placing::None;
    editor.puppet.frameEntered = editor.timeline.activeFrame;
    resyncLayers(editor);
}

// `candidate` hangs somewhere below `part` (or is it).
bool below(Editor& editor, ls::SpriteId candidate, ls::SpriteId part) {
    for (ls::SpriteId at = candidate; at.valid(); at = parentPart(editor.doc, at)) {
        if (at == part) {
            return true;
        }
        if (hangingDepth(editor.doc, at) > 256) {
            break;
        }
    }
    return false;
}

void partRow(Editor& editor, ls::SpriteId part) {
    const int depth = hangingDepth(editor.doc, part);
    ImGui::PushID(static_cast<int>(part.value));
    const std::string label = std::string(static_cast<size_t>(depth) * 2, ' ') +
                              partName(editor.doc, part);
    char angle[24] = "";
    if (editor.puppet.mode == Mode::Pose) {
        std::snprintf(angle, sizeof(angle), "%.0f\xC2\xB0",
                      static_cast<double>(jointAngle(editor.doc, part)));
    }
    if (ImGui::Selectable(label.c_str(), editor.puppet.part == part)) {
        editor.puppet.part = part;
        editor.puppet.placing = Placing::None;
        if (editor.puppet.mode == Mode::DrawPart) {
            resyncLayers(editor);
        }
    }
    if (angle[0] != '\0') {
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 30.f);
        ImGui::TextDisabled("%s", angle);
    }
    ImGui::PopID();
}

void partFromActiveLayer(Editor& editor) {
    PaintLayer* layer = editor.active();
    if (layer == nullptr) {
        return;
    }
    auto info = editor.doc.engine().getLayerInfo(layer->layer);
    const std::string name = info.ok() && !info.value.name.empty() ? info.value.name : "part";
    // The joint starts in the middle of what the layer draws; drag it to the
    // real joint afterwards.
    ls::Vec2f joint { 0.5f, 0.5f };
    auto size = editor.doc.engine().getCanvasSize(editor.doc.id());
    if (size.ok()) {
        auto drawn = editor.doc.engine().compileLayer(
            layer->layer, compileProfile(ls::CompileProfileType::BoundsOnly,
                                         static_cast<uint32_t>(size.value.x),
                                         static_cast<uint32_t>(size.value.y)));
        if (drawn.ok() && drawn.value.bounds.width() > 0) {
            joint = jointPoint({ (drawn.value.bounds.min.x + drawn.value.bounds.max.x) * 0.5f,
                                 (drawn.value.bounds.min.y + drawn.value.bounds.max.y) * 0.5f });
        }
    }
    const ls::SpriteId part = partFromLayer(editor.doc, layer->layer, name, joint);
    if (!part.valid()) {
        editor.say("Could not make a part of that layer");
        return;
    }
    editor.puppet.part = part;
    resyncLayers(editor);
    editor.say("\"" + name + "\" is a part now: hang it from another, and put its joint "
               "where it turns (Draw part)");
}

// The pose of the picked part, and what is done with a pose: first in the
// panel while posing, so capturing is never scrolled out of reach.
void drawPoseControls(Editor& editor, CanvasView& canvas, ls::SpriteId part, float half) {
    Editor::PuppetView& view = editor.puppet;
    theme::sectionHeader(tr("POSE"));
    float angle = jointAngle(editor.doc, part);
    ImGui::SetNextItemWidth(-1.f);
    if (ImGui::InputFloat("##jointangle", &angle, 0.f, 0.f, "%.2f deg")) {
        setJointAngle(editor.doc, part, angle);
        canvas.invalidate();
    }
    holdForUndo(editor, editor.draggingTransform, "Pose");
    const ls::SpriteId root = rootPart(editor.doc, part);
    if (part == root) {
        const ls::Vec2f offset = rootOffset(editor.doc, root);
        float at[2] = { offset.x, offset.y };
        ImGui::SetNextItemWidth(-1.f);
        if (ImGui::InputFloat2("##rootoffset", at, "%.0f px")) {
            setRootOffset(editor.doc, root, { std::round(at[0]), std::round(at[1]) });
            canvas.invalidate();
        }
        holdForUndo(editor, editor.draggingTransform, "Pose");
    }
    if (ImGui::Button(tr("Rest pose"), ImVec2(half, 0.f))) {
        restPose(editor.doc, root);
        canvas.invalidate();
    }
    ImGui::SameLine();
    if (ImGui::Button(tr("Capture pose"), ImVec2(half, 0.f))) {
        const int at = capturePose(editor.doc, root, editor.timeline.activeFrame);
        if (at >= 0) {
            editor.timeline.activeFrame = at;
            view.frameEntered = at;
            resyncLayers(editor);
            editor.say("Captured as frame " + std::to_string(at + 1) +
                       " -- pose again and capture the next");
        } else {
            editor.say("Could not capture the pose");
        }
    }
}

} // namespace

// ------------------------------------------------------------------ panel --

void settlePuppetView(Editor& editor, CanvasView& canvas) {
    Editor::PuppetView& view = editor.puppet;
    if (view.part.valid() && !isPart(editor.doc, view.part)) {
        view.part = ls::SpriteId{};
    }
    if (view.mode != Mode::Frames &&
        (!view.part.valid() || editor.timeline.activeFrame != view.frameEntered)) {
        setMode(editor, Mode::Frames);
    }
    // A root posed away from rest cannot be drawn on where it shows.
    if (view.mode == Mode::DrawPart && isRoot(editor, view.part) && !atRest(editor, view.part)) {
        setMode(editor, Mode::Frames);
        editor.say("Put the puppet at rest (Rest pose) to draw on its root");
    }
    canvas.showAssembly(view.mode == Mode::Pose ? rootPart(editor.doc, view.part) : ls::SpriteId{});
}

void drawPuppetPanel(Editor& editor, CanvasView& canvas) {
    Editor::PuppetView& view = editor.puppet;
    const std::vector<ls::SpriteId> parts = partsOf(editor.doc);

    // What the canvas shows.
    int mode = static_cast<int>(view.mode);
    const bool picked = view.part.valid();
    if (ImGui::RadioButton(tr("Frames"), mode == 0)) { setMode(editor, Mode::Frames); }
    besideIfItFits(tr("Draw part"));
    const bool rootPosed = picked && isRoot(editor, view.part) && !atRest(editor, view.part);
    ImGui::BeginDisabled(!picked || rootPosed);
    if (ImGui::RadioButton(tr("Draw part"), mode == 1)) { setMode(editor, Mode::DrawPart); }
    ImGui::EndDisabled();
    if (rootPosed && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("%s", tr("A posed root is drawn where the pose puts it; put the "
                                   "puppet at rest to draw on it."));
    }
    besideIfItFits(tr("Pose"));
    ImGui::BeginDisabled(!picked);
    if (ImGui::RadioButton(tr("Pose"), mode == 2)) { setMode(editor, Mode::Pose); }
    ImGui::EndDisabled();
    const float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    if (view.mode == Mode::Pose && view.part.valid()) {
        drawPoseControls(editor, canvas, view.part, half);
    }

    // The parts, as the puppets they hang in.
    theme::sectionHeader(tr("PARTS"));
    for (ls::SpriteId root : puppetRoots(editor.doc)) {
        // Parents before children, each branch together.
        std::vector<ls::SpriteId> stack { root };
        while (!stack.empty()) {
            const ls::SpriteId at = stack.back();
            stack.pop_back();
            partRow(editor, at);
            std::vector<ls::SpriteId> children;
            for (const PartSocket& socket : socketsOf(editor.doc, at)) {
                auto hung = editor.doc.engine().getAttachedSprites(socket.id);
                if (hung.ok()) {
                    children.insert(children.end(), hung.value.begin(), hung.value.end());
                }
            }
            stack.insert(stack.end(), children.rbegin(), children.rend());
        }
    }
    ImGui::BeginDisabled(view.mode != Mode::Frames || editor.active() == nullptr);
    if (ImGui::Button(tr("Part from layer"), ImVec2(half, 0.f))) {
        partFromActiveLayer(editor);
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("%s", tr("The frame's picked layer becomes a part, drawn where it "
                                   "was (Frames)."));
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!picked);
    if (ImGui::Button(tr("Delete part"), ImVec2(half, 0.f))) {
        deletePart(editor.doc, view.part);
        view.part = ls::SpriteId{};
        setMode(editor, Mode::Frames);
    }
    ImGui::EndDisabled();
    if (parts.empty()) {
        theme::hint(tr("A puppet is a character in parts: draw each limb on a layer of "
                       "its own, pick the layer, and make it a part. Hang each part from "
                       "the one it moves with, then pose and capture frames."));
    }

    if (!picked) {
        return;
    }
    const ls::SpriteId part = view.part;

    // The picked part.
    theme::sectionHeader(tr("PART"));
    if (view.naming != part) {
        std::snprintf(view.nameBuffer, sizeof(view.nameBuffer), "%s", partName(editor.doc, part).c_str());
        view.naming = part;
    }
    ImGui::SetNextItemWidth(-1.f);
    ImGui::InputText("##partname", view.nameBuffer, sizeof(view.nameBuffer));
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        renamePart(editor.doc, part, view.nameBuffer);
    }

    // What it hangs from.
    const ls::SpriteId parent = parentPart(editor.doc, part);
    const std::string hangs = parent.valid() ? partName(editor.doc, parent) : std::string(tr("nothing"));
    ImGui::SetNextItemWidth(-1.f);
    const bool choosing = ImGui::BeginCombo("##hangsfrom",
                                            (std::string(tr("hangs from")) + " " + hangs).c_str());
    nameForScripts("Hangs from");
    if (choosing) {
        if (ImGui::Selectable(tr("nothing"), !parent.valid()) && parent.valid()) {
            unhangPart(editor.doc, part);
        }
        for (ls::SpriteId other : parts) {
            if (below(editor, other, part)) {
                continue;               // itself, or something hanging from it
            }
            if (ImGui::Selectable(partName(editor.doc, other).c_str(), other == parent) &&
                other != parent) {
                if (parent.valid()) {
                    unhangPart(editor.doc, part);
                }
                hangPartWhereItIs(editor.doc, part, other, partName(editor.doc, part));
            }
        }
        ImGui::EndCombo();
    }
    if (parent.valid()) {
        auto how = editor.doc.engine().getAttachment(part);
        bool behind = how.ok() && how.value.behindParent;
        if (ImGui::Checkbox(tr("Behind its parent"), &behind)) {
            setBehindParent(editor.doc, part, behind);
        }
    }

    // Joint and sockets.
    const ls::Vec2f joint = jointPosition(editor.doc, part);
    ImGui::Text("%s %.1f, %.1f", tr("joint"), static_cast<double>(joint.x), static_cast<double>(joint.y));
    ImGui::BeginDisabled(view.mode != Mode::DrawPart);
    if (ImGui::Button(tr("Place joint"), ImVec2(half, 0.f))) {
        view.placing = view.placing == Placing::Joint ? Placing::None : Placing::Joint;
    }
    ImGui::SameLine();
    if (ImGui::Button(tr("Add socket"), ImVec2(half, 0.f))) {
        view.placing = view.placing == Placing::Socket ? Placing::None : Placing::Socket;
    }
    ImGui::EndDisabled();
    if (view.mode != Mode::DrawPart) {
        theme::hint(tr("Joint and sockets are placed in Draw part: click where they go, or "
                       "drag their marks."));
    } else if (view.placing != Placing::None) {
        theme::hint(view.placing == Placing::Joint ? tr("Click where the part turns.")
                                                   : tr("Click where a part will hang."));
    }
    for (const PartSocket& socket : socketsOf(editor.doc, part)) {
        ImGui::PushID(static_cast<int>(socket.id.value));
        if (ImGui::SmallButton("x")) {
            removeSocketFrom(editor.doc, socket.id);
        }
        ImGui::SameLine();
        ImGui::Text("%s  %.1f, %.1f", socket.name.c_str(), static_cast<double>(socket.position.x),
                    static_cast<double>(socket.position.y));
        ImGui::PopID();
    }

}

// ------------------------------------------------------------ the canvas --

bool handlePuppetInput(Editor& editor, CanvasView& canvas, bool overCanvas) {
    Editor::PuppetView& view = editor.puppet;
    if (view.mode == Mode::Frames || !view.part.valid()) {
        return false;
    }
    const ImGuiIO& io = ImGui::GetIO();
    const ImVec2 pointer = io.MousePos;

    // A drag in progress.
    if (view.dragging >= 0) {
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            editor.doc.abandonAction();
            view.dragging = kNone;
            canvas.invalidate();
            editor.say("Put back as it was");
            return true;
        }
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            editor.doc.endAction();
            view.dragging = kNone;
            return true;
        }
        const ls::SpriteId part = view.part;
        switch (view.dragging) {
            case kTurn: {
                const ImVec2 centre = onScreen(canvas, jointShown(editor, part));
                const float now = degreesOf(centre, pointer);
                float step = now - view.lastPointer;
                while (step > 180.f) { step -= 360.f; }
                while (step < -180.f) { step += 360.f; }
                view.turned += step;
                view.lastPointer = now;
                float angle = view.startAngle + view.turned;
                if (io.KeyShift) {
                    angle = std::round(angle / 15.f) * 15.f;
                }
                setJointAngle(editor.doc, part, angle);
                break;
            }
            case kMoveRoot: {
                const float zoom = std::max(canvas.zoom(), 0.0001f);
                float dx = std::round((pointer.x - view.grab.x) / zoom);
                float dy = std::round((pointer.y - view.grab.y) / zoom);
                if (io.KeyShift) {
                    if (std::fabs(dx) >= std::fabs(dy)) { dy = 0.f; } else { dx = 0.f; }
                }
                setRootOffset(editor.doc, part, { view.startOffset.x + dx, view.startOffset.y + dy });
                break;
            }
            case kJoint:
                setJoint(editor.doc, part, jointPoint(canvas.pointerExact()));
                break;
            case kSocket:
                moveSocketTo(editor.doc, view.socket, jointPoint(canvas.pointerExact()));
                break;
            default:
                break;
        }
        canvas.invalidate();
        return true;
    }
    if (!overCanvas || ImGui::IsKeyDown(ImGuiKey_Space)) {
        return false;
    }
    const bool press = ImGui::IsMouseClicked(ImGuiMouseButton_Left);

    if (view.mode == Mode::DrawPart) {
        if (view.placing != Placing::None) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            if (press) {
                const ls::Vec2f at = jointPoint(canvas.pointerExact());
                if (view.placing == Placing::Joint) {
                    setJoint(editor.doc, view.part, at);
                    editor.say("The part turns here now");
                } else {
                    const std::string name = "socket " +
                        std::to_string(socketsOf(editor.doc, view.part).size() + 1);
                    addSocketTo(editor.doc, view.part, name, at);
                    editor.say("A socket: hang a part from it where it is drawn");
                }
                view.placing = Placing::None;
                canvas.invalidate();
            }
            return true;
        }
        // The joint and the sockets are dragged by their marks.
        int grabbed = kNone;
        ls::SocketId socket;
        if (distance(pointer, onScreen(canvas, jointPosition(editor.doc, view.part))) <= kMarkReach) {
            grabbed = kJoint;
        } else {
            for (const PartSocket& s : socketsOf(editor.doc, view.part)) {
                if (distance(pointer, onScreen(canvas, s.position)) <= kMarkReach) {
                    grabbed = kSocket;
                    socket = s.id;
                    break;
                }
            }
        }
        if (grabbed == kNone) {
            return false;           // the tools draw on the part
        }
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
        if (press) {
            editor.doc.beginAction(grabbed == kJoint ? "Move joint" : "Move socket");
            view.dragging = grabbed;
            view.socket = socket;
        }
        return true;
    }

    // Pose: the ring and the root's square on the picked part; anywhere else
    // a click picks the part drawn there. Nothing draws.
    const ls::SpriteId part = view.part;
    const ImVec2 centre = onScreen(canvas, jointShown(editor, part));
    const float fromCentre = distance(pointer, centre);
    const bool root = isRoot(editor, part);
    int over = kNone;
    if (root && std::fabs(pointer.x - centre.x) <= kSquare + 2.f &&
        std::fabs(pointer.y - centre.y) <= kSquare + 2.f) {
        over = kMoveRoot;
    } else if (std::fabs(fromCentre - kRing) <= kRingReach) {
        over = kTurn;
    }
    if (over != kNone) {
        ImGui::SetMouseCursor(over == kTurn ? ImGuiMouseCursor_Hand : ImGuiMouseCursor_ResizeAll);
    }
    if (!press) {
        return true;
    }
    if (over == kNone) {
        const ls::SpriteId hit = partAt(editor.doc, rootPart(editor.doc, part), canvas.pointerExact());
        if (hit.valid()) {
            view.part = hit;
        }
        return true;
    }
    editor.doc.beginAction("Pose");
    view.dragging = over;
    view.startAngle = jointAngle(editor.doc, part);
    view.turned = 0.f;
    view.lastPointer = degreesOf(centre, pointer);
    view.startOffset = rootOffset(editor.doc, part);
    view.grab = { pointer.x, pointer.y };
    return true;
}

void drawPuppetOverlay(Editor& editor, CanvasView& canvas, ImDrawList* draw, ImVec2 origin,
                       float zoom) {
    (void)canvas;
    const Editor::PuppetView& view = editor.puppet;
    if (view.mode == Mode::Frames || !view.part.valid()) {
        return;
    }
    const auto at = [&](ls::Vec2f p) { return ImVec2(origin.x + p.x * zoom, origin.y + p.y * zoom); };
    const ImU32 shadow = IM_COL32(0, 0, 0, 170);
    const ImU32 accent = ImGui::GetColorU32(theme::palette().accent);
    const ImU32 bright = IM_COL32(255, 255, 255, 255);
    const ImU32 socketColour = IM_COL32(92, 196, 104, 255);

    if (view.mode == Mode::DrawPart) {
        // The joint as a cross in a circle, the sockets as diamonds, named.
        const ImVec2 joint = at(jointPosition(editor.doc, view.part));
        draw->AddCircle(joint, 6.f, shadow, 20, 4.f);
        draw->AddCircle(joint, 6.f, accent, 20, 2.f);
        draw->AddLine(ImVec2(joint.x - 9.f, joint.y), ImVec2(joint.x + 9.f, joint.y), accent, 1.5f);
        draw->AddLine(ImVec2(joint.x, joint.y - 9.f), ImVec2(joint.x, joint.y + 9.f), accent, 1.5f);
        for (const PartSocket& socket : socketsOf(editor.doc, view.part)) {
            const ImVec2 s = at(socket.position);
            const ImVec2 d[4] = { ImVec2(s.x, s.y - 6.f), ImVec2(s.x + 6.f, s.y),
                                  ImVec2(s.x, s.y + 6.f), ImVec2(s.x - 6.f, s.y) };
            draw->AddQuadFilled(d[0], d[1], d[2], d[3], socketColour);
            draw->AddQuad(d[0], d[1], d[2], d[3], shadow, 1.5f);
            draw->AddText(ImVec2(s.x + 1.f + 9.f, s.y - 7.f), shadow, socket.name.c_str());
            draw->AddText(ImVec2(s.x + 9.f, s.y - 8.f), bright, socket.name.c_str());
        }
        return;
    }

    // Pose: the bones -- each joint to the joints hanging from it -- every
    // joint a dot, and the picked part's ring and, for a root, its square.
    const ls::SpriteId root = rootPart(editor.doc, view.part);
    for (ls::SpriteId part : puppetOrder(editor.doc, root)) {
        const ImVec2 j = at(jointShown(editor, part));
        const ls::SpriteId parent = parentPart(editor.doc, part);
        if (parent.valid()) {
            const ImVec2 p = at(jointShown(editor, parent));
            draw->AddLine(p, j, shadow, 4.f);
            draw->AddLine(p, j, IM_COL32(255, 255, 255, 150), 1.5f);
        }
    }
    for (ls::SpriteId part : puppetOrder(editor.doc, root)) {
        const ImVec2 j = at(jointShown(editor, part));
        draw->AddCircleFilled(j, part == view.part ? 5.f : 3.5f, shadow);
        draw->AddCircleFilled(j, part == view.part ? 4.f : 2.5f, part == view.part ? accent : bright);
    }
    const ImVec2 centre = at(jointShown(editor, view.part));
    const bool held = view.dragging == kTurn;
    draw->AddCircle(centre, kRing, shadow, 96, held ? 5.f : 3.75f);
    draw->AddCircle(centre, kRing, held ? bright : accent, 96, held ? 3.f : 1.75f);
    const float a = jointAngle(editor.doc, view.part) * kPi / 180.f;
    const ImVec2 mark(centre.x + std::cos(a) * kRing, centre.y + std::sin(a) * kRing);
    draw->AddCircleFilled(mark, 4.f, IM_COL32(255, 170, 60, 255));
    if (held) {
        char text[24];
        std::snprintf(text, sizeof(text), "%.1f\xC2\xB0", static_cast<double>(jointAngle(editor.doc, view.part)));
        const ImVec2 size = ImGui::CalcTextSize(text);
        const float out = kRing + 10.f + std::max(size.x, size.y) * 0.5f;
        const ImVec2 t(centre.x + std::cos(a) * out - size.x * 0.5f,
                       centre.y + std::sin(a) * out - size.y * 0.5f);
        draw->AddText(ImVec2(t.x + 1.f, t.y + 1.f), shadow, text);
        draw->AddText(t, bright, text);
    }
    if (isRoot(editor, view.part)) {
        const ImVec2 lo(centre.x - kSquare, centre.y - kSquare);
        const ImVec2 hi(centre.x + kSquare, centre.y + kSquare);
        draw->AddRectFilled(lo, hi, view.dragging == kMoveRoot ? bright : IM_COL32(230, 230, 236, 220));
        draw->AddRect(lo, hi, shadow, 0.f, 0, 1.5f);
    }
}

} // namespace fast
