// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

// timeline.cpp — the frame strip, and the onion skin.
//
// Every thumbnail here is a texture the FrameCache already holds, drawn scaled
// down. That is the whole reason a timeline is affordable: eight frames on
// screen cost eight textured quads, where eight compiles would be 31 ms at
// 128x128 and a strip that stutters whenever it is visible.
//
// The one place that is not true is a frame that has never been compiled -- a
// file with forty frames, just opened. Those are compiled as they come into
// view, a few per UI frame, so opening a long animation is not a two-second
// pause before the window appears.

#include "ui/panels.h"
#include "ui/theme.h"

#include "app/tracks.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace fast {
namespace {

constexpr float kThumbSize = 56.f;    // the frame cell, in screen pixels
constexpr float kStepSize  = 34.f;    // a step chip, which carries a number

// A step row is a second list under the frames, so the panel is taller when a
// cycle is selected. It is not always shown because most of the time there is
// nothing in it to say.
constexpr float kBaseHeight  = 158.f;
constexpr float kStepsHeight = 52.f;

// How many frames the strip may compile in one UI frame.
//
// Two cases need this, and the second is the one that bites. Opening a long
// animation has nothing cached, so without a budget the window appears after
// every frame has compiled. And an undo dirties the whole document -- the
// engine restores state wholesale and cannot know what actually differs -- so
// every thumbnail is stale at once; at 128x128 with twenty frames, refreshing
// them all in one pass is a 78 ms hitch after every Ctrl+Z.
//
// A thumbnail that is one UI frame behind is invisible. A hitch is not. So the
// strip refreshes a couple per pass and shows the picture it already has in the
// meantime -- the canvas itself is never delayed, because it compiles before
// the strip is reached.
constexpr int kCompileBudget = 2;

// Milliseconds shown as something a person reads at a glance.
std::string timingOf(int milliseconds) {
    char buffer[32];
    if (milliseconds >= 1000) {
        std::snprintf(buffer, sizeof(buffer), "%.2fs",
                      static_cast<double>(milliseconds) / 1000.0);
    } else {
        std::snprintf(buffer, sizeof(buffer), "%dms", milliseconds);
    }
    return buffer;
}

// What a frame is called in the strip: its name if it has one, its number if
// not. Numbered from one, because frame zero is a programmer's idea.
std::string labelOf(const Frame& frame, int index) {
    if (!frame.name.empty()) {
        return frame.name;
    }
    return std::to_string(index + 1);
}

// Draws one frame's picture inside a cell, centred and at a whole-number scale.
// Whole numbers matter as much here as on the canvas: a thumbnail at 3.7x has
// pixels of two different widths, which is exactly the artefact the artist is
// looking at the thumbnail to check for.
void drawThumbnail(ImDrawList* draw, const CanvasView& canvas,
                   const FrameCache::Entry& entry, ImVec2 cell, float size) {
    if (entry.width == 0 || entry.height == 0) {
        return;
    }
    const float fit = std::min(size / static_cast<float>(entry.width),
                               size / static_cast<float>(entry.height));
    // Below 1x there is nothing to round to, so a large canvas is simply
    // scaled to fit rather than refused a thumbnail.
    const float scale = fit >= 1.f ? std::floor(fit) : fit;

    const float width  = static_cast<float>(entry.width) * scale;
    const float height = static_cast<float>(entry.height) * scale;
    const ImVec2 at(cell.x + (size - width) * 0.5f,
                    cell.y + (size - height) * 0.5f);
    canvas.drawFrameTinted(draw, entry, at, scale, IM_COL32_WHITE);
}

const char* loopName(LoopMode mode) {
    switch (mode) {
        case LoopMode::Loop:     return "Loop";
        case LoopMode::Once:     return "Once";
        case LoopMode::PingPong: return "Ping-pong";
    }
    return "Loop";
}

// The controls that make and unmake cycles. Kept apart from the frame controls
// because they act on different things: one edits the drawings, the other edits
// the order they play in.
void drawCycleControls(Editor& editor) {
    const theme::Palette& c = theme::palette();
    TimelineSettings& timeline = editor.timeline;
    const int frameCount = static_cast<int>(editor.frames.size());

    ImGui::TextColored(c.textDim, "Cycle");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(150.f);

    // "Every frame" is not a cycle, it is what a document without one plays.
    // Naming it in the same list is how a person discovers cycles exist.
    const std::string current = timeline.activeCycle < 0
        ? std::string("Every frame")
        : (editor.cycles[static_cast<size_t>(timeline.activeCycle)].name.empty()
               ? "Cycle " + std::to_string(timeline.activeCycle + 1)
               : editor.cycles[static_cast<size_t>(timeline.activeCycle)].name);

    if (ImGui::BeginCombo("##cycle", current.c_str())) {
        if (ImGui::Selectable("Every frame", timeline.activeCycle < 0)) {
            selectCycle(editor, -1);
        }
        for (int i = 0; i < static_cast<int>(editor.cycles.size()); ++i) {
            ImGui::PushID(i);
            const std::string label = editor.cycles[static_cast<size_t>(i)].name.empty()
                ? "Cycle " + std::to_string(i + 1)
                : editor.cycles[static_cast<size_t>(i)].name;
            if (ImGui::Selectable(label.c_str(), i == timeline.activeCycle)) {
                selectCycle(editor, i);
            }
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }

    ImGui::SameLine();
    ImGui::BeginDisabled(editor.cycles.size() >= kMaxCycles || frameCount == 0);
    if (ImGui::Button("New")) {
        const int at = addCycle(editor.doc, "cycle " +
                                std::to_string(editor.cycles.size() + 1), frameCount);
        if (at >= 0) {
            resyncFrames(editor);
            selectCycle(editor, at);
            editor.say("New cycle over every frame -- trim it to taste");
        }
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("A new cycle covering every frame in order.\n"
                          "It plays straight away; refining it is subtraction.");
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    ImGui::BeginDisabled(timeline.activeCycle < 0);
    if (ImGui::Button("Rename")) {
        const std::string& name =
            editor.cycles[static_cast<size_t>(timeline.activeCycle)].name;
        std::snprintf(timeline.cycleNameBuffer, sizeof(timeline.cycleNameBuffer),
                      "%s", name.c_str());
        ImGui::OpenPopup("rename cycle");
    }
    ImGui::SameLine();
    if (ImGui::Button("Delete##cycle")) {
        if (deleteCycle(editor.doc, timeline.activeCycle, frameCount)) {
            resyncFrames(editor);
            selectCycle(editor, -1);
        }
    }

    // The loop mode belongs to the cycle, not to the editor: "hurt" plays once
    // and "walk" loops, in the same document, at the same time.
    ImGui::SameLine();
    ImGui::TextColored(c.textDim, "when it ends");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(112.f);
    if (timeline.activeCycle >= 0) {
        // A copy: choosing a mode re-reads the cycle list under a reference.
        const Cycle cycle = editor.cycles[static_cast<size_t>(timeline.activeCycle)];
        if (ImGui::BeginCombo("##loop", loopName(cycle.loop))) {
            for (LoopMode mode : { LoopMode::Loop, LoopMode::Once, LoopMode::PingPong }) {
                if (ImGui::Selectable(loopName(mode), mode == cycle.loop)) {
                    setCycleLoop(editor.doc, timeline.activeCycle, mode, frameCount);
                    resyncFrames(editor);
                }
            }
            ImGui::EndCombo();
        }
    } else {
        ImGui::BeginDisabled(true);
        ImGui::BeginCombo("##loop", "Loop", ImGuiComboFlags_NoArrowButton);
        ImGui::EndDisabled();
    }
    ImGui::EndDisabled();

    // How long this cycle runs, which is not the same as how long the frames
    // add up to: a cycle can leave frames out, or play one twice.
    ImGui::SameLine();
    const Cycle playing = activeCycle(editor);
    const int length = cycleDurationMs(editor.frames, playing);
    const std::string summary = std::to_string(playing.frames.size()) + " steps, " +
                                timingOf(length);
    ImGui::TextColored(c.textDim, "%s", summary.c_str());

    if (ImGui::BeginPopup("rename cycle")) {
        ImGui::SetNextItemWidth(200.f);
        const bool done = ImGui::InputText("##name", timeline.cycleNameBuffer,
                                           sizeof(timeline.cycleNameBuffer),
                                           ImGuiInputTextFlags_EnterReturnsTrue);
        if (done || ImGui::Button("Rename##ok")) {
            if (timeline.activeCycle >= 0) {
                renameCycle(editor.doc, timeline.activeCycle,
                            timeline.cycleNameBuffer, frameCount);
                resyncFrames(editor);
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

// The cycle as a sequence: one chip per step, each naming a frame.
//
// This row exists because a cycle is not a subset of the frames -- it is an
// order over them, and a frame may appear in it more than once. A row of
// checkboxes on the strip could not say "0 1 2 1", which is the shape every
// four-frame walk actually has.
void drawStepRow(Editor& editor) {
    const theme::Palette& c = theme::palette();
    TimelineSettings& timeline = editor.timeline;
    if (timeline.activeCycle < 0 ||
        timeline.activeCycle >= static_cast<int>(editor.cycles.size())) {
        return;
    }
    const int frameCount = static_cast<int>(editor.frames.size());
    // A copy, not a reference: clicking a step selects a frame, which re-reads
    // the cycle list, and the next step would then be read through a list
    // that no longer exists. That was a crash on Play after enough edits to
    // make the vector move.
    const Cycle cycle = editor.cycles[static_cast<size_t>(timeline.activeCycle)];
    const int steps = static_cast<int>(cycle.frames.size());

    if (timeline.selectedStep >= steps) {
        timeline.selectedStep = steps - 1;
    }
    if (timeline.selectedStep < 0) {
        timeline.selectedStep = 0;
    }

    // Which step the clock is on, so playback can be read here as well as on
    // the strip -- with a repeated frame, the strip alone cannot say which of
    // the two passes is running.
    const int playingStep = timeline.playing
        ? cyclePositionAt(editor.frames, cycle,
                          static_cast<int64_t>(SDL_GetTicks() - timeline.startedAtMs))
        : -1;

    ImGui::BeginChild("steps", ImVec2(0.f, kStepSize + 8.f), false,
                      ImGuiWindowFlags_HorizontalScrollbar);
    ImDrawList* draw = ImGui::GetWindowDrawList();

    // Two rows of numbered boxes sitting on top of each other need telling
    // apart: the strip is what was drawn, this is the order it plays in.
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(c.textDim, "Plays");
    ImGui::SameLine(0.f, 8.f);

    for (int i = 0; i < steps; ++i) {
        ImGui::PushID(i);
        ImGui::SameLine(0.f, 4.f);
        const int frame = cycle.frames[static_cast<size_t>(i)];
        const ImVec2 chip = ImGui::GetCursorScreenPos();

        if (ImGui::InvisibleButton("step", ImVec2(kStepSize, kStepSize))) {
            timeline.playing = false;
            timeline.selectedStep = i;
            selectFrame(editor, frame);     // safe to go on: `cycle` is a copy
        }
        const bool hovered = ImGui::IsItemHovered();

        // Middle-click removes, which keeps the chip a chip rather than a chip
        // with an x on it at this size.
        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) {
            if (removeCycleStep(editor.doc, timeline.activeCycle, i, frameCount)) {
                resyncFrames(editor);
            }
            ImGui::PopID();
            break;
        }

        if (ImGui::BeginDragDropSource()) {
            ImGui::SetDragDropPayload("step", &i, sizeof(int));
            ImGui::Text("Step %d", i + 1);
            ImGui::EndDragDropSource();
        }
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("step")) {
                const int from = *static_cast<const int*>(payload->Data);
                if (moveCycleStep(editor.doc, timeline.activeCycle, from, i, frameCount)) {
                    resyncFrames(editor);
                    timeline.selectedStep = i;
                    ImGui::EndDragDropTarget();
                    ImGui::PopID();
                    break;
                }
            }
            ImGui::EndDragDropTarget();
        }
        if (hovered) {
            ImGui::SetTooltip("Step %d plays frame %d\nMiddle-click to remove, "
                              "drag to reorder", i + 1, frame + 1);
        }

        const ImVec2 corner(chip.x + kStepSize, chip.y + kStepSize);
        const bool selected = (i == timeline.selectedStep);
        draw->AddRectFilled(chip, corner,
                            ImGui::GetColorU32(selected ? c.controlActive : c.control),
                            theme::metrics().rounding);
        if (selected) {
            draw->AddRect(chip, corner, ImGui::ColorConvertFloat4ToU32(c.accent),
                          theme::metrics().rounding, 0, 2.f);
        } else if (hovered) {
            draw->AddRect(chip, corner, ImGui::GetColorU32(c.border),
                          theme::metrics().rounding, 0, 1.f);
        }
        if (i == playingStep) {
            draw->AddRectFilled(ImVec2(chip.x, corner.y - 3.f), corner,
                                ImGui::ColorConvertFloat4ToU32(c.accent));
        }

        const std::string label = std::to_string(frame + 1);
        const ImVec2 size = ImGui::CalcTextSize(label.c_str());
        draw->AddText(ImVec2(chip.x + (kStepSize - size.x) * 0.5f,
                             chip.y + (kStepSize - size.y) * 0.5f),
                      ImGui::GetColorU32(selected ? c.textBright : c.text),
                      label.c_str());
        ImGui::PopID();
    }

    // Appending the selected frame is the ordinary way a cycle is built up,
    // and it lands after the selected step rather than at the end, so a step
    // can be put in the middle of a sequence that already exists.
    ImGui::SameLine(0.f, 8.f);
    ImGui::BeginDisabled(cycle.frames.size() >= kMaxFramesPerCycle);
    if (ImGui::Button("+", ImVec2(kStepSize, kStepSize))) {
        const int at = addCycleStep(editor.doc, timeline.activeCycle,
                                    timeline.selectedStep, timeline.activeFrame,
                                    frameCount);
        if (at >= 0) {
            resyncFrames(editor);
            timeline.selectedStep = at;
        }
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Add frame %d to this cycle, after the selected step.\n"
                          "A frame may appear more than once.",
                          timeline.activeFrame + 1);
    }

    ImGui::EndChild();
}

} // namespace

float timelinePanelHeight(const Editor& editor) {
    if (!editor.timeline.visible) {
        return 0.f;
    }
    return editor.timeline.activeCycle >= 0 ? kBaseHeight + kStepsHeight : kBaseHeight;
}

void drawOnionSkin(Editor& editor, CanvasView& canvas, ImDrawList* draw,
                   ImVec2 origin, float zoom) {
    const TimelineSettings& timeline = editor.timeline;
    if (!timeline.onion || timeline.playing || editor.frames.size() < 2 ||
        draw == nullptr) {
        return;
    }
    const int active = timeline.activeFrame;
    const int count = static_cast<int>(editor.frames.size());

    // The sequence the neighbours come from: every frame, or what the
    // selected cycle plays -- where the ghosts are the frames played either
    // side of this one, which may not be its neighbours in the strip.
    std::vector<int> sequence;
    if (timeline.onionInCycle && timeline.activeCycle >= 0) {
        sequence = activeCycle(editor).frames;
    }
    if (sequence.empty()) {
        for (int i = 0; i < count; ++i) {
            sequence.push_back(i);
        }
    }
    int at = -1;
    for (size_t i = 0; i < sequence.size(); ++i) {
        if (sequence[i] == active) {
            at = static_cast<int>(i);
            break;
        }
    }
    if (at < 0) {
        return;
    }
    const int length = static_cast<int>(sequence.size());
    const auto neighbour = [&](int offset) {
        int position = at + offset;
        if (timeline.onionWraps) {
            position = ((position % length) + length) % length;
        }
        if (position < 0 || position >= length || position == at) {
            return -1;
        }
        return sequence[static_cast<size_t>(position)];
    };

    // Behind in one colour, ahead in another, so a person can tell which way
    // the motion goes without counting. Both are faint enough to stay under
    // the live artwork rather than competing with it.
    const auto ghost = [&](int index, bool ahead, int distance) {
        if (index < 0 || index >= count) {
            return;
        }
        // Only what is already compiled. An onion skin is a convenience, and
        // paying a compile for one would undo the reason it is affordable.
        const FrameCache::Entry* entry =
            canvas.frames().cachedEntry(editor.frames[static_cast<size_t>(index)].sprite);
        if (entry == nullptr) {
            return;
        }
        const int alpha = std::max(24, 96 / distance);
        const float* colour = ahead ? timeline.onionAhead : timeline.onionBehind;
        const ImU32 tint = IM_COL32(static_cast<int>(colour[0] * 255.f),
                                    static_cast<int>(colour[1] * 255.f),
                                    static_cast<int>(colour[2] * 255.f), alpha);
        canvas.drawFrameTinted(draw, *entry, origin, zoom, tint);
    };

    // Furthest first, so the nearest neighbour ends up on top.
    for (int i = timeline.onionBefore; i >= 1; --i) { ghost(neighbour(-i), false, i); }
    for (int i = timeline.onionAfter;  i >= 1; --i) { ghost(neighbour(i), true,  i); }
}

void drawTimelinePanel(Editor& editor, CanvasView& canvas) {
    if (!editor.timeline.visible) {
        return;
    }
    const theme::Palette& c = theme::palette();
    TimelineSettings& timeline = editor.timeline;

    // ------------------------------------------------------------ controls --
    if (ImGui::Button(timeline.playing ? "Stop" : "Play", ImVec2(56.f, 0.f))) {
        timeline.playing = !timeline.playing;
        timeline.startedAtMs = SDL_GetTicks();
        if (!timeline.playing) {
            // Stopping leaves you on the frame that was showing, which is
            // almost always the one you stopped to look at.
            selectFrame(editor, frameToShow(editor, SDL_GetTicks()));
        }
    }
    ImGui::SameLine();

    const bool busy = editor.busy();
    int first = 0;
    int last = 0;
    const bool ranged = frameRange(editor, &first, &last);
    ImGui::BeginDisabled(busy || timeline.playing);
    if (ImGui::Button("+ Frame")) {
        const int at = ranged ? duplicateFrames(editor.doc, first, last)
                              : duplicateFrame(editor.doc, timeline.activeFrame);
        if (at >= 0) {
            resyncFrames(editor);
            timeline.rangeAnchor = ranged ? at : -1;
            selectFrame(editor, ranged ? at + (last - first) : at);
            editor.say(ranged ? "Duplicated " + std::to_string(last - first + 1) + " frames"
                              : "Duplicated frame " + std::to_string(timeline.activeFrame));
        } else {
            editor.say("Could not add a frame");
        }
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Duplicate this frame, or the selected run  (Ctrl+Shift+D)\n"
                          "The copy owns its own drawing.");
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!tracksOn(editor.doc) || ranged);
    if (ImGui::Button("+ Linked")) {
        // A copy whose every layer is the same cel as the original's: drawing
        // on either draws on both.
        editor.doc.beginAction("Duplicate frame, linked");
        const int source = timeline.activeFrame;
        const int at = duplicateFrame(editor.doc, source);
        if (at >= 0) {
            const std::vector<Frame> frames = readFrames(editor.doc);
            const ls::SpriteId from = frames[static_cast<size_t>(source)].sprite;
            const ls::SpriteId copy = frames[static_cast<size_t>(at)].sprite;
            for (ls::LayerId layer : layerOrder(editor.doc, from)) {
                linkCels(editor.doc, layer, { copy });
            }
            editor.doc.endAction();
            resyncFrames(editor);
            selectFrame(editor, at);
            editor.say("A linked copy: drawing on one draws on both");
        } else {
            editor.doc.abandonAction();
            editor.say("Could not add a frame");
        }
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("Duplicate this frame with every layer linked to the original: "
                          "the same cels, so a stroke on either lands on both. Unlink a "
                          "layer from its row's menu.");
    }
    ImGui::SameLine();
    if (ImGui::Button("+ Empty")) {
        const int at = addEmptyFrame(editor, timeline.activeFrame);
        if (at >= 0) {
            resyncFrames(editor);
            selectFrame(editor, at);
        }
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(editor.frames.size() <= 1);
    if (ImGui::Button("Delete")) {
        const int from = ranged ? first : timeline.activeFrame;
        const int to = ranged ? last : timeline.activeFrame;
        if (deleteFrames(editor.doc, from, to)) {
            timeline.rangeAnchor = -1;
            resyncFrames(editor);
            selectFrame(editor, from - 1 < 0 ? 0 : from - 1);
        } else if (ranged) {
            editor.say("The last frame stays; leave at least one");
        }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!ranged);
    if (ImGui::Button("Reverse")) {
        if (reverseFrames(editor.doc, first, last)) {
            resyncFrames(editor);
            editor.say("Reversed frames " + std::to_string(first + 1) + " to " +
                       std::to_string(last + 1) + "; every cycle still plays the same pictures");
        }
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip(ranged ? "Put the selected frames in the opposite order."
                                 : "Shift+click a second frame to select a run to reverse.");
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!ranged || last - first < 2 || !tracksOn(editor.doc) ||
                         editor.active() == nullptr);
    if (ImGui::Button("Tween")) {
        ImGui::OpenPopup("##tween");
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("The selected layer's transforms, from the first frame of the run\n"
                          "to the last, with every frame between getting the values in\n"
                          "between: rotate, scale, offset and pivot. Give the two end\n"
                          "frames the same transforms first. Shift+click selects a run.");
    }
    if (ImGui::BeginPopup("##tween")) {
        const struct { TweenEasing easing; const char* name; } easings[] = {
            { TweenEasing::Linear, "Linear" },
            { TweenEasing::EaseInOut, "Ease in and out" },
        };
        for (const auto& e : easings) {
            if (ImGui::Selectable(e.name)) {
                std::vector<ls::SpriteId> run;
                for (int i = first; i <= last; ++i) {
                    run.push_back(editor.frames[static_cast<size_t>(i)].sprite);
                }
                const std::string key = trackKey(editor.doc, editor.active()->layer);
                std::string why;
                editor.doc.beginAction("Tween");
                if (tweenTransforms(editor.doc, run, key, e.easing, &why)) {
                    editor.doc.endAction();
                    resyncFrames(editor);
                    editor.say("Tweened frames " + std::to_string(first + 1) + " to " +
                               std::to_string(last + 1));
                } else {
                    editor.doc.abandonAction();
                    editor.say("No tween: " + why);
                }
            }
        }
        ImGui::EndPopup();
    }
    ImGui::EndDisabled();

    // The duration of the frame being looked at. One number, on the frame it
    // belongs to, rather than a global frame rate -- a hold at the top of a
    // jump is the most ordinary thing in a sprite animation.
    ImGui::SameLine();
    ImGui::SetNextItemWidth(96.f);
    if (timeline.activeFrame < static_cast<int>(editor.frames.size())) {
        int held = editor.frames[static_cast<size_t>(timeline.activeFrame)].durationMs;
        if (ImGui::DragInt("##hold", &held, 5.f, kMinFrameMs, kMaxFrameMs, "%d ms")) {
            // Driven live, then bracketed on release like every other slider,
            // so a drag through forty values is one undo step. With a run
            // selected it is the hold of every frame in it.
            if (ranged) {
                setFramesDuration(editor.doc, first, last, held);
            } else {
                setFrameDuration(editor.doc, timeline.activeFrame, held);
            }
            resyncFrames(editor);
        }
        if (ImGui::IsItemHovered() && ranged) {
            ImGui::SetTooltip("The hold of every selected frame");
        }
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(70.f);
    ImGui::DragFloat("##speed", &timeline.speed, 0.01f, 0.25f, 4.f, "%.2fx");
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Playback speed: a preview at half speed to judge the "
                          "timing, or faster to see the motion. The holds are "
                          "not changed.");
    }
    ImGui::SameLine();
    ImGui::Checkbox("Onion", &timeline.onion);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("The frames either side, faint, under this one.\n"
                          "Behind in warm, ahead in cool.");
    }
    ImGui::SameLine(0.f, 2.f);
    if (ImGui::SmallButton("v##onion")) {
        ImGui::OpenPopup("onion-settings");
    }
    if (ImGui::BeginPopup("onion-settings")) {
        ImGui::SetNextItemWidth(120.f);
        ImGui::SliderInt("behind", &timeline.onionBefore, 0, 4);
        ImGui::SetNextItemWidth(120.f);
        ImGui::SliderInt("ahead", &timeline.onionAfter, 0, 4);
        ImGui::Checkbox("Neighbours in the cycle", &timeline.onionInCycle);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("The frames the selected cycle plays either side of "
                              "this one, rather than its neighbours in the strip.");
        }
        ImGui::Checkbox("Wrap at the ends", &timeline.onionWraps);
        ImGui::ColorEdit3("behind##tint", timeline.onionBehind, ImGuiColorEditFlags_NoInputs);
        ImGui::SameLine();
        ImGui::ColorEdit3("ahead##tint", timeline.onionAhead, ImGuiColorEditFlags_NoInputs);
        ImGui::EndPopup();
    }

    ImGui::SameLine();
    {
        // The total, so a person knows what they have made without adding it up.
        int total = 0;
        for (const Frame& frame : editor.frames) {
            total += frame.durationMs;
        }
        const std::string summary =
            std::to_string(editor.frames.size()) + " frames, " + timingOf(total);
        ImGui::TextColored(c.textDim, "%s", summary.c_str());
    }

    // -------------------------------------------------------------- cycle --
    drawCycleControls(editor);

    // -------------------------------------------------------------- strip --
    ImGui::Separator();

    // What the cycle plays, so the strip can say which frames are in it and
    // how many times each is used. A frame in no cycle is not a mistake -- an
    // in-between kept for later is an ordinary thing to have -- so it is
    // marked quietly rather than flagged.
    const Cycle playing = activeCycle(editor);
    std::vector<int> usage(editor.frames.size(), 0);
    for (int step : playing.frames) {
        if (step >= 0 && step < static_cast<int>(usage.size())) {
            ++usage[static_cast<size_t>(step)];
        }
    }

    const float stripHeight = timeline.activeCycle >= 0
        ? -(kStepSize + 12.f)
        : 0.f;
    ImGui::BeginChild("strip", ImVec2(0.f, stripHeight), false,
                      ImGuiWindowFlags_HorizontalScrollbar);

    const int showing = frameToShow(editor, SDL_GetTicks());
    ImDrawList* draw = ImGui::GetWindowDrawList();
    int compiled = 0;

    for (int i = 0; i < static_cast<int>(editor.frames.size()); ++i) {
        const Frame frame = editor.frames[static_cast<size_t>(i)];   // a copy; see drawStepRow
        ImGui::PushID(i);
        if (i != 0) {
            ImGui::SameLine();
        }

        const ImVec2 cell = ImGui::GetCursorScreenPos();
        const bool selected = (i == timeline.activeFrame);
        const bool playhead = (i == showing);

        if (ImGui::InvisibleButton("cell", ImVec2(kThumbSize, kThumbSize + 16.f))) {
            timeline.playing = false;
            // Shift+click selects the run from the frame already selected to
            // this one; a plain click selects this frame alone.
            if (ImGui::GetIO().KeyShift) {
                if (timeline.rangeAnchor < 0) {
                    timeline.rangeAnchor = timeline.activeFrame;
                }
            } else {
                timeline.rangeAnchor = -1;
            }
            selectFrame(editor, i);         // safe to go on: `frame` is a copy
        }
        const bool hovered = ImGui::IsItemHovered();

        // Dragging a frame onto another reorders them. The cycles that name
        // them are renumbered with it, so a cycle still plays the same
        // pictures in the same order afterwards.
        if (ImGui::BeginDragDropSource()) {
            ImGui::SetDragDropPayload("frame", &i, sizeof(int));
            ImGui::Text("Frame %d", i + 1);
            ImGui::EndDragDropSource();
        }
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("frame")) {
                const int from = *static_cast<const int*>(payload->Data);
                if (moveFrame(editor.doc, from, i)) {
                    resyncFrames(editor);
                    selectFrame(editor, i);
                    ImGui::EndDragDropTarget();
                    ImGui::PopID();
                    break;
                }
            }
            ImGui::EndDragDropTarget();
        }

        const ImVec2 corner(cell.x + kThumbSize, cell.y + kThumbSize);
        draw->AddRectFilled(cell, corner, ImGui::GetColorU32(c.canvasBackground),
                            theme::metrics().rounding);
        // A frame in the selected run: a band under its cell.
        if (ranged && i >= first && i <= last) {
            draw->AddRectFilled(ImVec2(cell.x - 2.f, cell.y - 2.f),
                                ImVec2(corner.x + 2.f, corner.y + 18.f),
                                ImGui::ColorConvertFloat4ToU32(
                                    ImVec4(c.accent.x, c.accent.y, c.accent.z, 0.18f)),
                                theme::metrics().rounding);
        }

        // The picture. A frame that is already current costs nothing to ask
        // for; one that is stale or missing costs a compile, and only a couple
        // of those are allowed per pass.
        const FrameCache::Entry* entry = canvas.frames().cachedEntry(frame.sprite);
        auto stale = editor.doc.engine().isDirty(frame.sprite.value);
        const bool needsWork = entry == nullptr || !stale.ok() || stale.value;

        if (!needsWork) {
            // Clean and held: a lookup, nothing more.
        } else if (compiled < kCompileBudget) {
            entry = canvas.frames().entryFor(editor.doc, frame.sprite);
            ++compiled;
        }
        // else: draw the picture it already has, one pass behind. Null only
        // for a frame nothing has ever compiled, which draws as an empty cell
        // until its turn comes round.

        if (entry != nullptr) {
            drawThumbnail(draw, canvas, *entry, cell, kThumbSize);
        }

        // The selected frame gets the accent ring; the playhead gets a bar
        // under it. They are different marks because during playback they are
        // different frames, and conflating them makes playback look like it is
        // changing the selection.
        if (selected) {
            draw->AddRect(cell, corner,
                          ImGui::ColorConvertFloat4ToU32(c.accent),
                          theme::metrics().rounding, 0, 2.f);
        } else if (hovered) {
            draw->AddRect(cell, corner, ImGui::GetColorU32(c.border),
                          theme::metrics().rounding, 0, 1.f);
        }
        if (playhead && timeline.playing) {
            draw->AddRectFilled(ImVec2(cell.x, corner.y + 2.f),
                                ImVec2(corner.x, corner.y + 5.f),
                                ImGui::ColorConvertFloat4ToU32(c.accent));
        }

        // A dot per time this cycle plays the frame, in the corner of the
        // cell: present, twice, or absent, readable without counting chips.
        if (timeline.activeCycle >= 0) {
            const int times = usage[static_cast<size_t>(i)];
            if (times == 0) {
                // Dimmed rather than marked: the frame is still a frame.
                draw->AddRectFilled(cell, corner, IM_COL32(0, 0, 0, 120),
                                    theme::metrics().rounding);
            }
            for (int dot = 0; dot < times && dot < 4; ++dot) {
                draw->AddCircleFilled(
                    ImVec2(corner.x - 6.f - static_cast<float>(dot) * 7.f, cell.y + 6.f),
                    2.5f, ImGui::ColorConvertFloat4ToU32(c.accent));
            }
        }

        const std::string label = labelOf(frame, i);
        draw->AddText(ImVec2(cell.x + 3.f, corner.y + 6.f),
                      ImGui::GetColorU32(selected ? c.textBright : c.textDim),
                      label.c_str());

        // The hold, right-aligned under the cell, so an uneven timing is
        // visible without clicking each frame to find it.
        const std::string held = timingOf(frame.durationMs);
        const float width = ImGui::CalcTextSize(held.c_str()).x;
        draw->AddText(ImVec2(corner.x - width - 3.f, corner.y + 6.f),
                      ImGui::GetColorU32(c.textDim), held.c_str());

        ImGui::PopID();
    }

    ImGui::EndChild();

    // ------------------------------------------------------------- steps --
    drawStepRow(editor);
}


