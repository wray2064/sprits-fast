// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

// preferences_panel.cpp -- the Preferences window, and the view settings it sets.
// See app_internal.h for how the window's files divide.

#include "app/limits.h"
#include "ui/app_internal.h"

#include "app/palette_tools.h"
#include "ui/keys.h"
#include "app/i18n.h"
#include "ui/appearance.h"
#include "ui/widgets.h"
#include "ui/panels.h"
#include "ui/theme.h"
#include "ui/ui_script.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>
#include <vector>

namespace fast {

// The canvas as the preferences have it: grid on or off, the chequer's
// colours and size, and the grid's colour.
static ImU32 packedColour(uint32_t rgb, int alpha) {
    return IM_COL32((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF, alpha);
}

void applyViewPreferences(const Preferences& prefs, CanvasView& canvas) {
    canvas.setGridVisible(prefs.pixelGrid);
    canvas.setChecker(packedColour(prefs.checkerLight, 255), packedColour(prefs.checkerDark, 255),
                      static_cast<float>(prefs.checkerSize));
    canvas.setGridColour(packedColour(prefs.gridColour, prefs.gridOpacity));
}

// An 0xRRGGBB preference as a colour button; true when it was changed.
static bool editColour(const char* label, uint32_t* rgb) {
    float colour[3] = { static_cast<float>((*rgb >> 16) & 0xFF) / 255.f,
                        static_cast<float>((*rgb >> 8) & 0xFF) / 255.f,
                        static_cast<float>(*rgb & 0xFF) / 255.f };
    if (!ImGui::ColorEdit3(label, colour, ImGuiColorEditFlags_NoInputs)) {
        return false;
    }
    const auto byte = [](float v) {
        return static_cast<uint32_t>(std::clamp(v, 0.f, 1.f) * 255.f + 0.5f);
    };
    *rgb = (byte(colour[0]) << 16) | (byte(colour[1]) << 8) | byte(colour[2]);
    return true;
}

// Preferences: what a new document is, autosave, the grid and the history's
// length on one tab; every command and its keys on the other, where a key is
// given by pressing it. Everything is written the moment it changes.
void drawPreferencesPanel(Editor& editor, CanvasView& canvas, SDL_Window* window) {
    if (!editor.preferencesOpen) {
        return;
    }
    ImGui::SetNextWindowSize(ImVec2(560.f, 720.f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Preferences", &editor.preferencesOpen)) {
        ImGui::End();
        return;
    }
    Preferences& prefs = editor.prefs;
    bool changed = false;
    // Escape closes it, as it does every dialog -- except while a key is being
    // given, when Escape is that key's way out, and while a picker or a text
    // field has the keyboard.
    if (editor.rebinding.empty() && widgets::escapePutsAway()) {
        editor.preferencesOpen = false;
    }
    if (ImGui::BeginTabBar("preference-tabs")) {
        if (ImGui::BeginTabItem(tr("General"))) {
            theme::sectionHeader(tr("A NEW DOCUMENT"));
            int size[2] = { static_cast<int>(prefs.newWidth), static_cast<int>(prefs.newHeight) };
            ImGui::SetNextItemWidth(200.f);
            if (ImGui::InputInt2("size", size)) {
                size[0] = std::clamp(size[0], 1, static_cast<int>(limits().canvasDimension));
                size[1] = std::clamp(size[1], 1, static_cast<int>(limits().canvasDimension));
                if (static_cast<uint64_t>(size[0]) * static_cast<uint64_t>(size[1]) <=
                    limits().canvasPixels) {
                    prefs.newWidth = static_cast<uint32_t>(size[0]);
                    prefs.newHeight = static_cast<uint32_t>(size[1]);
                    changed = true;
                }
            }
            const char* backgrounds[] = { "Transparent", "White", "Black", "The palette's first colour" };
            ImGui::SetNextItemWidth(200.f);
            changed |= ImGui::Combo(tr("background"), &prefs.newBackground, backgrounds, 4);
            const std::vector<PalettePreset>& presets = palettePresets();
            const char* current = prefs.newPreset < 0 ||
                                  prefs.newPreset >= static_cast<int>(presets.size())
                ? "Starter" : presets[static_cast<size_t>(prefs.newPreset)].name.c_str();
            ImGui::SetNextItemWidth(200.f);
            if (ImGui::BeginCombo("palette", current)) {
                if (ImGui::Selectable(tr("Starter"), prefs.newPreset < 0)) {
                    prefs.newPreset = -1;
                    changed = true;
                }
                for (size_t i = 0; i < presets.size(); ++i) {
                    if (ImGui::Selectable(presets[i].name.c_str(),
                                          prefs.newPreset == static_cast<int>(i))) {
                        prefs.newPreset = static_cast<int>(i);
                        changed = true;
                    }
                }
                ImGui::EndCombo();
            }

            theme::sectionHeader(tr("WORKING"));
            if (ImGui::Checkbox(tr("Autosave a copy"), &prefs.autosaveOn)) {
                editor.autosaveOn = prefs.autosaveOn && editor.recovery.active();
                changed = true;
            }
            int minutes = static_cast<int>(prefs.autosaveSeconds / 60);
            ImGui::SetNextItemWidth(200.f);
            if (widgets::sliderInt(tr("every"), &minutes, 1, 30, "%d min")) {
                prefs.autosaveSeconds = static_cast<uint32_t>(minutes) * 60u;
                editor.recovery.setIntervalSeconds(prefs.autosaveSeconds);
                changed = true;
            }
            int history = static_cast<int>(prefs.historyLimit);
            ImGui::SetNextItemWidth(200.f);
            if (widgets::sliderInt(tr("undo steps"), &history, 10, 2000)) {
                prefs.historyLimit = static_cast<uint32_t>(history);
                editor.doc.setHistoryLimit(prefs.historyLimit);
                changed = true;
            }

            theme::sectionHeader(tr("LANGUAGE"));
            {
                // The languages there are catalogues for, English first.
                std::vector<std::string> languages = languagesIn(languageFolder());
                languages.insert(languages.begin(), std::string());
                const auto shown = [](const std::string& code) {
                    return code.empty() ? std::string("English") : code;
                };
                ImGui::SetNextItemWidth(200.f);
                if (ImGui::BeginCombo(tr("Language"), shown(prefs.language).c_str())) {
                    for (const std::string& code : languages) {
                        if (ImGui::Selectable(shown(code).c_str(), code == prefs.language)) {
                            prefs.language = code;
                            applyLanguage(code);
                            changed = true;
                        }
                    }
                    ImGui::EndCombo();
                }
            }

            theme::sectionHeader(tr("THE CANVAS"));
            bool view = false;
            view |= ImGui::Checkbox(tr("Pixel grid"), &prefs.pixelGrid);
            ImGui::SameLine();
            view |= editColour("##grid", &prefs.gridColour);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(120.f);
            view |= widgets::sliderInt(tr("grid opacity"), &prefs.gridOpacity, 0, 255);
            view |= editColour("##checker-light", &prefs.checkerLight);
            ImGui::SameLine();
            view |= editColour("##checker-dark", &prefs.checkerDark);
            ImGui::SameLine();
            ImGui::TextUnformatted("chequer");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(120.f);
            view |= widgets::sliderInt(tr("square"), &prefs.checkerSize, 2, 64, "%d px");
            if (ImGui::Button(tr("Canvas defaults"))) {
                const Preferences defaults;
                prefs.pixelGrid = defaults.pixelGrid;
                prefs.gridColour = defaults.gridColour;
                prefs.gridOpacity = defaults.gridOpacity;
                prefs.checkerLight = defaults.checkerLight;
                prefs.checkerDark = defaults.checkerDark;
                prefs.checkerSize = defaults.checkerSize;
                view = true;
            }
            if (view) {
                applyViewPreferences(prefs, canvas);
                changed = true;
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(tr("Appearance"))) {
            const AppearanceResult appearance = drawAppearanceTab(editor, canvas, window);
            if (appearance.themeChanged) {
                // Canvas colours that were still the theme's have followed it.
                applyViewPreferences(prefs, canvas);
            }
            changed = changed || appearance.changed;
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(tr("Experimental"))) {
            ImGui::TextWrapped("%s", tr("Things that are off until asked for."));
            ImGui::Dummy(ImVec2(0.f, 4.f));
            if (ImGui::Checkbox(tr("Lift the size limits"), &prefs.liftLimits)) {
                setLimitsLifted(prefs.liftLimits);
                changed = true;
            }
            fast::nameForScripts("Lift the size limits");
            const Limits& now = limits();
            ImGui::PushStyleColor(ImGuiCol_Text, theme::palette().textDim);
            ImGui::TextWrapped("%s", tr("The defaults keep the canvas, the timeline and the "
                                        "interface quick on an ordinary machine. Lifted, nothing "
                                        "but what the machine can hold stops a bigger canvas, "
                                        "more frames, more parts or a larger brush. It applies "
                                        "to what is made or opened afterwards, and a very large "
                                        "canvas can be slow and use a great deal of memory."));
            ImGui::PopStyleColor();
            ImGui::Dummy(ImVec2(0.f, 4.f));
            theme::sectionHeader(tr("IN FORCE NOW"));
            ImGui::Text("%s  %u x %u (%llu pixels)", tr("Canvas up to"), now.canvasDimension,
                        now.canvasDimension, static_cast<unsigned long long>(now.canvasPixels));
            ImGui::Text("%s  %zu", tr("Frames"), now.frames);
            ImGui::Text("%s  %zu", tr("Cycles"), now.cycles);
            ImGui::Text("%s  %zu", tr("Cutout parts"), now.parts);
            ImGui::Text("%s  %zu", tr("References"), now.references);
            ImGui::Text("%s  %d", tr("Brush size"), now.brushSize);
            if (prefs.liftLimits) {
                ImGui::PushStyleColor(ImGuiCol_Text, theme::palette().textDim);
                ImGui::TextWrapped("%s", tr("Without it: 4096 x 4096 pixels, 512 frames, 64 cycles, "
                                            "256 parts, 16 references, brush 32."));
                ImGui::PopStyleColor();
            }
            ImGui::EndTabItem();
        }
        const ImGuiTabItemFlags keysFlags =
            editor.preferencesShowKeys ? ImGuiTabItemFlags_SetSelected : 0;
        editor.preferencesShowKeys = false;
        if (ImGui::BeginTabItem(tr("Keys"), nullptr, keysFlags)) {
            // A key being given: the next chord pressed is it, Escape gives
            // up, Backspace removes the chord.
            if (!editor.rebinding.empty()) {
                const CommandInfo* info = findCommand(editor.rebinding);
                ImGui::TextColored(theme::palette().accent, "Press the keys for %s...",
                                   info != nullptr ? tr(info->label) : "?");
                ImGui::TextDisabled("%s", tr("Escape to give up, Backspace to remove this key."));
                Chord chord;
                if (chordPressed(&chord)) {
                    std::vector<Chord> chords = editor.keys.chordsFor(editor.rebinding);
                    const int at = editor.rebindingChord;
                    if (chord.key == "Backspace" && !chord.ctrl && !chord.shift && !chord.alt) {
                        if (at >= 0 && at < static_cast<int>(chords.size())) {
                            chords.erase(chords.begin() + at);
                        }
                        editor.keys.setChords(editor.rebinding, chords);
                        changed = true;
                    } else if (!(chord.key == "Escape")) {
                        if (at >= 0 && at < static_cast<int>(chords.size())) {
                            chords[static_cast<size_t>(at)] = chord;
                        } else if (chords.size() < 4) {
                            chords.push_back(chord);
                        }
                        editor.keys.setChords(editor.rebinding, chords);
                        changed = true;
                    }
                    editor.rebinding.clear();
                }
                if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
                    editor.rebinding.clear();
                }
                ImGui::Separator();
            }
            if (ImGui::Button(tr("Reset every key"))) {
                editor.keys.resetAll();
                changed = true;
            }
            ImGui::BeginChild("keys", ImVec2(0.f, 0.f));
            const char* group = "";
            for (const CommandInfo& info : commandList()) {
                if (std::string(group) != info.group) {
                    group = info.group;
                    // In capitals like the other headings -- in English; a
                    // translation is shown as it was written.
                    std::string header = tr(group);
                    if (header == group) {
                        for (char& ch : header) {
                            ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
                        }
                    }
                    theme::sectionHeader(header.c_str());
                }
                ImGui::PushID(info.id);
                ImGui::TextUnformatted(tr(info.label));
                ImGui::SameLine(190.f);
                const std::vector<Chord>& chords = editor.keys.chordsFor(info.id);
                for (size_t i = 0; i < chords.size(); ++i) {
                    ImGui::PushID(static_cast<int>(i));
                    const std::string text = chordText(chords[i]);
                    // A chord two commands share is marked: pressing it would
                    // fire both.
                    const bool shared = editor.keys.commandsUsing(chords[i]).size() > 1;
                    if (shared) {
                        ImGui::PushStyleColor(ImGuiCol_Text, theme::palette().danger);
                    }
                    if (ImGui::SmallButton(text.c_str())) {
                        editor.rebinding = info.id;
                        editor.rebindingChord = static_cast<int>(i);
                    }
                    if (shared) {
                        ImGui::PopStyleColor();
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("%s", tr("Another command has this key too."));
                        }
                    }
                    ImGui::SameLine();
                    ImGui::PopID();
                }
                if (chords.size() < 4 && ImGui::SmallButton("+")) {
                    editor.rebinding = info.id;
                    editor.rebindingChord = -1;
                }
                if (!editor.keys.isDefault(info.id)) {
                    ImGui::SameLine();
                    if (ImGui::SmallButton(tr("default"))) {
                        editor.keys.reset(info.id);
                        changed = true;
                    }
                }
                ImGui::PopID();
            }
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::End();
    if (changed) {
        editor.files.newDocument.width = prefs.newWidth;
        editor.files.newDocument.height = prefs.newHeight;
        editor.files.newDocument.background = prefs.newBackground;
        editor.files.newDocument.preset = prefs.newPreset;
        saveSettings(editor);
    }
}


} // namespace fast
