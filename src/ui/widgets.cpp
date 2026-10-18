// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

#include "ui/widgets.h"

#include "app/i18n.h"
#include "app/theme_tokens.h"
#include "ui/theme.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <string>

namespace fast {

// Defined with the UI scripts: gives the widget just drawn a name a script can
// find it by, for the ones ImGui does not name itself.
void nameForScripts(const char* label);

namespace widgets {

namespace {

// How much the interface is scaled in all: the person's interface size and the
// display's. Fixed pixel measures (a dialog's width) follow it.
float scaleFactor() {
    const ImGuiStyle& style = ImGui::GetStyle();
    return style.FontScaleMain * style.FontScaleDpi;
}

ImU32 u32(const ImVec4& colour) {
    return ImGui::GetColorU32(colour);
}

// What a tone is drawn in as text or an icon, and the tint behind it.
ImVec4 toneColour(Tone tone) {
    const theme::Palette& p = theme::palette();
    switch (tone) {
        case Tone::Accent:  return p.accentText;
        case Tone::Success: return p.success;
        case Tone::Warning: return p.warning;
        case Tone::Danger:  return p.danger;
        case Tone::Info:    return p.info;
        case Tone::Neutral: break;
    }
    return p.textDim;
}

ImVec4 toneTint(Tone tone) {
    const theme::Palette& p = theme::palette();
    switch (tone) {
        case Tone::Accent:  return p.accentDim;
        case Tone::Success: return p.successSubtle;
        case Tone::Warning: return p.warningSubtle;
        case Tone::Danger:  return p.dangerSubtle;
        case Tone::Info:    return p.infoSubtle;
        case Tone::Neutral: break;
    }
    return p.control;
}

} // namespace

// ---------------------------------------------------------------------- buttons --

bool button(const char* label, ButtonStyle style, ImVec2 size) {
    const theme::Palette& p = theme::palette();
    const ImVec4 clear(0.f, 0.f, 0.f, 0.f);
    int colours = 0;
    const auto push = [&colours](ImGuiCol slot, const ImVec4& colour) {
        ImGui::PushStyleColor(slot, colour);
        ++colours;
    };
    switch (style) {
        case ButtonStyle::Primary:
            push(ImGuiCol_Button, p.accent);
            push(ImGuiCol_ButtonHovered, p.accentHover);
            push(ImGuiCol_ButtonActive, p.accentPressed);
            push(ImGuiCol_Text, p.textOnAccent);
            break;
        case ButtonStyle::Ghost:
            push(ImGuiCol_Button, clear);
            push(ImGuiCol_ButtonHovered, p.controlHovered);
            push(ImGuiCol_ButtonActive, p.controlActive);
            push(ImGuiCol_Text, p.textDim);
            break;
        case ButtonStyle::Danger:
            push(ImGuiCol_Button, p.control);
            push(ImGuiCol_ButtonHovered, p.dangerSubtle);
            push(ImGuiCol_ButtonActive, p.dangerSubtle);
            push(ImGuiCol_Text, p.danger);
            break;
        case ButtonStyle::Secondary:
            break;
    }
    // A ghost button has no box until it is pointed at.
    if (style == ButtonStyle::Ghost) {
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.f);
    }
    const bool clicked = ImGui::Button(tr(label), size);
    if (style == ButtonStyle::Ghost) {
        ImGui::PopStyleVar();
    }
    ImGui::PopStyleColor(colours);
    return clicked;
}

// ---------------------------------------------------------------------- choices --

bool toggle(const char* label, bool* value) {
    const theme::Palette& p = theme::palette();
    const char* shown = tr(label);
    ImGui::PushID(label);

    const float height = ImGui::GetFrameHeight();
    const float trackHeight = std::floor(height * 0.62f);
    const float trackWidth = std::floor(trackHeight * 1.8f);
    const float gap = ImGui::GetStyle().ItemInnerSpacing.x + 4.f;
    const ImVec2 text = ImGui::CalcTextSize(shown);
    const ImVec2 at = ImGui::GetCursorScreenPos();

    bool changed = false;
    if (ImGui::InvisibleButton("##toggle", ImVec2(trackWidth + gap + text.x, height))) {
        *value = !*value;
        changed = true;
    }
    const bool hovered = ImGui::IsItemHovered();
    const bool held = ImGui::IsItemActive();
    nameForScripts(label);

    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 from(at.x, at.y + (height - trackHeight) * 0.5f);
    const ImVec2 to(from.x + trackWidth, from.y + trackHeight);
    const ImVec4 off = held ? p.controlActive : hovered ? p.controlHovered : p.control;
    const ImVec4 on = held ? p.accentPressed : hovered ? p.accentHover : p.accent;
    draw->AddRectFilled(from, to, u32(*value ? on : off), trackHeight * 0.5f);
    if (!*value) {
        draw->AddRect(from, to, u32(p.border), trackHeight * 0.5f);
    }
    const float radius = trackHeight * 0.5f - 3.f;
    const float knobX = *value ? to.x - trackHeight * 0.5f : from.x + trackHeight * 0.5f;
    draw->AddCircleFilled(ImVec2(knobX, from.y + trackHeight * 0.5f), radius,
                          u32(*value ? p.textOnAccent : p.textDim));
    draw->AddText(ImVec2(at.x + trackWidth + gap, at.y + (height - text.y) * 0.5f),
                  ImGui::GetColorU32(ImGuiCol_Text), shown);
    // The ring a keyboard user follows; a mouse click does not draw it.
    if (ImGui::IsItemFocused() && ImGui::GetIO().NavVisible) {
        draw->AddRect(ImVec2(from.x - 2.f, from.y - 2.f), ImVec2(to.x + 2.f, to.y + 2.f),
                      u32(p.focus), trackHeight * 0.5f + 2.f, 0, 2.f);
    }
    ImGui::PopID();
    return changed;
}

bool segmented(const char* id, int* selected, const char* const* options, int count) {
    const theme::Palette& p = theme::palette();
    bool changed = false;
    ImGui::PushID(id);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(1.f, ImGui::GetStyle().ItemSpacing.y));
    for (int i = 0; i < count; ++i) {
        if (i > 0) {
            ImGui::SameLine();
        }
        const bool on = i == *selected;
        ImGui::PushStyleColor(ImGuiCol_Button, on ? p.selected : p.control);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, on ? p.selected : p.controlHovered);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, p.controlActive);
        ImGui::PushStyleColor(ImGuiCol_Text, on ? p.text : p.textDim);
        if (ImGui::Button(tr(options[i]))) {
            if (*selected != i) {
                *selected = i;
                changed = true;
            }
        }
        ImGui::PopStyleColor(4);
        if (on) {
            // The lit one carries the accent as a line under it: colour is not
            // the only thing that says which is chosen.
            const ImVec2 a = ImGui::GetItemRectMin();
            const ImVec2 b = ImGui::GetItemRectMax();
            ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(a.x, b.y - 2.f), b, u32(p.accent));
        }
    }
    ImGui::PopStyleVar();
    ImGui::PopID();
    return changed;
}

