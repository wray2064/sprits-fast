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

#include <algorithm>
#include <cstdio>
#include <string>

namespace fast {
namespace {

constexpr float kThumbSize   = 56.f;   // the cell, in screen pixels
constexpr float kStripHeight = 134.f;

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

} // namespace

void drawOnionSkin(Editor& editor, CanvasView& canvas, ImDrawList* draw,
                   ImVec2 origin, float zoom) {
    const TimelineSettings& timeline = editor.timeline;
    if (!timeline.onion || timeline.playing || editor.frames.size() < 2 ||
        draw == nullptr) {
        return;
    }
    const int active = timeline.activeFrame;
    const int count = static_cast<int>(editor.frames.size());

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
        const ImU32 tint = ahead ? IM_COL32(120, 200, 255, alpha)
                                 : IM_COL32(255, 140, 90, alpha);
        canvas.drawFrameTinted(draw, *entry, origin, zoom, tint);
    };

    // Furthest first, so the nearest neighbour ends up on top.
    for (int i = timeline.onionBefore; i >= 1; --i) { ghost(active - i, false, i); }
    for (int i = timeline.onionAfter;  i >= 1; --i) { ghost(active + i, true,  i); }
}

void drawTimelinePanel(Editor& editor, CanvasView& canvas) {
    if (!editor.timeline.visible) {
        return;
    }
    const theme::Palette& c = theme::palette();
    TimelineSettings& timeline = editor.timeline;

    ImGui::BeginChild("timeline", ImVec2(0.f, kStripHeight), true);

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
    ImGui::BeginDisabled(busy || timeline.playing);
    if (ImGui::Button("+ Frame")) {
        const int at = duplicateFrame(editor.doc, timeline.activeFrame);
        if (at >= 0) {
            resyncFrames(editor);
            selectFrame(editor, at);
            editor.say("Duplicated frame " + std::to_string(timeline.activeFrame));
        } else {
            editor.say("Could not add a frame");
        }
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Duplicate this frame  (Ctrl+Shift+D)\n"
                          "The copy owns its own drawing.");
    }
    ImGui::SameLine();
    if (ImGui::Button("+ Empty")) {
        const int at = addFrame(editor.doc, timeline.activeFrame);
        if (at >= 0) {
            resyncFrames(editor);
            selectFrame(editor, at);
        }
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(editor.frames.size() <= 1);
    if (ImGui::Button("Delete")) {
        if (deleteFrame(editor.doc, timeline.activeFrame)) {
            const int wanted = timeline.activeFrame - 1;
            resyncFrames(editor);
            selectFrame(editor, wanted < 0 ? 0 : wanted);
            canvas.frames().retainOnly(
                editor.doc.engine().getDocumentInfo(editor.doc.id()).value.sprites);
        }
    }
    ImGui::EndDisabled();
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
            // so a drag through forty values is one undo step.
            setFrameDuration(editor.doc, timeline.activeFrame, held);
            resyncFrames(editor);
        }
    }
    ImGui::SameLine();
    ImGui::Checkbox("Onion", &timeline.onion);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("The frames either side, faint, under this one.\n"
                          "Behind in warm, ahead in cool.");
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

    // -------------------------------------------------------------- strip --
    ImGui::Separator();
    ImGui::BeginChild("strip", ImVec2(0.f, 0.f), false,
                      ImGuiWindowFlags_HorizontalScrollbar);

    const int showing = frameToShow(editor, SDL_GetTicks());
    ImDrawList* draw = ImGui::GetWindowDrawList();
    int compiled = 0;

    for (int i = 0; i < static_cast<int>(editor.frames.size()); ++i) {
        const Frame& frame = editor.frames[static_cast<size_t>(i)];
        ImGui::PushID(i);
        if (i != 0) {
            ImGui::SameLine();
        }

        const ImVec2 cell = ImGui::GetCursorScreenPos();
        const bool selected = (i == timeline.activeFrame);
        const bool playhead = (i == showing);

        if (ImGui::InvisibleButton("cell", ImVec2(kThumbSize, kThumbSize + 16.f))) {
            timeline.playing = false;
            selectFrame(editor, i);
        }
        const bool hovered = ImGui::IsItemHovered();

        // Dragging a frame onto another reorders them. The cycles that name
        // them are renumbered with it, so a cycle still plays the same
        // pictures in the same order afterwards.
        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceNoPreviewTooltip)) {
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
                }
            }
            ImGui::EndDragDropTarget();
        }

        const ImVec2 corner(cell.x + kThumbSize, cell.y + kThumbSize);
        draw->AddRectFilled(cell, corner, ImGui::GetColorU32(c.canvasBackground),
                            theme::metrics().rounding);

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
    ImGui::EndChild();
}

} // namespace fast
