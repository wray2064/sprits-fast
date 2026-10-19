// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

#include "ui/timeline_drawer.h"

#include "app/i18n.h"
#include "ui/theme.h"
#include "ui/ui_script.h"
#include "ui/widgets.h"

#include <SDL3/SDL.h>
#include <imgui_internal.h>

#include <algorithm>
#include <string>

namespace fast {

namespace {

// How long the pointer must rest on the tab before the strip opens (so that
// reaching for the canvas's bottom edge does not open it), and how long it must
// be away from the strip before it tucks (so that a slip off its edge does not).
constexpr uint64_t kDwellMs = 220;
constexpr uint64_t kTuckMs = 450;

const char* const kDrawerId = "###timelinedrawer";

bool within(ImVec2 p, ImVec2 a, ImVec2 b) {
    return p.x >= a.x && p.x < b.x && p.y >= a.y && p.y < b.y;
}

// Whether the drawer's own window is in the middle of something that the pointer
// leaving must not interrupt: a frame being dragged, a field being typed into, a
// menu or a combo open from it.
bool busy(const ImGuiWindow* drawer) {
    if (drawer == nullptr) {
        return false;
    }
    const ImGuiContext& g = *GImGui;
    if (g.ActiveId != 0 && g.ActiveIdWindow != nullptr &&
        g.ActiveIdWindow->RootWindow == drawer) {
        return true;
    }
    for (int i = 0; i < g.OpenPopupStack.Size; ++i) {
        // The window that had the focus when the popup was opened is the one it
        // was opened from.
        const ImGuiWindow* from = g.OpenPopupStack[i].RestoreNavWindow;
        if (from != nullptr && from->RootWindow == drawer) {
            return true;
        }
    }
    return false;
}

} // namespace

void showTimeline(Editor& editor) {
    TimelineSettings& t = editor.timeline;
    t.visible = true;
    if (!t.pinned) {
        t.drawerOpen = true;
        t.drawerHot = false;
    }
    ImGui::MarkIniSettingsDirty();
}

void toggleTimeline(Editor& editor) {
    TimelineSettings& t = editor.timeline;
    if (t.pinned || !t.visible) {
        if (t.visible) {
            t.visible = false;
        } else {
            showTimeline(editor);
        }
    } else {
        // A drawer that is up, however it came up, is put away; one that is not is opened.
        const bool up = t.drawerOpen || t.drawerHot;
        t.drawerOpen = !up;
        t.drawerHot = false;
    }
    t.drawerSinceMs = 0;
    ImGui::MarkIniSettingsDirty();
}

void pinTimeline(Editor& editor, bool pinned) {
    TimelineSettings& t = editor.timeline;
    t.pinned = pinned;
    t.visible = true;
    t.drawerOpen = false;
    t.drawerHot = false;
    t.drawerSinceMs = 0;
    ImGui::MarkIniSettingsDirty();
}

void drawTimelineHeader(Editor& editor, bool inDrawer) {
    TimelineSettings& t = editor.timeline;
    const ImGuiStyle& style = ImGui::GetStyle();
    const char* pinLabel = tr(t.pinned ? "Unpin" : "Pin");
    const char* hideLabel = tr("Hide");
    const auto widthOf = [&](const char* label) {
        return ImGui::CalcTextSize(label).x + style.FramePadding.x * 2.f;
    };
    float needed = widthOf(pinLabel);
    if (inDrawer) {
        needed += widthOf(hideLabel) + style.ItemSpacing.x;
        theme::pushText(theme::TextStyle::Strong);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(tr("Timeline"));
        theme::popText();
        ImGui::SameLine();
    }
    ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(),
                                  ImGui::GetWindowContentRegionMax().x - needed));
    if (widgets::button(pinLabel, widgets::ButtonStyle::Ghost)) {
        pinTimeline(editor, !t.pinned);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", t.pinned
                                    ? tr("Let the timeline tuck away until it is wanted: it "
                                         "opens from a tab at the bottom of the canvas, over "
                                         "it, and the canvas keeps the whole window.")
                                    : tr("Keep the timeline docked under the canvas, where it "
                                         "stays and the canvas has the rest."));
    }
    if (inDrawer) {
        ImGui::SameLine();
        if (widgets::button(hideLabel, widgets::ButtonStyle::Ghost)) {
            t.drawerOpen = false;
            t.drawerHot = false;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", tr("Tuck the timeline away (T)."));
        }
        // The drawer's own header is set off from the strip; the panel's, which
        // is only a button, is not worth a line.
        ImGui::Separator();
    }
}