namespace {

// The look of every slider: the ImGui slider is drawn with a see-through frame
// and a thin accent handle, and the well and the fill under it are drawn
// afterwards into the channel behind, knowing where the value ended up. Doing
// it afterwards means the fill is never a frame behind a drag.
template <typename Call>
bool styledSlider(float (*fraction)(const void*, const void*, const void*),
                  const void* value, const void* low, const void* high, Call call) {
    const theme::Palette& p = theme::palette();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    ImDrawListSplitter split;
    split.Split(draw, 2);
    split.SetCurrentChannel(draw, 1);

    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.f, 0.f, 0.f, 0.f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(1.f, 1.f, 1.f, 0.06f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(1.f, 1.f, 1.f, 0.10f));
    ImGui::PushStyleColor(ImGuiCol_SliderGrab, p.accent);
    ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, p.accentHover);
    ImGui::PushStyleVar(ImGuiStyleVar_GrabMinSize, 4.f);
    ImGui::PushStyleVar(ImGuiStyleVar_GrabRounding, 2.f);
    const bool changed = call();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(5);

    const ImVec2 from = ImGui::GetItemRectMin();
    const ImVec2 to = ImGui::GetItemRectMax();
    const bool active = ImGui::IsItemActive();
    const float rounding = ImGui::GetStyle().FrameRounding;
    const float at = std::clamp(fraction(value, low, high), 0.f, 1.f);

