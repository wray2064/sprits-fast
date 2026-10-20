// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

#include "ui/theme.h"

#include "font_inter_regular.h"
#include "font_inter_semibold.h"
#include "icons_embedded.h"

#include "app/i18n.h"
#include "app/vector_icon.h"
#include "ui/widgets.h"

#include <algorithm>
#include <string>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

namespace fast {
namespace theme {

ImVec4 toImVec4(Rgba colour) {
    return ImVec4(static_cast<float>(colour.r) / 255.f, static_cast<float>(colour.g) / 255.f,
                  static_cast<float>(colour.b) / 255.f, static_cast<float>(colour.a) / 255.f);
}

namespace {

float gScale = 1.f;     // the person's interface scale; see setScale

// The window's colours are the theme's roles (app/theme_tokens.h), turned into
// what ImGui draws with. The first seven names are the ones the panels have
// always used; the rest are the roles the overhaul adds, which the panels move
// onto as each is rebuilt.
Palette paletteFor(const Theme& t) {
    Palette p {};
    p.windowBackground = toImVec4(t[Token::App]);
    p.panelBackground = toImVec4(t[Token::Panel]);
    p.canvasBackground = toImVec4(t[Token::Canvas]);
    p.control = toImVec4(t[Token::Control]);
    p.controlHovered = toImVec4(t[Token::ControlHover]);
    p.controlActive = toImVec4(t[Token::ControlPressed]);
    p.border = toImVec4(t[Token::Border]);
    p.text = toImVec4(t[Token::TextPrimary]);
    p.textDim = toImVec4(t[Token::TextSecondary]);
    p.textBright = toImVec4(t[Token::TextStrong]);
    p.accent = toImVec4(t[Token::Accent]);
    p.accentDim = toImVec4(t[Token::AccentSubtle]);
    p.danger = toImVec4(t[Token::Danger]);
    p.checkerLight = toImVec4(t[Token::CheckerA]);
    p.checkerDark = toImVec4(t[Token::CheckerB]);

    p.raised = toImVec4(t[Token::Raised]);
    p.inset = toImVec4(t[Token::Inset]);
    p.textMuted = toImVec4(t[Token::TextMuted]);
    p.textDisabled = toImVec4(t[Token::TextDisabled]);
    p.textOnAccent = toImVec4(t[Token::TextOnAccent]);
    p.separator = toImVec4(t[Token::Separator]);
    p.focus = toImVec4(t[Token::Focus]);
    p.selected = toImVec4(t[Token::Selected]);
    p.selectedText = toImVec4(t[Token::SelectedText]);
    p.accentText = toImVec4(t[Token::AccentText]);
    p.accentHover = toImVec4(t[Token::AccentHover]);
    p.accentPressed = toImVec4(t[Token::AccentPressed]);
    p.success = toImVec4(t[Token::Success]);
    p.warning = toImVec4(t[Token::Warning]);
    p.info = toImVec4(t[Token::Info]);
    p.successSubtle = toImVec4(t[Token::SuccessSubtle]);
    p.warningSubtle = toImVec4(t[Token::WarningSubtle]);
    p.dangerSubtle = toImVec4(t[Token::DangerSubtle]);
    p.infoSubtle = toImVec4(t[Token::InfoSubtle]);
    p.gridLine = toImVec4(t[Token::GridLine]);
    p.selectionLine = toImVec4(t[Token::SelectionLine]);
    p.guide = toImVec4(t[Token::Guide]);
    p.onionPast = toImVec4(t[Token::OnionPast]);
    p.onionFuture = toImVec4(t[Token::OnionFuture]);
    return p;
}

Theme gTheme = *findBuiltinTheme("slate");
Palette gPalette = paletteFor(gTheme);

const TypeScale kType {};
Metrics kMetrics {};                 // set from the density by apply()
Density gDensity = Density::Comfortable;
float   gDisplayScale = 1.f;         // the display's own scale (loadFonts)
ImFont* gRegular = nullptr;
ImFont* gStrong = nullptr;

// The spacing of each density. Control height follows from the frame padding
// and the font, so what changes is the air, not the type.
struct DensitySteps {
    float framePaddingY;
    float itemSpacing;
    float panelPadding;
    float sectionGap;
    float rowHeight;
};
DensitySteps stepsFor(Density density) {
    switch (density) {
        case Density::Compact:  return { 3.f, 4.f, 8.f, 10.f, 22.f };
        case Density::Spacious: return { 7.f, 8.f, 12.f, 18.f, 28.f };
        case Density::Comfortable: break;
    }
    return { 5.f, 6.f, 10.f, 14.f, 24.f };
}

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

const Palette& palette() { return gPalette; }

const Theme& current() { return gTheme; }

ImVec4 extra(const char* id) {
    return toImVec4(gTheme.extraColour(id));
}

ImU32 extraU32(const char* id) {
    const Rgba c = gTheme.extraColour(id);
    return IM_COL32(c.r, c.g, c.b, c.a);
}

void setTheme(const Theme& theme) {
    gTheme = theme;
    gPalette = paletteFor(gTheme);
    apply();
}

void setLight(bool light) {
    setTheme(*findBuiltinTheme(light ? "paper" : "slate"));
}
const Metrics& metrics() { return kMetrics; }

void apply() {
    ImGuiStyle& style = ImGui::GetStyle();
    // Every call starts from the style as ImGui made it, so a scale applied
    // twice is not applied on top of itself.
    static const ImGuiStyle pristine = ImGui::GetStyle();
    style = pristine;
    const Palette& c = gPalette;
    const DensitySteps steps = stepsFor(gDensity);
    // What the style is built from is for 100% (it is scaled at the end, below);
    // what metrics() hands to the panels is the same, already at the person's
    // interface size, because a panel adds it to widths ImGui has scaled (a gap
    // between buttons) and a measure at 100% beside them would not fit.
    Metrics m;
    m.itemSpacing = steps.itemSpacing;
    m.panelPadding = steps.panelPadding;
    m.sectionGap = steps.sectionGap;
    m.rowHeight = steps.rowHeight;
    kMetrics = m;
    kMetrics.rowHeight *= gScale;
    kMetrics.panelPadding *= gScale;
    kMetrics.itemSpacing *= gScale;
    kMetrics.sectionGap *= gScale;
    kMetrics.sidebarWidth *= gScale;
    kMetrics.toolbarHeight *= gScale;
    kMetrics.rounding *= gScale;

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
    // A hairline round every control. Without it a control is a fill a few
    // percent off the panel, which fails the "can I see where it is" test in
    // the light themes and on a dim screen; with it the edge is always there,
    // in the border colour, which each theme sets to be seen and no more.
    style.FrameBorderSize   = 1.f;
    style.PopupBorderSize   = 1.f;

    style.WindowPadding     = ImVec2(m.panelPadding, m.panelPadding);
    style.FramePadding      = ImVec2(8.f, steps.framePaddingY);
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
    colours[ImGuiCol_PopupBg]              = c.raised;
    colours[ImGuiCol_Border]               = c.border;
    colours[ImGuiCol_BorderShadow]         = ImVec4(0, 0, 0, 0);

    colours[ImGuiCol_Text]                 = c.text;
    colours[ImGuiCol_TextDisabled]         = c.textMuted;

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

    colours[ImGuiCol_Header]               = c.selected;
    colours[ImGuiCol_HeaderHovered]        = c.controlHovered;
    colours[ImGuiCol_HeaderActive]         = c.selected;

    colours[ImGuiCol_Separator]            = c.separator;
    colours[ImGuiCol_SeparatorHovered]     = c.accentDim;
    colours[ImGuiCol_SeparatorActive]      = c.accent;

    colours[ImGuiCol_ResizeGrip]           = ImVec4(0, 0, 0, 0);
    colours[ImGuiCol_ResizeGripHovered]    = c.accentDim;
    colours[ImGuiCol_ResizeGripActive]     = c.accent;

    colours[ImGuiCol_Tab]                  = c.windowBackground;
    colours[ImGuiCol_TabHovered]           = c.control;
    colours[ImGuiCol_TabSelected]          = c.panelBackground;
    colours[ImGuiCol_TabSelectedOverline]  = c.accent;
    colours[ImGuiCol_TabDimmed]            = c.windowBackground;
    colours[ImGuiCol_TabDimmedSelected]    = c.panelBackground;
    colours[ImGuiCol_TabDimmedSelectedOverline] = c.accentDim;

    // Docking: where a dragged panel would land, and the empty space of a
    // dock node with nothing in it.
    colours[ImGuiCol_DockingPreview]       = ImVec4(c.accent.x, c.accent.y, c.accent.z, 0.45f);
    colours[ImGuiCol_DockingEmptyBg]       = c.windowBackground;

    colours[ImGuiCol_TextSelectedBg]       = c.accentDim;
    colours[ImGuiCol_NavCursor]            = c.focus;
    colours[ImGuiCol_ModalWindowDimBg]     = ImVec4(0.f, 0.f, 0.f, 0.55f);

    // The person's interface scale: text and every spacing, not the canvas.
    if (gScale != 1.f) {
        style.ScaleAllSizes(gScale);
    }
    style.FontScaleMain = gScale;
    style.FontScaleDpi = gDisplayScale;
}

void setScale(float scale) {
    gScale = std::clamp(scale, 0.5f, 3.f);
    apply();
}

float scale() {
    return gScale;
}

float px(float at100) {
    return at100 * gScale;
}

void loadFonts(float scale) {
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();
    gDisplayScale = scale > 0.f ? scale : 1.f;
    gRegular = nullptr;
    gStrong = nullptr;

    // Inter, compiled in: the same face on every machine. The sizes are the
    // type scale's, unscaled; the display's scale and the person's interface
    // size are applied by the style (see apply), so a pushed size follows both.
    ImFontConfig config;
    config.FontDataOwnedByAtlas = false;          // the bytes are ours, in the program
    gRegular = io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(kInterRegular),
                                              static_cast<int>(kInterRegular_size),
                                              kType.body, &config);
    gStrong = io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(kInterSemiBold),
                                             static_cast<int>(kInterSemiBold_size),
                                             kType.body, &config);
    if (gRegular != nullptr) {
        if (gStrong == nullptr) {
            gStrong = gRegular;
        }
        return;
    }

