// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

#include "ui/theme.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace fast {
namespace theme {
namespace {

constexpr ImVec4 rgb(int r, int g, int b, float a = 1.f) {
    return ImVec4(static_cast<float>(r) / 255.f, static_cast<float>(g) / 255.f,
                  static_cast<float>(b) / 255.f, a);
}

// Slate and amber.
//
// The reference is Source-era Valve: a cold, desaturated blue-grey shell with
// one hot amber running through it. The two work because they are opposites --
// the slate is cool and almost colourless, so the amber reads as light rather
// than as another surface. Warming the greys would put them in competition with
// the accent and make both look muddy.
//
// The greys are tinted blue on purpose, by a few points only. Enough that they
// read as slate rather than as neutral grey; not enough to become a colour and
// start arguing with whatever is on the canvas.
const Palette kPalette {
    /* windowBackground */ rgb(20, 24, 29),
    /* panelBackground  */ rgb(29, 34, 41),
    /* canvasBackground */ rgb(14, 17, 21),
    /* control          */ rgb(43, 51, 61),
    /* controlHovered   */ rgb(57, 68, 81),
    /* controlActive    */ rgb(70, 83, 98),
    /* border           */ rgb(50, 60, 71),
    /* text             */ rgb(190, 202, 214),
    /* textDim          */ rgb(116, 130, 146),
    /* textBright       */ rgb(228, 238, 248),
    /* accent           */ rgb(255, 158, 38),
    /* accentDim        */ rgb(255, 158, 38, 0.22f),
    /* danger           */ rgb(214, 84, 68),
    /* checkerLight     */ rgb(56, 64, 74),
    /* checkerDark      */ rgb(44, 51, 60),
};

const Metrics kMetrics {};

// Where an interface font might live. Tried in order; the first that loads wins.
// Deliberately a short list of the faces these platforms actually ship, rather
// than a hopeful sweep of every font directory.
const char* const kFontPaths[] = {
#if defined(_WIN32)
    "C:/Windows/Fonts/segoeui.ttf",
    "C:/Windows/Fonts/tahoma.ttf",
    "C:/Windows/Fonts/arial.ttf",
#elif defined(__APPLE__)
    "/System/Library/Fonts/SFNS.ttf",
    "/System/Library/Fonts/Helvetica.ttc",
#else
    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
    "/usr/share/fonts/TTF/DejaVuSans.ttf",
#endif
};

} // namespace

const Palette& palette() { return kPalette; }
const Metrics& metrics() { return kMetrics; }

void apply() {
    ImGuiStyle& style = ImGui::GetStyle();
    const Palette& c = kPalette;
    const Metrics& m = kMetrics;

    // Geometry. Slightly rounded rather than pill-shaped: this is a tool for
    // drawing on a square grid, and softness here reads as imprecision.
    style.WindowRounding    = 0.f;      // panels are docked; rounding them
    style.ChildRounding     = m.rounding;   // would leave gaps at the seams
    style.FrameRounding     = m.rounding;
    style.PopupRounding     = m.rounding;
    style.GrabRounding      = m.rounding;
    style.ScrollbarRounding = m.rounding;
    style.TabRounding       = m.rounding;

    style.WindowBorderSize  = 1.f;
    style.FrameBorderSize   = 0.f;      // borders on every input is noise
    style.PopupBorderSize   = 1.f;

    style.WindowPadding     = ImVec2(m.panelPadding, m.panelPadding);
    style.FramePadding      = ImVec2(8.f, 5.f);
    style.ItemSpacing       = ImVec2(m.itemSpacing, m.itemSpacing);
    style.ItemInnerSpacing  = ImVec2(6.f, 4.f);
    style.IndentSpacing     = 16.f;
    style.ScrollbarSize     = 11.f;
    style.GrabMinSize       = 9.f;

    style.WindowTitleAlign  = ImVec2(0.f, 0.5f);
    style.ButtonTextAlign   = ImVec2(0.5f, 0.5f);
    style.SeparatorTextBorderSize = 1.f;
    style.SeparatorTextAlign = ImVec2(0.f, 0.5f);
    style.SeparatorTextPadding = ImVec2(0.f, 8.f);

    ImVec4* colours = style.Colors;
    colours[ImGuiCol_WindowBg]             = c.panelBackground;
    colours[ImGuiCol_ChildBg]              = ImVec4(0, 0, 0, 0);
    colours[ImGuiCol_PopupBg]              = c.panelBackground;
    colours[ImGuiCol_Border]               = c.border;
    colours[ImGuiCol_BorderShadow]         = ImVec4(0, 0, 0, 0);

    colours[ImGuiCol_Text]                 = c.text;
    colours[ImGuiCol_TextDisabled]         = c.textDim;

    colours[ImGuiCol_FrameBg]              = c.control;
    colours[ImGuiCol_FrameBgHovered]       = c.controlHovered;
    colours[ImGuiCol_FrameBgActive]        = c.controlActive;

    colours[ImGuiCol_TitleBg]              = c.windowBackground;
    colours[ImGuiCol_TitleBgActive]        = c.windowBackground;
    colours[ImGuiCol_TitleBgCollapsed]     = c.windowBackground;
    colours[ImGuiCol_MenuBarBg]            = c.windowBackground;

    colours[ImGuiCol_ScrollbarBg]          = ImVec4(0, 0, 0, 0);
    colours[ImGuiCol_ScrollbarGrab]        = c.control;
    colours[ImGuiCol_ScrollbarGrabHovered] = c.controlHovered;
    colours[ImGuiCol_ScrollbarGrabActive]  = c.controlActive;

    // The accent, used only where something is selected, focused, or being
    // dragged. Everywhere else is neutral.
    colours[ImGuiCol_CheckMark]            = c.accent;
    colours[ImGuiCol_SliderGrab]           = c.accent;
    colours[ImGuiCol_SliderGrabActive]     = c.accent;

    colours[ImGuiCol_Button]               = c.control;
    colours[ImGuiCol_ButtonHovered]        = c.controlHovered;
    colours[ImGuiCol_ButtonActive]         = c.controlActive;

    colours[ImGuiCol_Header]               = c.accentDim;
    colours[ImGuiCol_HeaderHovered]        = c.controlHovered;
    colours[ImGuiCol_HeaderActive]         = c.accentDim;

    colours[ImGuiCol_Separator]            = c.border;
    colours[ImGuiCol_SeparatorHovered]     = c.accentDim;
    colours[ImGuiCol_SeparatorActive]      = c.accent;

    colours[ImGuiCol_ResizeGrip]           = ImVec4(0, 0, 0, 0);
    colours[ImGuiCol_ResizeGripHovered]    = c.accentDim;
    colours[ImGuiCol_ResizeGripActive]     = c.accent;

    colours[ImGuiCol_Tab]                  = c.windowBackground;
    colours[ImGuiCol_TabHovered]           = c.control;
    colours[ImGuiCol_TabSelected]          = c.panelBackground;

    colours[ImGuiCol_TextSelectedBg]       = c.accentDim;
    colours[ImGuiCol_NavCursor]            = c.accent;
    colours[ImGuiCol_ModalWindowDimBg]     = ImVec4(0.f, 0.f, 0.f, 0.55f);
}

void loadFonts(float scale) {
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();

    const float size = 15.f * (scale > 0.f ? scale : 1.f);
    for (const char* path : kFontPaths) {
        if (io.Fonts->AddFontFromFileTTF(path, size) != nullptr) {
            return;
        }
    }
    // Nothing found. The built-in face is not pretty, but a program that starts
    // beats a program that does not.
    io.Fonts->AddFontDefault();
}

// ---------------------------------------------------------------- widgets --

void sectionHeader(const char* label) {
    // A muted amber rather than grey. It is the one place a second use of the
    // accent earns its keep: it separates the structure of the panel from its
    // contents at a glance, and it is dim enough not to pull the eye off the
    // canvas. Anything brighter here and the panel starts shouting.
    const ImVec4 muted { kPalette.accent.x, kPalette.accent.y, kPalette.accent.z,
                         0.72f };
    ImGui::Dummy(ImVec2(0.f, 2.f));
    ImGui::PushStyleColor(ImGuiCol_Text, muted);
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();

    // A hairline under it, running to the edge of the panel: the Source-era
    // habit, and it does the separating so the label does not have to be loud.
    const ImVec2 at = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    ImGui::GetWindowDrawList()->AddLine(
        ImVec2(at.x, at.y + 1.f), ImVec2(at.x + width, at.y + 1.f),
        ImGui::GetColorU32(kPalette.border));
    ImGui::Dummy(ImVec2(0.f, 3.f));
}

// ------------------------------------------------------------------ icons --
//
// Each icon is described in a unit square and scaled to wherever it is drawn, so
// one definition serves the toolbar and anything else that wants it. The shapes
// are deliberately chunky: an icon rendered at 18 pixels loses any detail
// thinner than about a tenth of its width, and a thin outline reads as grey mush.

namespace {

// A point in the unit square of an icon, placed into a real one.
struct IconSpace {
    ImVec2 at;
    float  size;
    ImVec2 operator()(float x, float y) const {
        return ImVec2(at.x + x * size, at.y + y * size);
    }
};

// A quad along an axis, used for the barrel of the pencil and the dropper. The
// axis runs from `from` to `to`; `halfWidth` is measured across it.
void axisQuad(ImDrawList* draw, const IconSpace& s, ImVec2 from, ImVec2 to,
              float halfWidth, ImU32 colour) {
    const float dx = to.x - from.x;
    const float dy = to.y - from.y;
    const float length = std::sqrt(dx * dx + dy * dy);
    if (length <= 0.f) {
        return;
    }
    const float px = -dy / length * halfWidth;
    const float py = dx / length * halfWidth;
    draw->AddQuadFilled(s(from.x + px, from.y + py), s(to.x + px, to.y + py),
                        s(to.x - px, to.y - py), s(from.x - px, from.y - py), colour);
}

} // namespace

void drawIcon(ImDrawList* draw, Icon icon, ImVec2 at, float size, ImU32 colour) {
    const IconSpace s { at, size };

    switch (icon) {
        case Icon::Pencil: {
            // A pencil on the usual diagonal, tip toward the bottom left.
            const ImVec2 tip { 0.14f, 0.86f };
            const ImVec2 shoulder { 0.36f, 0.64f };
            const ImVec2 tail { 0.86f, 0.14f };

            axisQuad(draw, s, shoulder, tail, 0.115f, colour);

            // The sharpened point: a triangle narrowing to the tip.
            const float px = 0.115f * 0.7071f;
            draw->AddTriangleFilled(s(tip.x, tip.y),
                                    s(shoulder.x + px, shoulder.y + px),
                                    s(shoulder.x - px, shoulder.y - px), colour);

            // The ferrule, a band across the barrel near the tail. Drawn in the
            // panel colour so it reads as a gap rather than another shape.
            axisQuad(draw, s, ImVec2(0.68f, 0.32f), ImVec2(0.735f, 0.265f), 0.115f,
                     ImGui::GetColorU32(kPalette.panelBackground));
            break;
        }

        case Icon::Eraser: {
            // A block eraser, tilted. The seam between the rubber and the sleeve
            // is a hairline: any wider and the icon reads as two separate blocks
            // rather than one object, which is what a full gap did here.
            axisQuad(draw, s, ImVec2(0.22f, 0.76f), ImVec2(0.78f, 0.28f), 0.185f,
                     colour);
            draw->AddLine(s(0.36f, 0.75f), s(0.66f, 0.43f),
                          ImGui::GetColorU32(kPalette.panelBackground),
                          std::max(1.5f, size * 0.075f));
            break;
        }

        case Icon::Bucket: {
            // Upright rather than tipped. A tipped bucket needs the handle, the
            // mouth and the pour all legible at once, and at eighteen pixels it
            // simply becomes a blob; upright reads immediately.
            draw->AddQuadFilled(s(0.26f, 0.40f), s(0.72f, 0.40f),
                                s(0.64f, 0.86f), s(0.34f, 0.86f), colour);

            // The handle springs from the lip corners, so it belongs to the
            // bucket instead of floating above it.
            draw->PathClear();
            draw->PathArcTo(s(0.49f, 0.41f), 0.23f * size, 3.34f, 6.09f, 16);
            draw->PathStroke(colour, 0, std::max(1.6f, size * 0.075f));

            // A drop off the rim, because a plain trapezoid reads as a box.
            draw->AddCircleFilled(s(0.85f, 0.63f), size * 0.09f, colour, 10);
            break;
        }

        case Icon::Rectangle: {
            // Outlined rather than filled, because these tools make outlines of
            // shapes as often as they make solid ones, and a filled square is
            // hard to tell from a colour swatch at this size.
            const float weight = std::max(1.6f, size * 0.085f);
            draw->AddRect(s(0.16f, 0.24f), s(0.84f, 0.76f), colour, 0.f, 0, weight);
            break;
        }

        case Icon::Ellipse: {
            const float weight = std::max(1.6f, size * 0.085f);
            draw->AddEllipse(s(0.5f, 0.5f), ImVec2(0.34f * size, 0.26f * size),
                             colour, 0.f, 24, weight);
            break;
        }

        case Icon::Eye:
        case Icon::EyeShut: {
            // Two arcs for the lid and a pupil; shut is the lower lid alone.
            const float weight = std::max(1.5f, size * 0.08f);
            const ImVec2 centre = s(0.5f, 0.5f);
            const float rx = size * 0.36f;
            const float ry = size * 0.24f;
            draw->PathClear();
            for (int i = 0; i <= 12; ++i) {
                const float t = 3.14159265f + 3.14159265f * static_cast<float>(i) / 12.f;
                draw->PathLineTo(ImVec2(centre.x + rx * std::cos(t), centre.y - ry * std::sin(t)));
            }
            draw->PathStroke(colour, 0, weight);
            if (icon == Icon::Eye) {
                draw->PathClear();
                for (int i = 0; i <= 12; ++i) {
                    const float t = 3.14159265f * static_cast<float>(i) / 12.f;
                    draw->PathLineTo(ImVec2(centre.x + rx * std::cos(t), centre.y - ry * std::sin(t)));
                }
                draw->PathStroke(colour, 0, weight);
                draw->AddCircleFilled(centre, size * 0.11f, colour, 12);
            }
            break;
        }

        case Icon::Line: {
            const float weight = std::max(1.8f, size * 0.09f);
            draw->AddLine(s(0.18f, 0.82f), s(0.82f, 0.18f), colour, weight);
            // End points, so it reads as a segment with handles rather than as
            // a stray diagonal.
            draw->AddCircleFilled(s(0.18f, 0.82f), size * 0.085f, colour, 10);
            draw->AddCircleFilled(s(0.82f, 0.18f), size * 0.085f, colour, 10);
            break;
        }

        case Icon::Dropper: {
            // A pipette. The bulb is a circle rather than a wider quad: a quad
            // merges into the shaft and the whole thing reads as a carrot.
            draw->AddCircleFilled(s(0.735f, 0.265f), size * 0.175f, colour, 16);

            // A collar, so bulb and shaft are two parts rather than one taper.
            axisQuad(draw, s, ImVec2(0.60f, 0.40f), ImVec2(0.66f, 0.34f), 0.105f,
                     colour);
            draw->AddLine(s(0.585f, 0.415f), s(0.675f, 0.325f),
                          ImGui::GetColorU32(kPalette.panelBackground),
                          std::max(1.2f, size * 0.055f));

            axisQuad(draw, s, ImVec2(0.28f, 0.72f), ImVec2(0.60f, 0.40f), 0.062f,
                     colour);

            const float px = 0.062f * 0.7071f;
            draw->AddTriangleFilled(s(0.13f, 0.87f),
                                    s(0.28f + px, 0.72f + px),
                                    s(0.28f - px, 0.72f - px), colour);
            break;
        }
    }
}

bool toolButton(Icon icon, const char* name, const char* shortcutHint,
                bool selected, const char* description) {
    const Palette& c = kPalette;
    const float side = 30.f;

    ImGui::PushStyleColor(ImGuiCol_Button,
                          selected ? c.accentDim : ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                          selected ? c.accentDim : c.controlHovered);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, c.controlActive);