    split.SetCurrentChannel(draw, 0);
    draw->AddRectFilled(from, to, u32(p.inset), rounding);
    if (at > 0.f) {
        draw->AddRectFilled(from, ImVec2(from.x + (to.x - from.x) * at, to.y), u32(p.accentDim),
                            rounding);
    }
    draw->AddRect(from, to, u32(active ? p.accent : p.border), rounding);
    split.Merge(draw);
    return changed;
}

float intFraction(const void* value, const void* low, const void* high) {
    const int v = *static_cast<const int*>(value);
    const int lo = *static_cast<const int*>(low);
    const int hi = *static_cast<const int*>(high);
    return hi > lo ? static_cast<float>(v - lo) / static_cast<float>(hi - lo) : 0.f;
}

float floatFraction(const void* value, const void* low, const void* high) {
    const float v = *static_cast<const float*>(value);
    const float lo = *static_cast<const float*>(low);
    const float hi = *static_cast<const float*>(high);
    return hi > lo ? (v - lo) / (hi - lo) : 0.f;
}

} // namespace

bool sliderInt(const char* label, int* value, int low, int high, const char* format,
               ImGuiSliderFlags flags) {
    return styledSlider(intFraction, value, &low, &high, [&] {
        return ImGui::SliderInt(label, value, low, high, format, flags);
    });
}

bool sliderFloat(const char* label, float* value, float low, float high, const char* format,
                 ImGuiSliderFlags flags) {
    return styledSlider(floatFraction, value, &low, &high, [&] {
        return ImGui::SliderFloat(label, value, low, high, format, flags);
    });
}

// ----------------------------------------------------------------------- fields --

float fieldLabelWidth() {
    return std::floor(ImGui::GetFontSize() * 8.5f);
}

void field(const char* label, float labelWidth) {
    const float width = labelWidth > 0.f ? labelWidth : fieldLabelWidth();
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(theme::palette().textDim, "%s", tr(label));
    ImGui::SameLine(width);
    ImGui::SetNextItemWidth(-FLT_MIN);
}

// ----------------------------------------------------------------------- status --

void badge(const char* text, Tone tone) {
    theme::pushText(theme::TextStyle::Caption);
    const char* shown = tr(text);
    const ImVec2 size = ImGui::CalcTextSize(shown);
    const ImVec2 pad(7.f, 2.f);
    const ImVec2 at = ImGui::GetCursorScreenPos();
    const ImVec2 box(size.x + pad.x * 2.f, size.y + pad.y * 2.f);
    ImGui::Dummy(box);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(at, ImVec2(at.x + box.x, at.y + box.y), u32(toneTint(tone)),
                        box.y * 0.5f);
    draw->AddText(ImVec2(at.x + pad.x, at.y + pad.y), u32(toneColour(tone)), shown);
    theme::popText();
}