    // Not expected, but a program that starts beats one that does not: the
    // platform's own face, and then ImGui's built-in one.
    for (const char* path : kFontPaths) {
        gRegular = io.Fonts->AddFontFromFileTTF(path, kType.body + 1.f);
        if (gRegular != nullptr) {
            break;
        }
    }
    if (gRegular == nullptr) {
        gRegular = io.Fonts->AddFontDefault();
    }
    gStrong = gRegular;
}

void pushText(TextStyle style) {
    switch (style) {
        case TextStyle::Caption: ImGui::PushFont(gRegular, kType.caption); break;
        case TextStyle::Label:   ImGui::PushFont(gStrong, kType.caption); break;
        case TextStyle::Body:    ImGui::PushFont(gRegular, kType.body); break;
        case TextStyle::Strong:  ImGui::PushFont(gStrong, kType.body); break;
        case TextStyle::Title:   ImGui::PushFont(gStrong, kType.title); break;
        case TextStyle::Display: ImGui::PushFont(gStrong, kType.display); break;
    }
}

void popText() {
    ImGui::PopFont();
}

const TypeScale& type() { return kType; }

void setDensity(Density density) {
    gDensity = density;
    apply();
}

Density density() { return gDensity; }

// ---------------------------------------------------------------- widgets --

bool foldingHeader(const char* label) {
    const char* shown = tr(label);
    ImGui::Dummy(ImVec2(0.f, 2.f));
    // No box behind it, as with a plain section header: a filled bar would
    // read as a selected row.
    pushText(TextStyle::Label);
    ImGui::PushStyleColor(ImGuiCol_Text, gPalette.textDim);
    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.f, 0.f, 0.f, 0.f));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, gPalette.controlHovered);
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, gPalette.controlActive);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2.f, 2.f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.f);   // a heading is not a control
    const std::string id = std::string(shown) + "###fold" + label;
    const bool open = ImGui::CollapsingHeader(id.c_str(), ImGuiTreeNodeFlags_DefaultOpen);
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(4);
    popText();
    const ImVec2 at = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    ImGui::GetWindowDrawList()->AddLine(
        ImVec2(at.x, at.y + 1.f), ImVec2(at.x + width, at.y + 1.f),
        ImGui::GetColorU32(gPalette.separator));
    ImGui::Dummy(ImVec2(0.f, 3.f));
    return open;
}