    ImGui::PushID(name);
    const ImVec2 at = ImGui::GetCursorScreenPos();
    const bool pressed = ImGui::Button("##tool", ImVec2(side, side));
    const bool hovered = ImGui::IsItemHovered();
    ImGui::PopID();
    ImGui::PopStyleColor(3);

    // The icon carries the selected state as well as the background does, which
    // matters because an accent-tinted background is subtle at a glance.
    const float inset = side * 0.18f;
    drawIcon(ImGui::GetWindowDrawList(), icon,
             ImVec2(at.x + inset, at.y + inset), side - inset * 2.f,
             ImGui::GetColorU32(selected ? c.accent
                              : hovered  ? c.textBright
                                         : c.text));

    // A bar down the selected tool left edge. The background tint alone is easy
    // to miss; this is unambiguous without being loud.
    if (selected) {
        ImGui::GetWindowDrawList()->AddRectFilled(
            ImVec2(at.x - 6.f, at.y + 4.f), ImVec2(at.x - 3.f, at.y + side - 4.f),
            ImGui::GetColorU32(c.accent), 1.f);
    }

    if (hovered) {
        ImGui::BeginTooltip();
        ImGui::PushStyleColor(ImGuiCol_Text, c.textBright);
        ImGui::TextUnformatted(name);
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, c.accent);
        ImGui::TextUnformatted(shortcutHint);
        ImGui::PopStyleColor();
        if (description != nullptr) {
            ImGui::PushStyleColor(ImGuiCol_Text, c.textDim);
            ImGui::Spacing();
            ImGui::PushTextWrapPos(260.f);
            ImGui::TextUnformatted(description);
            ImGui::PopTextWrapPos();
            ImGui::PopStyleColor();
        }
        ImGui::EndTooltip();
    }
    return pressed;
}

