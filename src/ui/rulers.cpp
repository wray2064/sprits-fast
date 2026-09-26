// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

#include "ui/rulers.h"

#include "app/guides.h"
#include "ui/theme.h"

#include <cmath>
#include <cstdio>

namespace fast {

namespace {

constexpr float kThick = 16.f;
constexpr float kGrab = 5.f;

const ImU32 kGuideColour = IM_COL32(60, 200, 255, 190);

// The step between labelled ticks, in canvas pixels: the smallest of 1, 2, 5,
// 10, 20, 50 ... that leaves room for a label.
int majorStep(float zoom) {
    const int steps[] = { 1, 2, 5, 10, 20, 50, 100, 200, 500, 1000, 2000, 5000 };
    for (int step : steps) {
        if (static_cast<float>(step) * zoom >= 48.f) {
            return step;
        }
    }
    return 10000;
}

} // namespace

void drawRulers(Editor& editor, CanvasView& canvas) {
    if (!editor.rulersOn) {
        return;
    }
    const ImVec2 tl = canvas.viewTopLeft();
    const ImVec2 size = canvas.viewSize();
    const float zoom = canvas.zoom();
    if (size.x <= kThick * 2.f || size.y <= kThick * 2.f || zoom <= 0.f) {
        return;
    }
    const ls::Rect2f seen = canvas.visibleArea();
    const auto screenX = [&](float cx) { return tl.x + (cx - seen.min.x) * zoom; };
    const auto screenY = [&](float cy) { return tl.y + (cy - seen.min.y) * zoom; };
    const auto canvasX = [&](float sx) { return seen.min.x + (sx - tl.x) / zoom; };
    const auto canvasY = [&](float sy) { return seen.min.y + (sy - tl.y) / zoom; };

    const theme::Palette& c = theme::palette();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImU32 background = ImGui::GetColorU32(c.panelBackground);
    const ImU32 ink = ImGui::GetColorU32(c.textDim);
    draw->AddRectFilled(tl, ImVec2(tl.x + size.x, tl.y + kThick), background);
    draw->AddRectFilled(ImVec2(tl.x, tl.y + kThick), ImVec2(tl.x + kThick, tl.y + size.y), background);
    draw->AddLine(ImVec2(tl.x + kThick, tl.y + kThick), ImVec2(tl.x + size.x, tl.y + kThick),
                  ImGui::GetColorU32(c.border));
    draw->AddLine(ImVec2(tl.x + kThick, tl.y + kThick), ImVec2(tl.x + kThick, tl.y + size.y),
                  ImGui::GetColorU32(c.border));

    // Ticks: labelled every major step, short ones between.
    const int major = majorStep(zoom);
    const int minor = major >= 10 ? major / 5 : (major >= 2 ? 1 : 0);
    const auto ticks = [&](bool across) {
        const float from = across ? seen.min.x : seen.min.y;
        const float to = across ? seen.max.x : seen.max.y;
        const int step = minor > 0 ? minor : major;
        int v = static_cast<int>(std::floor(from / static_cast<float>(step))) * step;
        draw->PushClipRect(across ? ImVec2(tl.x + kThick, tl.y) : ImVec2(tl.x, tl.y + kThick),
                           across ? ImVec2(tl.x + size.x, tl.y + kThick)
                                  : ImVec2(tl.x + kThick, tl.y + size.y), true);
        for (; static_cast<float>(v) <= to; v += step) {
            const bool labelled = v % major == 0;
            const float len = labelled ? kThick * 0.6f : kThick * 0.25f;
            if (across) {
                const float x = screenX(static_cast<float>(v));
                draw->AddLine(ImVec2(x, tl.y + kThick - len), ImVec2(x, tl.y + kThick), ink);
                if (labelled) {
                    char text[16];
                    std::snprintf(text, sizeof(text), "%d", v);
                    draw->AddText(ImGui::GetFont(), 10.f, ImVec2(x + 2.f, tl.y), ink, text);
                }
            } else {
                const float y = screenY(static_cast<float>(v));
                draw->AddLine(ImVec2(tl.x + kThick - len, y), ImVec2(tl.x + kThick, y), ink);
                if (labelled) {
                    char text[16];
                    std::snprintf(text, sizeof(text), "%d", v);
                    draw->AddText(ImGui::GetFont(), 10.f, ImVec2(tl.x + 1.f, y + 1.f), ink, text);
                }
            }
        }
        draw->PopClipRect();
    };
    ticks(true);
    ticks(false);

    // Where the pointer is, on both.
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const ImU32 accent = ImGui::GetColorU32(c.accent);
    if (mouse.x > tl.x + kThick && mouse.x < tl.x + size.x) {
        draw->AddLine(ImVec2(mouse.x, tl.y), ImVec2(mouse.x, tl.y + kThick), accent);
    }
    if (mouse.y > tl.y + kThick && mouse.y < tl.y + size.y) {
        draw->AddLine(ImVec2(tl.x, mouse.y), ImVec2(tl.x + kThick, mouse.y), accent);
    }

    // Guides' markers: a line across marks the left ruler, one down marks
    // the top.
    std::vector<Guide> guides = readGuides(editor.doc);
    for (const Guide& g : guides) {
        if (g.vertical) {
            const float x = screenX(static_cast<float>(g.at));
            if (x > tl.x + kThick && x < tl.x + size.x) {
                draw->AddTriangleFilled(ImVec2(x - 4.f, tl.y + kThick - 7.f),
                                        ImVec2(x + 4.f, tl.y + kThick - 7.f),
                                        ImVec2(x, tl.y + kThick), kGuideColour);
            }
        } else {
            const float y = screenY(static_cast<float>(g.at));
            if (y > tl.y + kThick && y < tl.y + size.y) {
                draw->AddTriangleFilled(ImVec2(tl.x + kThick - 7.f, y - 4.f),
                                        ImVec2(tl.x + kThick - 7.f, y + 4.f),
                                        ImVec2(tl.x + kThick, y), kGuideColour);
            }
        }
    }

    // The rulers as things to press: they take the press from the canvas.
    const ImVec2 cursor = ImGui::GetCursorScreenPos();
    bool fromTop = false;
    bool fromLeft = false;
    ImGui::SetCursorScreenPos(ImVec2(tl.x + kThick, tl.y));
    ImGui::InvisibleButton("##ruler-top", ImVec2(size.x - kThick, kThick));
    fromTop = ImGui::IsItemActivated();
    const bool topActive = ImGui::IsItemActive();
    ImGui::SetCursorScreenPos(ImVec2(tl.x, tl.y + kThick));
    ImGui::InvisibleButton("##ruler-left", ImVec2(kThick, size.y - kThick));
    fromLeft = ImGui::IsItemActivated();
    const bool leftActive = ImGui::IsItemActive();
    ImGui::SetCursorScreenPos(cursor);

    if ((fromTop || fromLeft) && editor.draggingGuide < 0) {
        // A marker under the press is that guide; anywhere else, a new one --
        // across the canvas from the top ruler, down it from the left.
        int grabbed = -1;
        for (size_t i = 0; i < guides.size(); ++i) {
            const Guide& g = guides[i];
            if (fromTop && g.vertical &&
                std::fabs(screenX(static_cast<float>(g.at)) - mouse.x) <= kGrab) {
                grabbed = static_cast<int>(i);
            }
            if (fromLeft && !g.vertical &&
                std::fabs(screenY(static_cast<float>(g.at)) - mouse.y) <= kGrab) {
                grabbed = static_cast<int>(i);
            }
        }
        editor.doc.beginAction(grabbed >= 0 ? "Move guide" : "Add guide");
        if (grabbed < 0) {
            Guide made;
            made.vertical = fromLeft;
            made.at = fromLeft ? static_cast<int32_t>(std::lround(canvasX(mouse.x)))
                               : static_cast<int32_t>(std::lround(canvasY(mouse.y)));
            guides.push_back(made);
            writeGuides(editor.doc, guides);
            grabbed = static_cast<int>(guides.size()) - 1;
            editor.guideIsNew = true;
        } else {
            editor.guideIsNew = false;
        }
        editor.draggingGuide = grabbed;
        editor.guidesShown = true;
    }

    if (editor.draggingGuide >= 0) {
        const size_t index = static_cast<size_t>(editor.draggingGuide);
        if (index >= guides.size()) {
            editor.draggingGuide = -1;
            editor.doc.abandonAction();
            return;
        }
        Guide& g = guides[index];
        if (topActive || leftActive) {
            g.at = g.vertical ? static_cast<int32_t>(std::lround(canvasX(mouse.x)))
                              : static_cast<int32_t>(std::lround(canvasY(mouse.y)));
            writeGuides(editor.doc, guides);
        } else {
            // Let go: back on its ruler, it goes; a new one that never left the
            // ruler was never made.
            const bool onRuler = g.vertical ? mouse.x < tl.x + kThick : mouse.y < tl.y + kThick;
            if (onRuler) {
                if (editor.guideIsNew) {
                    editor.doc.abandonAction();
                } else {
                    guides.erase(guides.begin() + static_cast<std::ptrdiff_t>(index));
                    writeGuides(editor.doc, guides);
                    editor.doc.endAction();
                    editor.say("Guide removed");
                }
            } else {
                editor.doc.endAction();
            }
            editor.draggingGuide = -1;
        }
    }
}

void drawGuides(Editor& editor, const CanvasView& canvas, ImDrawList* draw, ImVec2 origin,
                float zoom) {
    if (!editor.guidesShown) {
        return;
    }
    const ImVec2 tl = canvas.viewTopLeft();
    const ImVec2 size = canvas.viewSize();
    for (const Guide& g : readGuides(editor.doc)) {
        if (g.vertical) {
            const float x = origin.x + static_cast<float>(g.at) * zoom;
            draw->AddLine(ImVec2(x, tl.y), ImVec2(x, tl.y + size.y), kGuideColour);
        } else {
            const float y = origin.y + static_cast<float>(g.at) * zoom;
            draw->AddLine(ImVec2(tl.x, y), ImVec2(tl.x + size.x, y), kGuideColour);
        }
    }
}

} // namespace fast