void sectionHeader(const char* label) {
    label = tr(label);
    // A small semibold label in secondary text. It used to be amber, which put
    // the accent on every heading in every panel; the accent is for state
    // (selected, focused, primary), so a heading is now the structure of the
    // panel and nothing more, and the colour left for the thing that matters.
    ImGui::Dummy(ImVec2(0.f, 2.f));
    pushText(TextStyle::Label);
    ImGui::PushStyleColor(ImGuiCol_Text, gPalette.textDim);
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();
    popText();

    // A hairline under it, running to the edge of the panel.
    const ImVec2 at = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    ImGui::GetWindowDrawList()->AddLine(
        ImVec2(at.x, at.y + 1.f), ImVec2(at.x + width, at.y + 1.f),
        ImGui::GetColorU32(gPalette.separator));
    ImGui::Dummy(ImVec2(0.f, 3.f));
}

// ------------------------------------------------------------------ icons --
//
// Lucide's (ISC licence, assets/icons/lucide/, notices beside them and in
// NOTICE), compiled in as the SVGs they are and read into strokes
// (app/vector_icon.h). They are drawn here with whatever colour the caller asks
// for and a stroke that follows the size, so one set serves the rail, the layer
// rows and anything else, and none of it is a picture.

namespace {

struct IconSource {
    const unsigned char* data;
    size_t               size;
};

// In the order of the Icon enum.
const IconSource kIconSources[] = {
    { kIcon_pencil, kIcon_pencil_size },                 // Pencil
    { kIcon_eraser, kIcon_eraser_size },                 // Eraser
    { kIcon_paint_bucket, kIcon_paint_bucket_size },     // Bucket
    { kIcon_pipette, kIcon_pipette_size },               // Dropper
    { kIcon_square, kIcon_square_size },                 // Rectangle
    { kIcon_circle, kIcon_circle_size },                 // Ellipse
    { kIcon_slash, kIcon_slash_size },                   // Line
    { kIcon_eye, kIcon_eye_size },                       // Eye
    { kIcon_eye_off, kIcon_eye_off_size },               // EyeShut
    { kIcon_square_dashed, kIcon_square_dashed_size },   // Marquee
    { kIcon_circle_dashed, kIcon_circle_dashed_size },   // EllipseMarquee
    { kIcon_lasso, kIcon_lasso_size },                   // Lasso
    { kIcon_wand, kIcon_wand_size },                     // Wand
    { kIcon_move, kIcon_move_size },                     // Move
    { kIcon_spray_can, kIcon_spray_can_size },           // Spray
    { kIcon_pen_tool, kIcon_pen_tool_size },             // Contour
    { kIcon_hand, kIcon_hand_size },                     // Hand
    { kIcon_zoom_in, kIcon_zoom_in_size },               // Zoom
    { kIcon_blend, kIcon_blend_size },                   // Gradient
    { kIcon_type, kIcon_type_size },                     // Text
    { kIcon_lasso_select, kIcon_lasso_select_size },     // PolygonLasso
    { kIcon_pentagon, kIcon_pentagon_size },             // Polygon
    { kIcon_spline, kIcon_spline_size },                 // Curve
    { kIcon_slice, kIcon_slice_size },                   // Slice
    { kIconOwn_folder_open, kIconOwn_folder_open_size }, // FolderOpen
    { kIconOwn_sheet, kIconOwn_sheet_size },             // Sheet
    { kIconOwn_images, kIconOwn_images_size },           // Images
    { kIconOwn_book, kIconOwn_book_size },               // Book
    { kIconOwn_bulb, kIconOwn_bulb_size },               // Bulb
    { kIconOwn_keyboard, kIconOwn_keyboard_size },       // Keyboard
    { kIconOwn_file_plus, kIconOwn_file_plus_size },     // FilePlus
};
constexpr size_t kIconCount = sizeof(kIconSources) / sizeof(kIconSources[0]);
static_assert(kIconCount == static_cast<size_t>(Icon::FilePlus) + 1,
              "every Icon has a source, in the enum's order");

// Read once, on first use. An icon that will not read is simply not drawn.
const VectorIcon* iconShape(Icon icon) {
    static VectorIcon shapes[kIconCount];
    static char state[kIconCount] = {};          // 0 not read, 1 read, 2 would not read
    const size_t index = static_cast<size_t>(icon);
    if (index >= kIconCount) {
        return nullptr;
    }
    if (state[index] == 0) {
        const IconSource& source = kIconSources[index];
        std::string error;
        state[index] = parseVectorIcon(
            std::string(reinterpret_cast<const char*>(source.data), source.size),
            &shapes[index], &error) ? 1 : 2;
    }
    return state[index] == 1 ? &shapes[index] : nullptr;
}

} // namespace

