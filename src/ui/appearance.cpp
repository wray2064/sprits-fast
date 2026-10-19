// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

#include "ui/appearance.h"

#include "app/file_io.h"
#include "app/i18n.h"
#include "app/theme_store.h"
#include "ui/theme.h"
#include "ui/widgets.h"

#include <SDL3/SDL.h>
#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

namespace fast {

// Defined with the UI scripts.
void nameForScripts(const char* label);

namespace {

uint32_t packRgb(Rgba c) {
    return (static_cast<uint32_t>(c.r) << 16) | (static_cast<uint32_t>(c.g) << 8) | c.b;
}

ImVec4 vec(Rgba c) {
    return ImVec4(c.r / 255.f, c.g / 255.f, c.b / 255.f, c.a / 255.f);
}

// What a person is told a role is called: Fast's own, or one a program built on
// it registered. An id nobody knows is shown as it is.
std::string roleLabel(const std::string& name) {
    Token token;
    if (tokenFromName(name, &token)) {
        return tr(tokenLabel(token));
    }
    if (const ExtraRole* role = findExtraRole(name)) {
        return tr(role->label.c_str());
    }
    return name;
}

// Canvas colours that were still the old theme's follow the new one; ones the
// person chose in Preferences > General stay theirs.
void adoptCanvasColours(Preferences& prefs, const Theme& before, const Theme& after) {
    if (prefs.checkerLight == packRgb(before[Token::CheckerA]) &&
        prefs.checkerDark == packRgb(before[Token::CheckerB])) {
        prefs.checkerLight = packRgb(after[Token::CheckerA]);
        prefs.checkerDark = packRgb(after[Token::CheckerB]);
    }
    if (prefs.gridColour == packRgb(before[Token::GridLine]) &&
        prefs.gridOpacity == before[Token::GridLine].a) {
        prefs.gridColour = packRgb(after[Token::GridLine]);
        prefs.gridOpacity = after[Token::GridLine].a;
    }
}

// Whether a role has an opacity worth editing: the tints, and what is laid over
// the work.
bool hasAlpha(Token token) {
    switch (token) {
        case Token::AccentSubtle: case Token::SuccessSubtle: case Token::WarningSubtle:
        case Token::DangerSubtle: case Token::InfoSubtle: case Token::GridLine:
        case Token::Guide: case Token::OnionPast: case Token::OnionFuture:
            return true;
        default:
            return false;
    }
}

// A name no built-in theme and no file of the person's has.
std::string uniqueName(const std::string& folder, const std::string& wanted) {
    std::string name = wanted.empty() ? std::string("My theme") : wanted;
    std::string candidate = name;
    for (int n = 2; n < 100; ++n) {
        if (uniqueThemeId(folder, candidate) == themeIdFromName(candidate)) {
            return candidate;
        }
        candidate = name + " " + std::to_string(n);
    }
    return candidate;
}

void reloadCustom(Editor& editor) {
    editor.appearance.custom = loadCustomThemes(themeFolderFor(editor));
}

// A change to the working copy: shown at once on the real window, and noted as
// unsaved when it no longer matches the theme it started from.
void touched(Editor& editor, const Theme& before, AppearanceResult* result) {
    Editor::AppearanceState& a = editor.appearance;
    adoptCanvasColours(editor.prefs, before, a.working);
    theme::setTheme(a.working);
    Theme base;
    a.dirty = !findTheme(themeFolderFor(editor), a.baseId, &base) || !sameColours(base, a.working);
    result->changed = true;
    result->themeChanged = true;
}

} // namespace

std::string themeFolderFor(const Editor& editor) {
    return editor.settingsLoaded ? themesFolder() : std::string("ui_selftest_themes");
}

void selectTheme(Editor& editor, const Theme& chosen) {
    Editor::AppearanceState& a = editor.appearance;
    const Theme before = a.working;
    a.working = chosen;
    a.baseId = chosen.id;
    a.dirty = false;
    a.loaded = true;
    editor.prefs.themeId = chosen.id;
    editor.prefs.lightTheme = !chosen.dark;
    adoptCanvasColours(editor.prefs, before, chosen);
    theme::setTheme(chosen);
}

void loadAppearance(Editor& editor) {
    const std::string folder = themeFolderFor(editor);
    editor.appearance.custom = loadCustomThemes(folder);
    Theme chosen;
    if (!findTheme(folder, editor.prefs.themeId, &chosen)) {
        chosen = *findBuiltinTheme("slate");
    }
    selectTheme(editor, chosen);
}

void editRole(Editor& editor, Token token, Rgba colour) {
    const Theme before = editor.appearance.working;
    editor.appearance.working[token] = colour;
    AppearanceResult ignored;
    touched(editor, before, &ignored);
}

void editExtraRole(Editor& editor, const std::string& id, Rgba colour) {
    if (findExtraRole(id) == nullptr) {
        return;
    }
    const Theme before = editor.appearance.working;
    editor.appearance.working.extra[id] = colour;
    AppearanceResult ignored;
    touched(editor, before, &ignored);
}

void importThemeFrom(Editor& editor, const std::string& path) {
    Editor::AppearanceState& a = editor.appearance;
    std::vector<uint8_t> bytes;
    std::string error;
    if (!readFile(path, bytes, &error)) {
        a.message = "Could not read " + fileName(path) + ": " + error;
        editor.say(a.message);
        return;
    }
    if (bytes.size() > kMaxThemeFileBytes) {
        a.message = fileName(path) + " is too large to be a theme.";
        editor.say(a.message);
        return;
    }
    Theme theme;
    ThemeReport report;
    if (!parseTheme(std::string(bytes.begin(), bytes.end()), &theme, &report)) {
        a.message = fileName(path) + " is not a theme file.";
        editor.say(a.message);
        return;
    }
    const std::string folder = themeFolderFor(editor);
    theme.name = uniqueName(folder, theme.name);
    std::string id;
    if (!saveCustomTheme(folder, theme, &id, &error)) {
        a.message = "Could not keep it: " + error;
        editor.say(a.message);
        return;
    }
    reloadCustom(editor);
    Theme kept;
    if (findTheme(folder, id, &kept)) {
        selectTheme(editor, kept);
    }
    a.message = "Imported " + kept.name;
    if (report.unknownKeys > 0 || report.badColours > 0) {
        a.message += " (" + std::to_string(report.unknownKeys) + " line(s) not understood, " +
                     std::to_string(report.badColours) + " colour(s) skipped)";
    }
    editor.say(a.message);
}

void exportThemeTo(Editor& editor, const std::string& path) {
    Editor::AppearanceState& a = editor.appearance;
    const std::string target = hasExtension(path, ".lstheme") ? path : withExtension(path, ".lstheme");
    const std::string text = serializeTheme(a.working);
    std::string error;
    if (writeFileAtomic(target, std::vector<uint8_t>(text.begin(), text.end()), &error)) {
        a.message = "Exported " + fileName(target);
    } else {
        a.message = "Could not write " + fileName(target) + ": " + error;
    }
    editor.say(a.message);
}

AppearanceResult drawAppearanceTab(Editor& editor, CanvasView& /*canvas*/, SDL_Window* window) {
    Editor::AppearanceState& a = editor.appearance;
    if (!a.loaded) {
        loadAppearance(editor);
    }
    AppearanceResult result;
    const std::string folder = themeFolderFor(editor);
    const theme::Palette& p = theme::palette();
    Theme base;
    const bool haveBase = findTheme(folder, a.baseId, &base);
    const bool builtinBase = findBuiltinTheme(a.baseId) != nullptr;

    // ----------------------------------------------------------------- theme --
    theme::sectionHeader("THEME");
    widgets::field("theme");
    const std::string shown = a.working.name + (a.dirty ? std::string(" (edited)") : std::string());
    if (ImGui::BeginCombo("##theme", shown.c_str(), ImGuiComboFlags_HeightLargest)) {
        for (const Theme& choice : builtinThemes()) {
            if (ImGui::Selectable(choice.name.c_str(), choice.id == a.baseId && !a.dirty)) {
                selectTheme(editor, choice);
                result.changed = result.themeChanged = true;
            }
        }
        if (!a.custom.empty()) {
            ImGui::Separator();
            ImGui::TextDisabled("%s", tr("Your themes"));
            for (const Theme& choice : a.custom) {
                if (ImGui::Selectable(choice.name.c_str(), choice.id == a.baseId && !a.dirty)) {
                    selectTheme(editor, choice);
                    result.changed = result.themeChanged = true;
                }
            }
        }
        ImGui::EndCombo();
    }
    nameForScripts("theme");

    if (a.dirty) {
        ImGui::Dummy(ImVec2(0.f, 4.f));
        widgets::banner(widgets::Tone::Info,
                        "You have changed this theme. Save it as a theme of your own to keep "
                        "the changes.");
        widgets::field("name");
        ImGui::InputTextWithHint("##themename", tr("a name for it"), a.saveName,
                                 sizeof(a.saveName));
        ImGui::Dummy(ImVec2(0.f, 2.f));
        if (widgets::button("Save as new theme", widgets::ButtonStyle::Primary)) {
            std::string name = a.saveName[0] != '\0' ? std::string(a.saveName)
                                                     : a.working.name + " copy";
            Theme toSave = a.working;
            toSave.name = uniqueName(folder, name);
            toSave.id = themeIdFromName(toSave.name);
            std::string id;
            std::string error;
            if (saveCustomTheme(folder, toSave, &id, &error)) {
                reloadCustom(editor);
                a.working = toSave;
                a.baseId = id;
                a.dirty = false;
                editor.prefs.themeId = id;
                theme::setTheme(a.working);
                a.saveName[0] = '\0';
                a.message = "Saved as " + toSave.name;
                result.changed = true;
            } else {
                a.message = "Could not save it: " + error;
            }
        }
        if (haveBase && !builtinBase) {
            ImGui::SameLine();
            if (widgets::button("Update this theme")) {
                Theme toSave = a.working;
                toSave.name = base.name;
                toSave.id = base.id;
                std::string id;
                std::string error;
                if (saveCustomTheme(folder, toSave, &id, &error)) {
                    reloadCustom(editor);
                    a.working = toSave;
                    a.dirty = false;
                    a.message = "Updated " + toSave.name;
                    result.changed = true;
                } else {
                    a.message = "Could not save it: " + error;
                }
            }
        }
        ImGui::SameLine();
        if (widgets::button("Discard changes", widgets::ButtonStyle::Ghost) && haveBase) {
            selectTheme(editor, base);
            result.changed = result.themeChanged = true;
        }
    }

    ImGui::Dummy(ImVec2(0.f, 4.f));
    if (widgets::button("Import...")) {
        showImportThemeDialog(editor.files, window);
    }
    ImGui::SameLine();
    if (widgets::button("Export...")) {
        showExportThemeDialog(editor.files, window, themeIdFromName(a.working.name) + ".lstheme");
    }
    if (haveBase && !builtinBase) {
        ImGui::SameLine();
        if (widgets::button("Delete theme", widgets::ButtonStyle::Danger)) {
            ImGui::OpenPopup("delete-theme");
        }
    }
    if (ImGui::BeginPopup("delete-theme")) {
        ImGui::TextUnformatted(tr("Delete this theme's file? The program goes back to Slate."));
        if (widgets::button("Delete", widgets::ButtonStyle::Danger)) {
            const std::string gone = a.baseId;
            deleteCustomTheme(folder, gone);
            reloadCustom(editor);
            selectTheme(editor, *findBuiltinTheme("slate"));
            a.message = "Deleted " + gone;
            result.changed = result.themeChanged = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (widgets::button("Keep it")) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    if (!a.message.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, p.textMuted);
        ImGui::TextWrapped("%s", a.message.c_str());
        ImGui::PopStyleColor();
    }

    // ---------------------------------------------------------------- accent --
    ImGui::Dummy(ImVec2(0.f, 4.f));
    theme::sectionHeader("ACCENT");
    {
        const float side = std::round(ImGui::GetFrameHeight() * 1.05f);
        for (const AccentPreset& preset : accentPresets()) {
            ImGui::PushID(preset.name);
            // The ring marks the preset the theme's accent came from.
            Theme probe = a.working;
            applyAccent(&probe, preset.colour);
            const bool current = probe[Token::Accent] == a.working[Token::Accent];
            if (current) {
                ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 2.f);
                ImGui::PushStyleColor(ImGuiCol_Border, p.textBright);
            }
            if (ImGui::ColorButton("##accent", vec(preset.colour), ImGuiColorEditFlags_NoTooltip,
                                   ImVec2(side, side))) {
                const Theme before = a.working;
                applyAccent(&a.working, preset.colour);
                touched(editor, before, &result);
            }
            if (current) {
                ImGui::PopStyleColor();
                ImGui::PopStyleVar();
            }
            const std::string name = std::string("##accent-") + preset.name;
            nameForScripts(name.c_str());
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", tr(preset.name));
            }
            ImGui::PopID();
            ImGui::SameLine(0.f, 4.f);
        }
        float rgb[3] = { a.working[Token::Accent].r / 255.f, a.working[Token::Accent].g / 255.f,
                         a.working[Token::Accent].b / 255.f };
        if (ImGui::ColorEdit3("##accent-custom", rgb,
                              ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel)) {
            const Theme before = a.working;
            applyAccent(&a.working,
                        Rgba{ static_cast<uint8_t>(std::lround(rgb[0] * 255.f)),
                              static_cast<uint8_t>(std::lround(rgb[1] * 255.f)),
                              static_cast<uint8_t>(std::lround(rgb[2] * 255.f)), 255 });
            touched(editor, before, &result);
        }
        nameForScripts("##accent-custom");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", tr("Any colour. It is moved only as far as it must be to "
                                       "be seen and to carry text."));
        }
        ImGui::NewLine();
    }

    // ------------------------------------------------------------- interface --
    // Before the long list of roles, so that the size and the density are
    // within reach without scrolling past forty colours.
    ImGui::Dummy(ImVec2(0.f, 4.f));
    theme::sectionHeader("INTERFACE");
    {
        widgets::field("interface size");
        int percent = editor.prefs.uiScale;
        if (widgets::sliderInt("##interfacesize", &percent, 75, 150, "%d%%",
                               ImGuiSliderFlags_AlwaysClamp)) {
            percent = std::clamp(percent, 75, 150);
            editor.prefs.uiScale = percent;
            theme::setScale(static_cast<float>(percent) / 100.f);
            result.changed = true;
        }
        nameForScripts("interface size");
        widgets::field("");
        if (ImGui::SmallButton(tr("Reset##scale"))) {
            editor.prefs.uiScale = 100;
            theme::setScale(1.f);
            result.changed = true;
        }
        // The panels were sized for the interface size they were built at, and
        // keep their widths when it changes -- a panel too narrow for its
        // contents scrolls sideways -- so the way to sized ones is said here.
        if (editor.panels.builtAtScale > 0.f &&
            std::fabs(editor.panels.builtAtScale - theme::scale()) > 0.04f) {
            widgets::banner(widgets::Tone::Info,
                            "The panels keep the widths they had. Window > Layouts, then one "
                            "of the arrangements, sizes them for this interface size.");
        }
        widgets::field("density");
        const char* densities[] = { "Compact", "Comfortable", "Spacious" };
        if (widgets::segmented("density", &editor.prefs.density, densities, 3)) {
            theme::setDensity(static_cast<theme::Density>(editor.prefs.density));
            result.changed = true;
        }
    }

    // ----------------------------------------------------------- readability --
    ImGui::Dummy(ImVec2(0.f, 4.f));
    theme::sectionHeader("READABILITY");
    const std::vector<ContrastIssue> issues = checkTheme(a.working);
    if (issues.empty()) {
        widgets::banner(widgets::Tone::Success,
                        "Every text and control colour meets its contrast target.");
    } else {
        widgets::banner(widgets::Tone::Warning,
                        issues.size() == 1 ? "One colour pair is hard to read."
                                           : "Some colour pairs are hard to read.");
        for (const ContrastIssue& issue : issues) {
            const size_t on = issue.what.find(" on ");
            std::string line = issue.what;
            if (on != std::string::npos) {
                line = roleLabel(issue.what.substr(0, on)) + " " + tr("on") + " " +
                       roleLabel(issue.what.substr(on + 4));
            }
            char numbers[48];
            std::snprintf(numbers, sizeof numbers, "   %.1f : %.1f", issue.ratio, issue.required);
            // In the strongest text, not in danger: a theme whose danger colour
            // is the one that cannot be read must still be able to say so.
            ImGui::TextColored(p.textBright, "%s%s", line.c_str(), numbers);
        }
        if (widgets::button("Fix them", widgets::ButtonStyle::Primary)) {
            const Theme before = a.working;
            const int moved = fixContrast(&a.working);
            touched(editor, before, &result);
            a.message = "Moved " + std::to_string(moved) + " colour(s) to the nearest that can be read.";
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", tr("Moves each text and mark colour that misses its target to "
                                       "the nearest that meets it. Surfaces are left as you set them."));
        }
    }

    // ---------------------------------------------------------------- colours --
    ImGui::Dummy(ImVec2(0.f, 4.f));
    theme::sectionHeader("COLOURS");
    // One row: a picker, the role's name, the numbers where it misses a target,
    // its hex, and a reset where it differs from the theme it began as. Fast's
    // roles and a program's own are drawn by the same code, so they look and
    // behave alike.
    const auto roleRow = [&](int pushId, const std::string& name, const std::string& label,
                             bool alpha, Rgba current, bool changedFromBase, Rgba baseColour,
                             const std::function<void(Rgba)>& assign) {
        ImGui::PushID(pushId);
        float rgba[4] = { current.r / 255.f, current.g / 255.f, current.b / 255.f,
                          current.a / 255.f };
        const ImGuiColorEditFlags flags =
            ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel |
            (alpha ? (ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf)
                   : ImGuiColorEditFlags_NoAlpha);
        if (ImGui::ColorEdit4("##role", rgba, flags)) {
            assign(Rgba{ static_cast<uint8_t>(std::lround(rgba[0] * 255.f)),
                         static_cast<uint8_t>(std::lround(rgba[1] * 255.f)),
                         static_cast<uint8_t>(std::lround(rgba[2] * 255.f)),
                         alpha ? static_cast<uint8_t>(std::lround(rgba[3] * 255.f))
                               : static_cast<uint8_t>(255) });
        }
        const std::string scriptName = "##role-" + name;
        nameForScripts(scriptName.c_str());
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label.c_str());

        // Where the role fails a target, the numbers; and what it is, quietly,
        // at the right.
        for (const ContrastIssue& issue : issues) {
            if (issue.what.compare(0, name.size() + 4, name + " on ") == 0) {
                ImGui::SameLine();
                char numbers[32];
                std::snprintf(numbers, sizeof numbers, "%.1f/%.1f", issue.ratio, issue.required);
                widgets::badge(numbers, widgets::Tone::Danger);
                break;
            }
        }
        const std::string hex = colourText(current);
        const float hexWidth = ImGui::CalcTextSize(hex.c_str()).x;
        const float resetWidth = changedFromBase ? ImGui::CalcTextSize(tr("reset")).x +
                                                       ImGui::GetStyle().FramePadding.x * 2.f + 8.f
                                                 : 0.f;
        ImGui::SameLine(std::max(ImGui::GetCursorPosX() + 8.f,
                                 ImGui::GetWindowWidth() - hexWidth - resetWidth -
                                     ImGui::GetStyle().WindowPadding.x - 4.f));
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(p.textMuted, "%s", hex.c_str());
        if (changedFromBase) {
            ImGui::SameLine();
            if (widgets::button("reset", widgets::ButtonStyle::Ghost)) {
                assign(baseColour);
            }
        }
        ImGui::PopID();
    };

    const char* group = "";
    bool open = true;
    for (size_t i = 0; i < kTokenCount; ++i) {
        const Token token = static_cast<Token>(i);
        if (std::string(tokenGroup(token)) != group) {
            group = tokenGroup(token);
            open = theme::foldingHeader(group);
        }
        if (!open) {
            continue;
        }
        roleRow(static_cast<int>(i), tokenName(token), tr(tokenLabel(token)), hasAlpha(token),
                a.working[token], haveBase && base[token] != a.working[token], base[token],
                [&](Rgba colour) {
                    const Theme before = a.working;
                    a.working[token] = colour;
                    touched(editor, before, &result);
                });
    }
    // The roles a program built on Fast added, under the groups it gave them.
    int extraIndex = 0;
    std::string extraGroup;
    for (const ExtraRole& role : extraRoles()) {
        if (role.group != extraGroup || extraIndex == 0) {
            extraGroup = role.group;
            open = theme::foldingHeader(extraGroup.c_str());
        }
        ++extraIndex;
        if (!open) {
            continue;
        }
        const std::string id = role.id;
        const Rgba current = a.working.extraColour(id);
        const Rgba baseColour = haveBase ? base.extraColour(id) : current;
        roleRow(1000 + extraIndex, id, tr(role.label.c_str()), role.alpha, current,
                haveBase && baseColour != current, baseColour, [&, id](Rgba colour) {
                    const Theme before = a.working;
                    a.working.extra[id] = colour;
                    touched(editor, before, &result);
                });
    }
    ImGui::Dummy(ImVec2(0.f, 4.f));
    return result;
}

} // namespace fast
