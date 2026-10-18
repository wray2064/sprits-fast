// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

#include "ui/theme.h"

#include "font_inter_regular.h"
#include "font_inter_semibold.h"

#include "app/i18n.h"
#include "ui/widgets.h"

#include <algorithm>
#include <string>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace fast {
namespace theme {
namespace {

float gScale = 1.f;     // the person's interface scale; see setScale

ImVec4 vec(Rgba colour) {
    return ImVec4(static_cast<float>(colour.r) / 255.f, static_cast<float>(colour.g) / 255.f,
                  static_cast<float>(colour.b) / 255.f, static_cast<float>(colour.a) / 255.f);
}

// The window's colours are the theme's roles (app/theme_tokens.h), turned into
// what ImGui draws with. The first seven names are the ones the panels have
// always used; the rest are the roles the overhaul adds, which the panels move
// onto as each is rebuilt.
Palette paletteFor(const Theme& t) {
    Palette p {};
    p.windowBackground = vec(t[Token::App]);
    p.panelBackground = vec(t[Token::Panel]);
    p.canvasBackground = vec(t[Token::Canvas]);
    p.control = vec(t[Token::Control]);
    p.controlHovered = vec(t[Token::ControlHover]);
    p.controlActive = vec(t[Token::ControlPressed]);
    p.border = vec(t[Token::Border]);
    p.text = vec(t[Token::TextPrimary]);
    p.textDim = vec(t[Token::TextSecondary]);
    p.textBright = vec(t[Token::TextStrong]);
    p.accent = vec(t[Token::Accent]);
    p.accentDim = vec(t[Token::AccentSubtle]);
    p.danger = vec(t[Token::Danger]);
    p.checkerLight = vec(t[Token::CheckerA]);
    p.checkerDark = vec(t[Token::CheckerB]);

    p.raised = vec(t[Token::Raised]);
    p.inset = vec(t[Token::Inset]);
    p.textMuted = vec(t[Token::TextMuted]);
    p.textDisabled = vec(t[Token::TextDisabled]);
    p.textOnAccent = vec(t[Token::TextOnAccent]);
    p.separator = vec(t[Token::Separator]);
    p.focus = vec(t[Token::Focus]);
    p.selected = vec(t[Token::Selected]);
    p.selectedText = vec(t[Token::SelectedText]);
    p.accentText = vec(t[Token::AccentText]);
    p.accentHover = vec(t[Token::AccentHover]);
    p.accentPressed = vec(t[Token::AccentPressed]);
    p.success = vec(t[Token::Success]);
    p.warning = vec(t[Token::Warning]);
    p.info = vec(t[Token::Info]);
    p.successSubtle = vec(t[Token::SuccessSubtle]);
    p.warningSubtle = vec(t[Token::WarningSubtle]);
    p.dangerSubtle = vec(t[Token::DangerSubtle]);
    p.infoSubtle = vec(t[Token::InfoSubtle]);
    p.gridLine = vec(t[Token::GridLine]);
    p.selectionLine = vec(t[Token::SelectionLine]);
    p.guide = vec(t[Token::Guide]);
    p.onionPast = vec(t[Token::OnionPast]);
    p.onionFuture = vec(t[Token::OnionFuture]);
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

bool isLight() { return !gTheme.dark; }

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
    kMetrics.itemSpacing = steps.itemSpacing;
    kMetrics.panelPadding = steps.panelPadding;
    kMetrics.sectionGap = steps.sectionGap;
    kMetrics.rowHeight = steps.rowHeight;
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
                     ImGui::GetColorU32(gPalette.panelBackground));
            break;
        }