void drawIcon(ImDrawList* draw, Icon icon, ImVec2 at, float size, ImU32 colour) {
    const VectorIcon* shape = iconShape(icon);
    if (shape == nullptr || size <= 0.f) {
        return;
    }
    const float scale = size / shape->width;
    // Lucide's stroke is 2 on a 24 grid. It follows the size, with a floor so
    // that a small icon does not thin to nothing.
    const float stroke = std::max(1.25f, 2.f * scale);
    static std::vector<ImVec2> points;
    for (const IconStroke& line : shape->strokes) {
        points.clear();
        for (const auto& p : line.points) {
            points.push_back(ImVec2(at.x + p.first * scale, at.y + p.second * scale));
        }
        draw->AddPolyline(points.data(), static_cast<int>(points.size()), colour,
                          line.closed ? ImDrawFlags_Closed : ImDrawFlags_None, stroke);
        // Round ends and a round dot where a stroke is a point: ImGui's polyline
        // is butt-ended, and a Lucide icon is not.
        if (!line.closed) {
            draw->AddCircleFilled(points.front(), stroke * 0.5f, colour);
            draw->AddCircleFilled(points.back(), stroke * 0.5f, colour);
        }
    }
}

bool toolButton(Icon icon, const char* name, const char* shortcutHint,
                bool selected, const char* description) {
    const Palette& c = gPalette;
    const float side = toolSide();

    ImGui::PushStyleColor(ImGuiCol_Button,
                          selected ? c.accentDim : ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                          selected ? c.accentDim : c.controlHovered);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, c.controlActive);

    ImGui::PushID(name);
    const ImVec2 at = ImGui::GetCursorScreenPos();
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.f);   // the rail is not boxed
    const bool pressed = ImGui::Button("##tool", ImVec2(side, side));
    ImGui::PopStyleVar();
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
                                         : c.textDim));

    // A bar down the selected tool left edge. The background tint alone is easy
    // to miss; this is unambiguous without being loud.
    if (selected) {
        const float pad = ImGui::GetStyle().WindowPadding.x;
        ImGui::GetWindowDrawList()->AddRectFilled(
            ImVec2(at.x - pad * 0.7f, at.y + 4.f), ImVec2(at.x - pad * 0.3f, at.y + side - 4.f),
            ImGui::GetColorU32(c.accent), 1.f);
    }

    if (hovered) {
        // The tool's name, its key as key caps, and what it does.
        ImGui::BeginTooltip();
        pushText(TextStyle::Strong);
        ImGui::TextColored(c.textBright, "%s", name);
        popText();
        if (shortcutHint != nullptr && shortcutHint[0] != '\0') {
            std::string part;
            const std::string keys = shortcutHint;
            for (size_t i = 0; i <= keys.size(); ++i) {
                if (i == keys.size() || keys[i] == '+') {
                    ImGui::SameLine(0.f, 8.f);
                    widgets::keycap(part.c_str());
                    part.clear();
                } else {
                    part += keys[i];
                }
            }
        }
        if (description != nullptr) {
            ImGui::PushStyleColor(ImGuiCol_Text, c.textDim);
            ImGui::Spacing();
            ImGui::PushTextWrapPos(ImGui::GetFontSize() * 19.f);
            ImGui::TextUnformatted(description);
            ImGui::PopTextWrapPos();
            ImGui::PopStyleColor();
        }
        ImGui::EndTooltip();
    }
    return pressed;
}