// ---------------------------------------------------------------- the sheet --

void drawAnimationPanel(Editor& editor, SDL_Window* window) {
    if (!editor.animationPanelOpen) {
        return;
    }
    const theme::Palette& c = theme::palette();
    AnimationSettings& settings = editor.animation;

    ImGui::OpenPopup("Export animation");
    ImGui::SetNextWindowSize(ImVec2(430.f, 0.f), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal("Export animation", &editor.animationPanelOpen,
                                ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    theme::sectionHeader("FORMAT");
    const char* formats[] = { "GIF", "Animated PNG", "PNG sequence", "WebP" };
    int format = static_cast<int>(settings.format);
    ImGui::SetNextItemWidth(160.f);
    if (ImGui::Combo("##format", &format, formats, 4)) {
        settings.format = static_cast<AnimationFormat>(format);
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(110.f);
    int scale = static_cast<int>(settings.scale);
    if (ImGui::DragInt("##scale", &scale, 0.1f, 1,
                       static_cast<int>(ExportSettings::kMaxScale), "%dx")) {
        settings.scale = static_cast<uint32_t>(scale);
    }
    ImGui::PushStyleColor(ImGuiCol_Text, c.textDim);
    ImGui::TextWrapped("%s",
        settings.format == AnimationFormat::Gif
            ? "Plays everywhere. 256 colours a frame and no partial "
              "transparency: a frame with more colours is reduced, and you are "
              "told when it is."
        : settings.format == AnimationFormat::Apng
            ? "Every colour and every level of transparency, exactly. Browsers "
              "and most viewers play it; some older tools show the first frame."
        : settings.format == AnimationFormat::Webp
            ? "Lossless WebP: every colour and level of transparency, exactly, "
              "and usually smaller than the PNG. Browsers and chat apps play it."
            : "One numbered PNG per step, for tools that assemble their own.");
    ImGui::PopStyleColor();

    // What plays. The selected cycle's steps in its loop mode -- ping-pong
    // there and back, once without looping -- or every frame, looping.
    const bool haveCycle = editor.timeline.activeCycle >= 0 &&
                           editor.timeline.activeCycle <
                               static_cast<int>(editor.cycles.size());
    if (!haveCycle) {
        editor.animationFromCycle = false;
    }
    ImGui::Dummy(ImVec2(0.f, theme::metrics().itemSpacing));
    theme::sectionHeader("WHAT PLAYS");
    ImGui::BeginDisabled(!haveCycle);
    if (ImGui::RadioButton("The selected cycle", editor.animationFromCycle)) {
        editor.animationFromCycle = true;
    }
    ImGui::EndDisabled();
    if (haveCycle) {
        ImGui::SameLine();
        const Cycle& cycle = editor.cycles[static_cast<size_t>(editor.timeline.activeCycle)];
        ImGui::TextColored(c.textDim, "(%s)", cycle.name.empty() ? "unnamed" : cycle.name.c_str());
    }
    if (ImGui::RadioButton("Every frame, in order", !editor.animationFromCycle)) {
        editor.animationFromCycle = false;
    }

    const Cycle cycle = editor.animationFromCycle
        ? activeCycle(editor) : everyFrame(static_cast<int>(editor.frames.size()));
    const std::vector<int> steps = stepsToPlay(cycle);
    int total = 0;
    for (int step : steps) {
        if (step >= 0 && step < static_cast<int>(editor.frames.size())) {
            total += editor.frames[static_cast<size_t>(step)].durationMs;
        }
    }
    ImGui::Dummy(ImVec2(0.f, theme::metrics().sectionGap));
    ImGui::Separator();
    ImGui::TextColored(c.textBright, "%d step(s), %d ms", static_cast<int>(steps.size()), total);
    ImGui::SameLine();
    ImGui::TextColored(c.textDim, "%s", cycle.loop == LoopMode::Once ? "-- plays once"
                                                                      : "-- loops");

    ImGui::Dummy(ImVec2(0.f, theme::metrics().itemSpacing));
    ImGui::BeginDisabled(steps.empty());
    if (ImGui::Button("Choose a file...", ImVec2(150.f, 0.f))) {
        showAnimationDialog(editor.files, window, editor.doc,
                            animationExtension(settings.format));
        editor.animationPanelOpen = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(90.f, 0.f))) {
        editor.animationPanelOpen = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void drawSheetPanel(Editor& editor, SDL_Window* window) {
    if (!editor.sheetPanelOpen) {
        return;
    }
    const theme::Palette& c = theme::palette();
    SheetSettings& settings = editor.sheet;

    ImGui::OpenPopup("Export sheet");
    ImGui::SetNextWindowSize(ImVec2(430.f, 0.f), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal("Export sheet", &editor.sheetPanelOpen,
                                ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    // What goes in it. A cycle is the useful default when there is one: a sheet
    // of "walk" is a thing somebody ships, where a sheet of every frame in the
    // document usually is not.
    const bool haveCycle = editor.timeline.activeCycle >= 0 &&
                           editor.timeline.activeCycle <
                               static_cast<int>(editor.cycles.size());
    if (!haveCycle) {
        editor.sheetFromCycle = false;
    }

    theme::sectionHeader("WHAT GOES IN");
    ImGui::BeginDisabled(!haveCycle);
    if (ImGui::RadioButton("The selected cycle", editor.sheetFromCycle)) {
        editor.sheetFromCycle = true;
    }
    ImGui::EndDisabled();
    if (haveCycle) {
        ImGui::SameLine();
        const Cycle& cycle = editor.cycles[static_cast<size_t>(editor.timeline.activeCycle)];
        ImGui::TextColored(c.textDim, "(%s, %d steps)",
                           cycle.name.empty() ? "unnamed" : cycle.name.c_str(),
                           static_cast<int>(cycle.frames.size()));
    }
    if (ImGui::RadioButton("Every frame, in order", !editor.sheetFromCycle)) {
        editor.sheetFromCycle = false;
    }

    // The cells, and their order, are decided here so the summary below can be
    // honest rather than approximate.
    const std::vector<int> steps = sheetSteps(editor);

    ImGui::Dummy(ImVec2(0.f, theme::metrics().itemSpacing));
    theme::sectionHeader("ARRANGEMENT");

    const char* layouts[] = { "Grid", "One row", "One column" };
    int layout = static_cast<int>(settings.layout);
    ImGui::SetNextItemWidth(140.f);
    if (ImGui::Combo("##layout", &layout, layouts, 3)) {
        settings.layout = static_cast<SheetLayout>(layout);
    }
    if (settings.layout == SheetLayout::Grid) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(120.f);
        // Zero means choose, which is what most people want and what the label
        // has to say, or the field looks broken.
        ImGui::DragInt("##columns", &settings.columns, 0.2f, 0, 64,
                       settings.columns > 0 ? "%d columns" : "auto columns");
    }

    ImGui::SameLine();
    ImGui::SetNextItemWidth(110.f);
    int scale = static_cast<int>(settings.scale);
    if (ImGui::DragInt("##scale", &scale, 0.1f, 1,
                       static_cast<int>(ExportSettings::kMaxScale), "%dx")) {
        settings.scale = static_cast<uint32_t>(scale);
    }

    int border = static_cast<int>(settings.border);
    int spacing = static_cast<int>(settings.spacing);
    ImGui::SetNextItemWidth(110.f);
    if (ImGui::DragInt("##border", &border, 0.1f, 0, 64, "border %d")) {
        settings.border = static_cast<uint32_t>(std::max(0, border));
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(110.f);
    if (ImGui::DragInt("##spacing", &spacing, 0.1f, 0, 64, "spacing %d")) {
        settings.spacing = static_cast<uint32_t>(std::max(0, spacing));
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Empty pixels between cells, so a texture filter or a mip "
                          "level does not bleed one frame into the next.");
    }
    ImGui::Checkbox("Trim to what is drawn", &settings.trim);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Every cell cut to the smallest rectangle holding what any "
                          "frame draws -- the same for all, so they still line up. "
                          "The description says where it was cut from.");
    }

    ImGui::Dummy(ImVec2(0.f, theme::metrics().itemSpacing));
    theme::sectionHeader("BESIDE THE IMAGE");
    ImGui::Checkbox("Write a description of the sheet", &settings.writeManifest);
    if (settings.writeManifest) {
        const char* formats[] = { "Fast's own", "Aseprite JSON (hash)", "Aseprite JSON (array)" };
        int format = static_cast<int>(settings.manifestFormat);
        ImGui::SetNextItemWidth(220.f);
        if (ImGui::Combo("##manifestformat", &format, formats, 3)) {
            settings.manifestFormat = static_cast<SheetManifestFormat>(format);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Fast's own says everything, cycles and repeats included.\n"
                              "The Aseprite layouts are what game-engine importers built "
                              "for\nAseprite already read; cycles become frame tags where "
                              "they are a run of cells.");
        }
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("A small .json beside the PNG: where every cell is, how "
                          "long it is held,\nand what the cycles are. Without it a "
                          "consumer has only the picture.");
    }

    ImGui::Checkbox("One pattern across the whole sheet", &settings.patternAcrossSheet);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Off: every cell is exactly what exporting that frame "
                          "alone would give.\nOn: dithers and screens run "
                          "continuously across the sheet, so a cell\nno longer "
                          "matches what the editor showed. Nothing else can do "
                          "this;\nask for it deliberately.");
    }

    // What it will actually come out as. Shown before committing, because the
    // difference between a 3 MB sheet and a refused one is two of these numbers.
    ImGui::Dummy(ImVec2(0.f, theme::metrics().sectionGap));
    ImGui::Separator();

    auto size = editor.doc.engine().getCanvasSize(editor.doc.id());
    SheetPlan plan;
    std::string why;
    const bool workable = size.ok() &&
        planSheet(static_cast<int>(steps.size()),
                  static_cast<uint32_t>(size.value.x),
                  static_cast<uint32_t>(size.value.y), settings, &plan, &why);

    if (workable) {
        ImGui::TextColored(c.textBright, "%d x %d pixels", plan.width, plan.height);
        ImGui::SameLine();
        ImGui::TextColored(c.textDim, "-- %d cells of %d x %d, %d across",
                           plan.cells, plan.cellWidth, plan.cellHeight, plan.columns);
    } else {
        ImGui::TextColored(c.danger, "%s", why.empty() ? "nothing to write" : why.c_str());
    }

    ImGui::Dummy(ImVec2(0.f, theme::metrics().itemSpacing));
    ImGui::BeginDisabled(!workable);
    if (ImGui::Button("Choose a file...", ImVec2(150.f, 0.f))) {
        showSheetDialog(editor.files, window, editor.doc);
        editor.sheetPanelOpen = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(90.f, 0.f))) {
        editor.sheetPanelOpen = false;
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

} // namespace fast