        case Icon::Eraser: {
            // A block eraser, tilted. The seam between the rubber and the sleeve
            // is a hairline: any wider and the icon reads as two separate blocks
            // rather than one object, which is what a full gap did here.
            axisQuad(draw, s, ImVec2(0.22f, 0.76f), ImVec2(0.78f, 0.28f), 0.185f,
                     colour);
            draw->AddLine(s(0.36f, 0.75f), s(0.66f, 0.43f),
                          ImGui::GetColorU32(gPalette.panelBackground),
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

        // The selection tools, drawn dashed: a dashed outline is what every
        // editor has used for "selected" since MacPaint, and it separates
        // these at a glance from the shape tools, which make the same
        // outlines solid. Placeholders, like the rest -- see icons-needed.md.
        case Icon::Marquee:
        case Icon::EllipseMarquee: {
            const float weight = std::max(1.5f, size * 0.075f);
            const int dashes = 16;
            for (int i = 0; i < dashes; i += 2) {
                const float a = static_cast<float>(i) / dashes;
                const float b = static_cast<float>(i + 1) / dashes;
                if (icon == Icon::Marquee) {
                    // Round the rectangle's perimeter, as a fraction of it.
                    const auto along = [&](float t) {
                        const float x0 = 0.16f, y0 = 0.24f, x1 = 0.84f, y1 = 0.76f;
                        const float w = x1 - x0, h = y1 - y0, p = 2.f * (w + h);
                        float d = t * p;
                        if (d < w) { return s(x0 + d, y0); }
                        d -= w;
                        if (d < h) { return s(x1, y0 + d); }
                        d -= h;
                        if (d < w) { return s(x1 - d, y1); }
                        d -= w;
                        return s(x0, y1 - d);
                    };
                    draw->AddLine(along(a), along(b), colour, weight);
                } else {
                    const float ta = a * 6.2831853f;
                    const float tb = b * 6.2831853f;
                    const ImVec2 c = s(0.5f, 0.5f);
                    const float rx = 0.34f * size, ry = 0.26f * size;
                    draw->AddLine(ImVec2(c.x + rx * std::cos(ta), c.y + ry * std::sin(ta)),
                                  ImVec2(c.x + rx * std::cos(tb), c.y + ry * std::sin(tb)),
                                  colour, weight);
                }
            }
            break;
        }

        case Icon::Lasso: {
            // A loop with a tail: the rope, not the shape it makes.
            const float weight = std::max(1.5f, size * 0.075f);
            draw->PathClear();
            for (int i = 0; i <= 20; ++i) {
                const float t = 6.2831853f * static_cast<float>(i) / 20.f;
                const ImVec2 c = s(0.52f, 0.40f);
                draw->PathLineTo(ImVec2(c.x + 0.30f * size * std::cos(t),
                                        c.y + 0.20f * size * std::sin(t)));
            }
            draw->PathStroke(colour, 0, weight);
            draw->AddBezierCubic(s(0.36f, 0.56f), s(0.30f, 0.70f), s(0.42f, 0.78f),
                                 s(0.30f, 0.88f), colour, weight, 12);
            break;
        }

        case Icon::Polygon: {
            // A filled five-sided shape with its corners marked: a shape
            // made of corners that stay corners.
            const ImVec2 corners[] = { { 0.50f, 0.16f }, { 0.84f, 0.42f }, { 0.70f, 0.84f },
                                       { 0.26f, 0.80f }, { 0.16f, 0.38f } };
            draw->PathClear();
            for (const ImVec2& p : corners) {
                draw->PathLineTo(s(p.x, p.y));
            }
            draw->PathFillConvex((colour & 0x00FFFFFFu) | 0x90000000u);
            for (const ImVec2& p : corners) {
                draw->AddRectFilled(s(p.x - 0.06f, p.y - 0.06f), s(p.x + 0.06f, p.y + 0.06f),
                                    colour);
            }
            break;
        }

        case Icon::Slice: {
            // A frame with a corner cut into a tab: a named piece of the
            // canvas rather than a selection of it.
            const float weight = std::max(1.4f, size * 0.07f);
            draw->AddRect(s(0.16f, 0.26f), s(0.84f, 0.84f), colour, 0.f, 0, weight);
            draw->AddRectFilled(s(0.16f, 0.14f), s(0.48f, 0.30f), colour);
            draw->AddLine(s(0.38f, 0.26f), s(0.38f, 0.84f), colour, 1.f);
            draw->AddLine(s(0.62f, 0.26f), s(0.62f, 0.84f), colour, 1.f);
            break;
        }

        case Icon::Curve: {
            // An S with one anchor's handle drawn out: a pen, not a brush.
            const float weight = std::max(1.6f, size * 0.08f);
            draw->AddBezierCubic(s(0.16f, 0.78f), s(0.20f, 0.20f), s(0.80f, 0.80f),
                                 s(0.84f, 0.22f), colour, weight, 20);
            draw->AddLine(s(0.16f, 0.78f), s(0.20f, 0.20f), colour, 1.f);
            draw->AddCircleFilled(s(0.20f, 0.20f), size * 0.07f, colour, 10);
            draw->AddRectFilled(s(0.10f, 0.72f), s(0.22f, 0.84f), colour);
            draw->AddRectFilled(s(0.78f, 0.16f), s(0.90f, 0.28f), colour);
            break;
        }

        case Icon::PolygonLasso: {
            // A dashed polygon, corners dotted.
            const ImVec2 corners[] = { { 0.18f, 0.72f }, { 0.30f, 0.22f }, { 0.78f, 0.30f },
                                       { 0.66f, 0.82f } };
            const float weight = std::max(1.4f, size * 0.07f);
            for (int i = 0; i < 4; ++i) {
                const ImVec2 a = corners[i];
                const ImVec2 b = corners[(i + 1) % 4];
                const ImVec2 mid { (a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f };
                draw->AddLine(s(a.x, a.y), s(mid.x, mid.y), colour, weight);
                draw->AddCircleFilled(s(a.x, a.y), size * 0.06f, colour, 8);
            }
            break;
        }

        case Icon::Wand: {
            // A stick and a star at its tip.
            const float weight = std::max(1.8f, size * 0.09f);
            draw->AddLine(s(0.20f, 0.82f), s(0.60f, 0.42f), colour, weight);
            const ImVec2 star = s(0.70f, 0.30f);
            const float r = size * 0.16f;
            for (int i = 0; i < 4; ++i) {
                const float t = 0.7853982f * static_cast<float>(i * 2);
                draw->AddLine(ImVec2(star.x - r * std::cos(t), star.y - r * std::sin(t)),
                              ImVec2(star.x + r * std::cos(t), star.y + r * std::sin(t)),
                              colour, std::max(1.3f, size * 0.06f));
            }
            break;
        }

        case Icon::Move: {
            // Four arrows out from the middle.
            const float weight = std::max(1.6f, size * 0.08f);
            draw->AddLine(s(0.50f, 0.14f), s(0.50f, 0.86f), colour, weight);
            draw->AddLine(s(0.14f, 0.50f), s(0.86f, 0.50f), colour, weight);
            const float h = 0.12f;
            draw->AddTriangleFilled(s(0.50f, 0.08f), s(0.50f - h, 0.22f), s(0.50f + h, 0.22f), colour);
            draw->AddTriangleFilled(s(0.50f, 0.92f), s(0.50f - h, 0.78f), s(0.50f + h, 0.78f), colour);
            draw->AddTriangleFilled(s(0.08f, 0.50f), s(0.22f, 0.50f - h), s(0.22f, 0.50f + h), colour);
            draw->AddTriangleFilled(s(0.92f, 0.50f), s(0.78f, 0.50f - h), s(0.78f, 0.50f + h), colour);
            break;
        }

        case Icon::Spray: {
            // A can and its mist.
            draw->AddRectFilled(s(0.30f, 0.42f), s(0.58f, 0.88f), colour, 2.f);
            draw->AddRectFilled(s(0.38f, 0.30f), s(0.50f, 0.42f), colour);
            const float dot = std::max(1.2f, size * 0.05f);
            for (const ImVec2 mist : { ImVec2(0.68f, 0.20f), ImVec2(0.80f, 0.30f),
                                     ImVec2(0.72f, 0.36f), ImVec2(0.86f, 0.16f),
                                     ImVec2(0.62f, 0.12f), ImVec2(0.84f, 0.42f) }) {
                draw->AddCircleFilled(s(mist.x, mist.y), dot, colour, 6);
            }
            break;
        }

        case Icon::Contour: {
            // A freehand loop, filled: the shape a contour stroke makes.
            draw->PathClear();
            const ImVec2 points[] = { { 0.20f, 0.55f }, { 0.28f, 0.24f }, { 0.55f, 0.18f },
                                      { 0.82f, 0.34f }, { 0.74f, 0.70f }, { 0.44f, 0.84f } };
            for (const ImVec2& p : points) {
                draw->PathLineTo(s(p.x, p.y));
            }
            draw->PathFillConvex(colour);
            break;
        }

        case Icon::Gradient: {
            // A bar that dithers from solid to nothing: the tool's result.
            const float cell = size * 0.1f;
            for (int x = 0; x < 7; ++x) {
                for (int y = 0; y < 4; ++y) {
                    // Denser at the left; a Bayer-ish step to the right.
                    const int threshold = ((x + y * 3) % 4);
                    if (x < 2 || threshold < 4 - (x * 4) / 7) {
                        const ImVec2 cellAt = s(0.14f + 0.1f * static_cast<float>(x),
                                            0.30f + 0.1f * static_cast<float>(y));
                        draw->AddRectFilled(cellAt, ImVec2(cellAt.x + cell, cellAt.y + cell), colour);
                    }
                }
            }
            break;
        }

        case Icon::Text: {
            // A capital T with serifs at the foot.
            const float weight = std::max(2.f, size * 0.12f);
            draw->AddLine(s(0.20f, 0.22f), s(0.80f, 0.22f), colour, weight);
            draw->AddLine(s(0.50f, 0.22f), s(0.50f, 0.80f), colour, weight);
            draw->AddLine(s(0.38f, 0.80f), s(0.62f, 0.80f), colour, weight * 0.8f);
            break;
        }

        case Icon::Hand: {
            // An open palm: four fingers and a thumb over a round heel.
            const float weight = std::max(1.8f, size * 0.10f);
            draw->AddCircleFilled(s(0.50f, 0.68f), size * 0.22f, colour, 16);
            for (int i = 0; i < 4; ++i) {
                const float x = 0.32f + 0.12f * static_cast<float>(i);
                draw->AddLine(s(x, 0.62f), s(x, i == 0 || i == 3 ? 0.26f : 0.16f), colour, weight);
            }
            draw->AddLine(s(0.30f, 0.72f), s(0.14f, 0.52f), colour, weight);
            break;
        }

        case Icon::Zoom: {
            // A magnifier.
            const float weight = std::max(1.6f, size * 0.09f);
            draw->AddCircle(s(0.42f, 0.42f), size * 0.24f, colour, 20, weight);
            draw->AddLine(s(0.60f, 0.60f), s(0.84f, 0.84f), colour, weight * 1.4f);
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
                          ImGui::GetColorU32(gPalette.panelBackground),
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

void statusItem(const char* label, const char* value, bool bright) {
    ImGui::PushStyleColor(ImGuiCol_Text, gPalette.textDim);
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();
    ImGui::SameLine(0.f, 5.f);
    ImGui::PushStyleColor(ImGuiCol_Text,
                          bright ? gPalette.textBright : gPalette.text);
    ImGui::TextUnformatted(value);
    ImGui::PopStyleColor();
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