void banner(Tone tone, const char* text) {
    const theme::Palette& p = theme::palette();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    ImDrawListSplitter split;
    split.Split(draw, 2);
    split.SetCurrentChannel(draw, 1);

    const float padX = 12.f;
    const float padY = 8.f;
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const float startLocalX = ImGui::GetCursorPosX();
    const float width = ImGui::GetContentRegionAvail().x;
    ImGui::BeginGroup();
    ImGui::SetCursorScreenPos(ImVec2(start.x + padX + 3.f, start.y + padY));
    ImGui::PushTextWrapPos(startLocalX + width - padX);
    ImGui::TextUnformatted(tr(text));
    ImGui::PopTextWrapPos();
    ImGui::EndGroup();
    const float bottom = ImGui::GetItemRectMax().y + padY;

    split.SetCurrentChannel(draw, 0);
    const ImVec2 end(start.x + width, bottom);
    const float rounding = theme::metrics().rounding;
    draw->AddRectFilled(start, end, u32(toneTint(tone)), rounding);
    draw->AddRectFilled(start, ImVec2(start.x + 3.f, end.y), u32(toneColour(tone)), rounding);
    (void)p;
    split.Merge(draw);
    ImGui::SetCursorScreenPos(ImVec2(start.x, end.y));
    ImGui::Dummy(ImVec2(width, ImGui::GetStyle().ItemSpacing.y));
}

void keycap(const char* text) {
    const theme::Palette& p = theme::palette();
    theme::pushText(theme::TextStyle::Caption);
    const ImVec2 size = ImGui::CalcTextSize(text);
    const ImVec2 pad(6.f, 2.f);
    const ImVec2 at = ImGui::GetCursorScreenPos();
    const ImVec2 box(std::max(size.x + pad.x * 2.f, size.y + pad.y * 2.f + 2.f),
                     size.y + pad.y * 2.f);
    ImGui::Dummy(box);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 end(at.x + box.x, at.y + box.y);
    draw->AddRectFilled(at, end, u32(p.control), 3.f);
    draw->AddRect(at, end, u32(p.border), 3.f);
    // A thicker line under it: a key stands up from the page.
    draw->AddLine(ImVec2(at.x + 2.f, end.y), ImVec2(end.x - 2.f, end.y), u32(p.border), 2.f);
    draw->AddText(ImVec2(at.x + (box.x - size.x) * 0.5f, at.y + pad.y), u32(p.text), text);
    theme::popText();
}

void emptyState(const char* title, const char* hint) {
    const theme::Palette& p = theme::palette();
    ImGui::Dummy(ImVec2(0.f, 12.f));
    theme::pushText(theme::TextStyle::Strong);
    const char* shown = tr(title);
    const float width = ImGui::CalcTextSize(shown).x;
    const float avail = ImGui::GetContentRegionAvail().x;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.f, (avail - width) * 0.5f));
    ImGui::TextColored(p.textDim, "%s", shown);
    theme::popText();
    ImGui::PushStyleColor(ImGuiCol_Text, p.textMuted);
    ImGui::TextWrapped("%s", tr(hint));
    ImGui::PopStyleColor();
}

// ---------------------------------------------------------------------- dialogs --

bool beginDialog(const char* title, bool* open, DialogSize size) {
    if (open == nullptr || !*open) {
        return false;
    }
    if (!ImGui::IsPopupOpen(title)) {
        ImGui::OpenPopup(title);
    }
    const float k = scaleFactor();
    const float width = (size == DialogSize::Small ? 360.f : size == DialogSize::Large ? 640.f
                                                                                       : 480.f) * k;
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing,
                            ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(width, 0.f), ImVec2(width, FLT_MAX));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(22.f * k, 18.f * k));
    const bool visible = ImGui::BeginPopupModal(
        title, nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_AlwaysAutoResize);
    ImGui::PopStyleVar();
    if (!visible) {
        return false;
    }
    theme::pushText(theme::TextStyle::Title);
    ImGui::TextColored(theme::palette().textBright, "%s", tr(title));
    theme::popText();
    ImGui::Dummy(ImVec2(0.f, 2.f));
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0.f, 6.f));
    // Escape closes every dialog, the way it does in every other program.
    if (ImGui::IsKeyPressed(ImGuiKey_Escape) && !ImGui::GetIO().WantTextInput) {
        *open = false;
        ImGui::CloseCurrentPopup();
    }
    return true;
}

void endDialog() {
    ImGui::EndPopup();
}

