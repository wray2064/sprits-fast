// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

#include "ui/theme.h"

#include <cstdio>
#include <cstring>

namespace fast {
namespace theme {
namespace {

constexpr ImVec4 rgb(int r, int g, int b, float a = 1.f) {
    return ImVec4(static_cast<float>(r) / 255.f, static_cast<float>(g) / 255.f,
                  static_cast<float>(b) / 255.f, a);
}

const Palette kPalette {
    /* windowBackground */ rgb(24, 25, 28),
    /* panelBackground  */ rgb(31, 33, 37),
    /* canvasBackground */ rgb(18, 19, 21),
    /* control          */ rgb(44, 47, 52),
    /* controlHovered   */ rgb(56, 60, 66),
    /* controlActive    */ rgb(66, 71, 78),
    /* border           */ rgb(48, 51, 57),
    /* text             */ rgb(198, 202, 209),
    /* textDim          */ rgb(126, 131, 140),
    /* textBright       */ rgb(233, 236, 240),
    /* accent           */ rgb(226, 143, 65),
    /* accentDim        */ rgb(226, 143, 65, 0.28f),
    /* danger           */ rgb(206, 88, 76),
    /* checkerLight     */ rgb(58, 60, 65),
    /* checkerDark      */ rgb(46, 48, 52),
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
    ImGui::Dummy(ImVec2(0.f, 2.f));
    ImGui::PushStyleColor(ImGuiCol_Text, kPalette.textDim);
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();
    ImGui::Spacing();
}

bool toolButton(const char* glyph, const char* name, const char* shortcutHint,
                bool selected, const char* description) {
    const Palette& c = kPalette;
    const float side = 30.f;

    ImGui::PushStyleColor(ImGuiCol_Button,
                          selected ? c.accentDim : ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                          selected ? c.accentDim : c.controlHovered);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, c.controlActive);
    ImGui::PushStyleColor(ImGuiCol_Text, selected ? c.accent : c.text);

    const bool pressed = ImGui::Button(glyph, ImVec2(side, side));

    ImGui::PopStyleColor(4);

    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::PushStyleColor(ImGuiCol_Text, c.textBright);
        ImGui::TextUnformatted(name);
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, c.textDim);
        ImGui::Text("(%s)", shortcutHint);
        if (description != nullptr) {
            ImGui::Spacing();
            ImGui::PushTextWrapPos(260.f);
            ImGui::TextUnformatted(description);
            ImGui::PopTextWrapPos();
        }
        ImGui::PopStyleColor();
        ImGui::EndTooltip();
    }
    return pressed;
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