void drawTimelineDrawer(Editor& editor, ImVec2 areaPos, ImVec2 areaSize,
                        const std::function<void()>& body) {
    TimelineSettings& t = editor.timeline;
    const float k = theme::scale();
    const theme::Palette& p = theme::palette();
    const ImGuiIO& io = ImGui::GetIO();
    const uint64_t now = SDL_GetTicks();

    const float tabWidth = std::min(200.f * k, areaSize.x);
    const float tabHeight = std::min(24.f * k, areaSize.y);
    // Taller than the interface size alone makes it as that grows: the strip is
    // narrower in its own pixels, so its controls wrap onto more rows.
    const float stripHeight =
        std::min(220.f * k * (1.f + 0.6f * std::max(0.f, k - 1.f)), areaSize.y * 0.6f);
    const ImVec2 areaEnd(areaPos.x + areaSize.x, areaPos.y + areaSize.y);
    const ImVec2 stripMin(areaPos.x, areaEnd.y - stripHeight);
    const ImVec2 tabMin(areaPos.x + (areaSize.x - tabWidth) * 0.5f, areaEnd.y - tabHeight);
    const ImVec2 tabMax(tabMin.x + tabWidth, areaEnd.y);

    // --- when it is up --------------------------------------------------------------
    ImGuiWindow* drawer = ImGui::FindWindowByName(kDrawerId);
    if (t.drawerHot && !t.drawerOpen) {
        // Up because the pointer rested on the tab: it stays while the pointer is
        // over it or something in it is in use, and tucks a moment after.
        const bool over = within(io.MousePos, stripMin, areaEnd);
        if (over || busy(drawer)) {
            t.drawerSinceMs = 0;
        } else if (t.drawerSinceMs == 0) {
            t.drawerSinceMs = now;
        } else if (now - t.drawerSinceMs >= kTuckMs) {
            t.drawerHot = false;
            t.drawerSinceMs = 0;
            editor.stayAwake(500);          // on a timer, with no event: let it settle
        }
    } else if (!t.drawerOpen) {
        // Down: the pointer resting on the tab brings it up. Not while a button
        // is held, so that a stroke carried across the tab does not open it.
        const bool resting = within(io.MousePos, tabMin, tabMax) && !ImGui::IsAnyMouseDown();
        if (!resting) {
            t.drawerSinceMs = 0;
        } else if (t.drawerSinceMs == 0) {
            t.drawerSinceMs = now;
        } else if (now - t.drawerSinceMs >= kDwellMs) {
            t.drawerHot = true;
            t.drawerSinceMs = 0;
            editor.stayAwake(500);
        }
    }

    if (t.drawerOpen || t.drawerHot) {
        // --- the strip ---------------------------------------------------------------
        ImGui::SetNextWindowPos(stripMin);
        ImGui::SetNextWindowSize(ImVec2(areaSize.x, stripHeight));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.f * k, 6.f * k));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.f);
        ImGui::PushStyleColor(ImGuiCol_Border, p.border);
        const std::string title = std::string(tr("Timeline")) + kDrawerId;
        const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse |
                                       ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                                       ImGuiWindowFlags_NoSavedSettings |
                                       ImGuiWindowFlags_NoTitleBar |
                                       ImGuiWindowFlags_NoFocusOnAppearing;
        if (ImGui::Begin(title.c_str(), nullptr, flags)) {
            // Escape tucks a drawer that was kept open, as it ends every dialog.
            if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
                !ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
                t.drawerOpen = false;
                t.drawerHot = false;
            }
            drawTimelineHeader(editor, true);
            body();
        }
        ImGui::End();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(2);
        return;
    }

    // --- the tab ---------------------------------------------------------------------
    ImGui::SetNextWindowPos(tabMin);
    ImGui::SetNextWindowSize(ImVec2(tabMax.x - tabMin.x, tabMax.y - tabMin.y));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.f, 0.f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
    // The tab is shorter than a window may be by default (WindowMinSize), and a
    // window made taller than the tab hangs over the status bar below it.
    ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(1.f, 1.f));
    const ImGuiWindowFlags tabFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoDocking |
                                      ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove |
                                      ImGuiWindowFlags_NoBackground |
                                      ImGuiWindowFlags_NoFocusOnAppearing |
                                      ImGuiWindowFlags_NoScrollbar |
                                      ImGuiWindowFlags_NoScrollWithMouse;
    if (ImGui::Begin("##timelinetab", nullptr, tabFlags)) {
        const ImVec2 size(tabMax.x - tabMin.x, tabMax.y - tabMin.y);
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const bool hovered = ImGui::IsMouseHoveringRect(tabMin, tabMax);
        const float round = 5.f * k;
        draw->AddRectFilled(tabMin, tabMax, ImGui::GetColorU32(hovered ? p.controlHovered : p.raised),
                            round, ImDrawFlags_RoundCornersTop);
        // Thickness before flags (ImGui 1.92.8 swapped them): the flags alone
        // in that place are a 48-pixel outline, which fills the tab in the
        // border colour and leaves its label nothing to be read against.
        draw->AddRect(tabMin, tabMax, ImGui::GetColorU32(hovered ? p.accent : p.border), round,
                      1.f, ImDrawFlags_RoundCornersTop);
        // A chevron up: the strip rises from here.
        const float cx = tabMin.x + 14.f * k;
        const float cy = tabMin.y + size.y * 0.5f;
        const float r = 4.f * k;
        draw->AddTriangleFilled(ImVec2(cx - r, cy + r * 0.6f), ImVec2(cx + r, cy + r * 0.6f),
                                ImVec2(cx, cy - r * 0.7f),
                                ImGui::GetColorU32(hovered ? p.accent : p.textDim));
        if (ImGui::InvisibleButton("##timelinetabbutton", size)) {
            t.drawerOpen = true;
            t.drawerHot = false;
        }
        nameForScripts("timeline tab");
        // Which frame, and whether it plays: the least the strip would say.
        std::string frame = std::to_string(t.activeFrame + 1) + " / " +
                            std::to_string(editor.frames.size());
        if (t.playing) {
            frame += std::string("  ") + tr("playing");
        }
        theme::pushText(theme::TextStyle::Label);
        const float textY = tabMin.y + (size.y - ImGui::GetTextLineHeight()) * 0.5f;
        draw->AddText(ImVec2(tabMin.x + 28.f * k, textY),
                      ImGui::GetColorU32(hovered ? p.textBright : p.text), tr("Timeline"));
        const float labelWidth = ImGui::CalcTextSize(tr("Timeline")).x;
        theme::popText();
        draw->AddText(ImVec2(tabMin.x + 28.f * k + labelWidth + 10.f * k, textY),
                      ImGui::GetColorU32(t.playing ? p.accentText : p.textMuted), frame.c_str());
        if (hovered) {
            ImGui::SetTooltip("%s", tr("Click, or press T, to keep the timeline open. Pin puts "
                                       "it in the dock."));
        }
    }
    ImGui::End();
    ImGui::PopStyleVar(3);
}

} // namespace fast