bool beginModal(const char* name, bool* open, ImGuiWindowFlags flags) {
    const float k = scaleFactor();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(22.f * k, 18.f * k));
    const bool visible = ImGui::BeginPopupModal(
        name, open,
        flags | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_AlwaysAutoResize);
    ImGui::PopStyleVar();
    if (!visible) {
        return false;
    }
    std::string title = name;
    const size_t id = title.find("###");
    if (id != std::string::npos) {
        title.resize(id);
    }
    theme::pushText(theme::TextStyle::Title);
    ImGui::TextColored(theme::palette().textBright, "%s", tr(title.c_str()));
    theme::popText();
    ImGui::Dummy(ImVec2(0.f, 2.f));
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0.f, 6.f));
    if (open != nullptr && ImGui::IsKeyPressed(ImGuiKey_Escape) &&
        !ImGui::GetIO().WantTextInput) {
        *open = false;
        ImGui::CloseCurrentPopup();
    }
    return true;
}

int dialogButtons(const char* primary, const char* secondary, bool primaryEnabled) {
    const float k = scaleFactor();
    ImGui::Dummy(ImVec2(0.f, 10.f * k));
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0.f, 4.f * k));

    const ImGuiStyle& style = ImGui::GetStyle();
    const auto widthOf = [&](const char* label) {
        return std::max(96.f * k, ImGui::CalcTextSize(tr(label)).x + style.FramePadding.x * 4.f);
    };
    const float first = secondary != nullptr ? widthOf(secondary) : 0.f;
    const float second = widthOf(primary);
    const float total = first + (secondary != nullptr ? style.ItemSpacing.x : 0.f) + second;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() +
                         std::max(0.f, ImGui::GetContentRegionAvail().x - total));

    int result = 0;
    if (secondary != nullptr) {
        if (button(secondary, ButtonStyle::Secondary, ImVec2(first, 0.f))) {
            result = 2;
        }
        ImGui::SameLine();
    }
    ImGui::BeginDisabled(!primaryEnabled);
    if (button(primary, ButtonStyle::Primary, ImVec2(second, 0.f))) {
        result = 1;
    }
    ImGui::EndDisabled();
    // Enter is the primary, unless a text field has it.
    if (result == 0 && primaryEnabled && ImGui::IsKeyPressed(ImGuiKey_Enter) &&
        !ImGui::GetIO().WantTextInput &&
        ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
        result = 1;
    }
    return result;
}

// ---------------------------------------------------------------------- gallery --