float toolSide() {
    return std::round(ImGui::GetFrameHeight() * 1.3f);
}

float toolRailWidth() {
    return toolSide() + ImGui::GetStyle().WindowPadding.x * 2.f;
}

void railDivider() {
    const float pad = ImGui::GetStyle().ItemSpacing.y;
    ImGui::Dummy(ImVec2(0.f, pad));
    const ImVec2 at = ImGui::GetCursorScreenPos();
    const float width = toolSide();
    ImGui::GetWindowDrawList()->AddLine(ImVec2(at.x + 3.f, at.y),
                                        ImVec2(at.x + width - 3.f, at.y),
                                        ImGui::GetColorU32(gPalette.separator));
    ImGui::Dummy(ImVec2(0.f, pad));
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
    // Named for what it is -- ##slot3, ##fore -- so a UI script can find it.
    const std::string label = std::strncmp(id, "##", 2) == 0 ? std::string(id)
                                                             : "##" + std::string(id);
    const bool pressed = ImGui::InvisibleButton(label.c_str(), ImVec2(size, size));
    const bool hovered = ImGui::IsItemHovered();

    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 to(at.x + size, at.y + size);

    // A chequer behind, so a partly transparent colour reads as transparent
    // rather than as a dark colour.
    if ((colour >> IM_COL32_A_SHIFT) < 255) {
        const float half = size * 0.5f;
        draw->AddRectFilled(at, to, ImGui::GetColorU32(gPalette.checkerDark),
                            kMetrics.rounding);
        draw->AddRectFilled(ImVec2(at.x + half, at.y), ImVec2(to.x, at.y + half),
                            ImGui::GetColorU32(gPalette.checkerLight));
        draw->AddRectFilled(ImVec2(at.x, at.y + half), ImVec2(at.x + half, to.y),
                            ImGui::GetColorU32(gPalette.checkerLight));
    }
    draw->AddRectFilled(at, to, colour, kMetrics.rounding);

    if (selected) {
        // A ring outside the swatch rather than a border inside it: an inset
        // border eats the colour it is meant to identify.
        draw->AddRect(ImVec2(at.x - 2.f, at.y - 2.f), ImVec2(to.x + 2.f, to.y + 2.f),
                      ImGui::GetColorU32(gPalette.accent), kMetrics.rounding + 1.f,
                      0, 2.f);
    } else {
        draw->AddRect(at, to, ImGui::GetColorU32(
                          hovered ? gPalette.text : gPalette.border),
                      kMetrics.rounding, 0, 1.f);
    }

    ImGui::PopID();
    return pressed;
}