bool eyeToggle(const char* id, bool visible, float size) {
    const ImVec2 at = ImGui::GetCursorScreenPos();
    const bool clicked = ImGui::InvisibleButton(id, ImVec2(size, size));
    const bool hovered = ImGui::IsItemHovered();
    const Palette& c = palette();
    const ImU32 colour = ImGui::GetColorU32(visible ? (hovered ? c.text : c.textDim)
                                                    : (hovered ? c.textDim : c.border));
    drawIcon(ImGui::GetWindowDrawList(), visible ? Icon::Eye : Icon::EyeShut, at, size, colour);
    return clicked;
}

bool swatch(const char* id, ImU32 colour, bool selected, float size) {
    ImGui::PushID(id);
    const ImVec2 at = ImGui::GetCursorScreenPos();
    const bool pressed = ImGui::InvisibleButton("##swatch", ImVec2(size, size));
    const bool hovered = ImGui::IsItemHovered();

    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 to(at.x + size, at.y + size);

    // A chequer behind, so a partly transparent colour reads as transparent
    // rather than as a dark colour.
    if ((colour >> IM_COL32_A_SHIFT) < 255) {
        const float half = size * 0.5f;
        draw->AddRectFilled(at, to, ImGui::GetColorU32(kPalette.checkerDark),
                            kMetrics.rounding);
        draw->AddRectFilled(ImVec2(at.x + half, at.y), ImVec2(to.x, at.y + half),
                            ImGui::GetColorU32(kPalette.checkerLight));
        draw->AddRectFilled(ImVec2(at.x, at.y + half), ImVec2(at.x + half, to.y),
                            ImGui::GetColorU32(kPalette.checkerLight));
    }
    draw->AddRectFilled(at, to, colour, kMetrics.rounding);

    if (selected) {
        // A ring outside the swatch rather than a border inside it: an inset
        // border eats the colour it is meant to identify.
        draw->AddRect(ImVec2(at.x - 2.f, at.y - 2.f), ImVec2(to.x + 2.f, to.y + 2.f),
                      ImGui::GetColorU32(kPalette.accent), kMetrics.rounding + 1.f,
                      0, 2.f);
    } else {
        draw->AddRect(at, to, ImGui::GetColorU32(
                          hovered ? kPalette.text : kPalette.border),
                      kMetrics.rounding, 0, 1.f);
    }

    ImGui::PopID();
    return pressed;
}

void statusItem(const char* label, const char* value, bool bright) {
    ImGui::PushStyleColor(ImGuiCol_Text, kPalette.textDim);
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();
    ImGui::SameLine(0.f, 5.f);
    ImGui::PushStyleColor(ImGuiCol_Text,
                          bright ? kPalette.textBright : kPalette.text);
    ImGui::TextUnformatted(value);
    ImGui::PopStyleColor();
}

void hint(const char* text) {
    ImGui::PushStyleColor(ImGuiCol_Text, kPalette.textDim);
    ImGui::TextUnformatted("?");
    ImGui::PopStyleColor();
    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(280.f);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

} // namespace theme
} // namespace fast