void drawGallery(bool* open) {
    if (open == nullptr || !*open) {
        return;
    }
    ImGui::SetNextWindowSize(ImVec2(620.f, 640.f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(tr("Interface kit"), open)) {
        ImGui::End();
        return;
    }
    static bool on = true;
    static bool off = false;
    static int scale = 1;
    static int shape = 0;
    static float slider = 0.4f;
    static int integer = 32;
    static int choice = 0;
    static char text[32] = "hero walk";
    static bool dialog = false;

    theme::sectionHeader("BUTTONS");
    for (ButtonStyle style : { ButtonStyle::Primary, ButtonStyle::Secondary, ButtonStyle::Ghost,
                               ButtonStyle::Danger }) {
        const char* names[] = { "Secondary", "Primary", "Ghost", "Danger" };
        const char* name = names[static_cast<int>(style)];
        ImGui::PushID(name);
        button(name, style);
        ImGui::SameLine();
        ImGui::BeginDisabled();
        button("Disabled", style);
        ImGui::EndDisabled();
        ImGui::PopID();
        if (style != ButtonStyle::Danger) {
            ImGui::SameLine();
        }
    }
    ImGui::NewLine();

    // A dialog, opened from here so it is one click from the top.
    if (button("Open a dialog")) {
        dialog = true;
    }
    if (beginDialog("A dialog", &dialog, DialogSize::Small)) {
        ImGui::TextWrapped("%s", "The title above, the body here, and the buttons below: "
                                 "the primary on the right, Cancel beside it.");
        const int result = dialogButtons("Create");
        if (result != 0) {
            dialog = false;
            ImGui::CloseCurrentPopup();
        }
        endDialog();
    }

    theme::sectionHeader("CHOICES");
    toggle("Switched on", &on);
    toggle("Switched off", &off);
    ImGui::BeginDisabled();
    bool locked = true;
    toggle("Disabled", &locked);
    ImGui::EndDisabled();
    const char* scales[] = { "1x", "2x", "3x", "4x" };
    segmented("scale", &scale, scales, 4);
    const char* shapes[] = { "Round", "Square" };
    segmented("shape", &shape, shapes, 2);

    theme::sectionHeader("FIELDS");
    field("name");
    ImGui::InputText("##name", text, sizeof text);
    field("size");
    ImGui::InputInt("##size", &integer);
    field("opacity");
    ImGui::SliderFloat("##opacity", &slider, 0.f, 1.f);
    field("mode");
    const char* modes[] = { "Normal", "Multiply", "Screen" };
    ImGui::Combo("##mode", &choice, modes, 3);

    theme::sectionHeader("STATUS");
    for (Tone tone : { Tone::Neutral, Tone::Accent, Tone::Success, Tone::Warning, Tone::Danger,
                       Tone::Info }) {
        const char* names[] = { "neutral", "accent", "success", "warning", "danger", "info" };
        badge(names[static_cast<int>(tone)], tone);
        ImGui::SameLine();
    }
    ImGui::NewLine();
    banner(Tone::Info, "A message about the whole panel, in words.");
    banner(Tone::Warning, "This will change every frame of the animation.");
    banner(Tone::Danger, "That file could not be read.");
    banner(Tone::Success, "Exported hero.png at 4x.");
    keycap("Ctrl");
    ImGui::SameLine();
    keycap("Shift");
    ImGui::SameLine();
    keycap("E");

    theme::sectionHeader("TYPE");
    theme::pushText(theme::TextStyle::Display);
    ImGui::TextUnformatted("Display 22");
    theme::popText();
    theme::pushText(theme::TextStyle::Title);
    ImGui::TextUnformatted("Title 17");
    theme::popText();
    theme::pushText(theme::TextStyle::Strong);
    ImGui::TextUnformatted("Strong 14");
    theme::popText();
    ImGui::TextUnformatted("Body 14: the quick brown fox jumps over the lazy dog 0123456789");
    ImGui::PushStyleColor(ImGuiCol_Text, theme::palette().textDim);
    ImGui::TextUnformatted("Secondary: labels and units");
    ImGui::PopStyleColor();
    ImGui::PushStyleColor(ImGuiCol_Text, theme::palette().textMuted);
    ImGui::TextUnformatted("Muted: hints and help");
    ImGui::PopStyleColor();
    theme::pushText(theme::TextStyle::Caption);
    ImGui::TextUnformatted("Caption 12: the status bar and small print");
    theme::popText();

    theme::sectionHeader("SURFACES");
    {
        const Theme& t = theme::current();
        const float swatch = 22.f;
        int column = 0;
        const float avail = ImGui::GetContentRegionAvail().x;
        const int perRow = std::max(1, static_cast<int>(avail / (swatch + 4.f)));
        for (size_t i = 0; i < kTokenCount; ++i) {
            const Token token = static_cast<Token>(i);
            const Rgba c = t[token];
            if (column > 0) {
                ImGui::SameLine(0.f, 4.f);
            }
            ImGui::PushID(static_cast<int>(i));
            ImGui::ColorButton("##token", ImVec4(c.r / 255.f, c.g / 255.f, c.b / 255.f, c.a / 255.f),
                               ImGuiColorEditFlags_NoTooltip, ImVec2(swatch, swatch));
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s  %s", tokenName(token), colourText(c).c_str());
            }
            ImGui::PopID();
            column = (column + 1) % perRow;
        }
    }

    ImGui::End();
}

} // namespace widgets
} // namespace fast