float hintWidth() {
    return std::round(ImGui::GetFontSize() * 1.1f) + ImGui::GetStyle().ItemSpacing.x;
}

// A small circle with a question mark, brightening when it is pointed at, and
// the explanation in a tooltip as wide as the font says.
void hint(const char* text) {
    const float d = std::round(ImGui::GetFontSize() * 1.1f);
    ImGui::PushID(text);
    const ImVec2 at = ImGui::GetCursorScreenPos();
    const float lift = std::round((ImGui::GetFrameHeight() - d) * 0.5f);
    ImGui::SetCursorScreenPos(ImVec2(at.x, at.y + lift));
    ImGui::InvisibleButton("##hint", ImVec2(d, d));
    const bool hovered = ImGui::IsItemHovered();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImU32 ink = ImGui::GetColorU32(hovered ? gPalette.textBright : gPalette.textMuted);
    const ImVec2 centre(at.x + d * 0.5f, at.y + lift + d * 0.5f);
    draw->AddCircle(centre, d * 0.5f - 0.5f, ink, 0, 1.2f);
    pushText(TextStyle::Label);
    const ImVec2 mark = ImGui::CalcTextSize("?");
    draw->AddText(ImVec2(centre.x - mark.x * 0.5f, centre.y - mark.y * 0.5f), ink, "?");
    popText();
    ImGui::PopID();
    if (hovered) {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 20.f);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

} // namespace theme
} // namespace fast
