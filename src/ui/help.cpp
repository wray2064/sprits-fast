// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

#include "ui/help.h"

#include "app/i18n.h"
#include "ui/keys.h"
#include "ui/start_screen.h"
#include "ui/theme.h"
#include "ui/widgets.h"

#include <SDL3/SDL.h>
#include <imgui.h>

#include <algorithm>
#include <cctype>

namespace fast {

namespace {

std::string lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

// Whether a filter (already lower case) is in a command's label, group or keys.
bool matches(const CommandInfo& command, const std::string& keys, const std::string& filter) {
    return filter.empty() || lower(command.label).find(filter) != std::string::npos ||
           lower(command.group).find(filter) != std::string::npos ||
           lower(keys).find(filter) != std::string::npos;
}

// What the program is built on, and the licence each carries: the same facts
// as NOTICE, in words that fit a window.
struct Credit {
    const char* name;
    const char* licence;
};
const Credit kCredits[] = {
    { "LiveSprite engine", "Business Source License 1.1, converting to Apache-2.0" },
    { "SDL3", "zlib licence" },
    { "Dear ImGui", "MIT licence" },
    { "stb_image, stb_image_write", "Public domain, or MIT" },
};

// A link that opens in the person's browser when it is pressed, and says where
// it goes on hover. Nothing is opened until it is pressed.
void link(const char* label, const std::string& url) {
    if (url.empty()) {
        return;
    }
    if (ImGui::Button(label)) {
        SDL_OpenURL(url.c_str());
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", url.c_str());
    }
    ImGui::SameLine();
}

void drawAbout(Editor& editor, SDL_Window* window) {
    if (!editor.aboutOpen) {
        return;
    }
    const ProgramInfo& info = programInfo();
    if (!widgets::beginDialog("About", &editor.aboutOpen, widgets::DialogSize::Medium)) {
        return;
    }
    const float logoSize = 64.f;
    if (SDL_Texture* logo = programLogo(window)) {
        const ImVec2 at = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddImage(reinterpret_cast<ImTextureID>(logo), at,
                                             ImVec2(at.x + logoSize, at.y + logoSize));
        ImGui::Dummy(ImVec2(logoSize, logoSize));
        ImGui::SameLine(0.f, 14.f);
    }
    ImGui::BeginGroup();
    ImGui::Dummy(ImVec2(0.f, 4.f));
    theme::pushText(theme::TextStyle::Display);
    ImGui::TextColored(theme::palette().textBright, "%s", info.title.c_str());
    theme::popText();
    if (!info.version.empty()) {
        ImGui::TextColored(theme::palette().textDim, "%s %s", tr("Version"), info.version.c_str());
    }
    ImGui::EndGroup();

    ImGui::Dummy(ImVec2(0.f, 8.f));
    ImGui::TextWrapped("%s",
        tr("Every mark is kept as geometry and only turned into pixels when it is shown or "
           "exported."));
    if (!info.licence.empty()) {
        ImGui::Dummy(ImVec2(0.f, 4.f));
        ImGui::TextColored(theme::palette().textDim, "%s", tr("Licence"));
        ImGui::TextWrapped("%s", info.licence.c_str());
    }

    ImGui::Dummy(ImVec2(0.f, 6.f));
    ImGui::TextColored(theme::palette().textDim, "%s", tr("Built on"));
    for (const Credit& credit : kCredits) {
        ImGui::BulletText("%s  --  %s", credit.name, credit.licence);
    }

    if (!info.homepage.empty() || !info.issues.empty()) {
        ImGui::Dummy(ImVec2(0.f, 8.f));
        link(tr("Project page"), info.homepage);
        link(tr("Report a problem"), info.issues);
        ImGui::NewLine();
    }
    if (widgets::dialogButtons("Close", nullptr) != 0) {
        editor.aboutOpen = false;
        ImGui::CloseCurrentPopup();
    }
    widgets::endDialog();
}

void drawShortcuts(Editor& editor) {
    if (!editor.shortcutsOpen) {
        return;
    }
    ImGui::SetNextWindowSize(ImVec2(440.f, 520.f), ImGuiCond_Appearing);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing,
                            ImVec2(0.5f, 0.5f));
    if (!ImGui::Begin(tr("Keyboard shortcuts"), &editor.shortcutsOpen,
                      ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }
    ImGui::SetNextItemWidth(-1.f);
    ImGui::InputTextWithHint("##shortcutFilter", tr("Type to narrow the list"),
                             editor.shortcutsFilter, sizeof(editor.shortcutsFilter));
    ImGui::Dummy(ImVec2(0.f, 4.f));

    const std::string filter = lower(editor.shortcutsFilter);
    const float footer = ImGui::GetFrameHeightWithSpacing() + 6.f;
    ImGui::BeginChild("shortcutList", ImVec2(0.f, -footer), false);
    std::string group;
    int shown = 0;
    for (const CommandInfo& command : commandList()) {
        const std::string keys = keysLabel(editor.keys, command.id);
        if (keys.empty()) {
            continue;
        }
        if (!matches(command, keys, filter)) {
            continue;
        }
        if (group != command.group) {
            group = command.group;
            ImGui::Dummy(ImVec2(0.f, 4.f));
            theme::sectionHeader(tr(command.group));
        }
        // A row is a way to change that key: pressing it opens the Keys tab.
        ImGui::PushID(command.id);
        if (ImGui::Selectable(tr(command.label))) {
            editor.preferencesOpen = true;
            editor.preferencesShowKeys = true;
        }
        ImGui::PopID();
        ImGui::SameLine(ImGui::GetWindowWidth() * 0.55f);
        ImGui::TextColored(theme::palette().textDim, "%s", keys.c_str());
        ++shown;
    }
    if (shown == 0) {
        ImGui::TextDisabled("%s", tr("No shortcut matches."));
    }
    ImGui::EndChild();

    if (ImGui::Button(tr("Change shortcuts..."))) {
        editor.preferencesOpen = true;
        editor.preferencesShowKeys = true;
    }
    ImGui::End();
}

// Command search: a few letters, the commands that match, Enter or a click to
// do the first or the one pointed at. It lists the same commands the keymap
// does, so everything with a key can be found without one.
void drawCommandSearch(Editor& editor) {
    if (!editor.commandSearchOpen) {
        return;
    }
    ImGui::SetNextWindowSize(ImVec2(460.f, 380.f), ImGuiCond_Appearing);
    ImGui::SetNextWindowPos(ImVec2(ImGui::GetMainViewport()->GetCenter().x,
                                   ImGui::GetMainViewport()->WorkPos.y + 90.f),
                            ImGuiCond_Appearing, ImVec2(0.5f, 0.f));
    if (!ImGui::Begin(tr("Search commands"), &editor.commandSearchOpen,
                      ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }
    if (editor.commandSearchFresh) {
        ImGui::SetKeyboardFocusHere();
        editor.commandSearchFresh = false;
    }
    ImGui::SetNextItemWidth(-1.f);
    if (ImGui::InputTextWithHint("##commandSearch", tr("Type what you want to do"),
                                 editor.commandSearchText, sizeof(editor.commandSearchText))) {
        editor.commandSearchRow = 0;
    }

    const std::string filter = lower(editor.commandSearchText);
    std::vector<const CommandInfo*> found;
    for (const CommandInfo& command : commandList()) {
        if (matches(command, std::string(), filter) ||
            lower(tr(command.label)).find(filter) != std::string::npos) {
            found.push_back(&command);
        }
    }
    if (!found.empty()) {
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) {
            editor.commandSearchRow = std::min(editor.commandSearchRow + 1,
                                               static_cast<int>(found.size()) - 1);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) {
            editor.commandSearchRow = std::max(editor.commandSearchRow - 1, 0);
        }
        editor.commandSearchRow = std::clamp(editor.commandSearchRow, 0,
                                             static_cast<int>(found.size()) - 1);
    }

    const CommandInfo* chosen = nullptr;
    ImGui::Dummy(ImVec2(0.f, 4.f));
    ImGui::BeginChild("commandResults", ImVec2(0.f, 0.f), false);
    for (size_t i = 0; i < found.size(); ++i) {
        const CommandInfo& command = *found[i];
        ImGui::PushID(command.id);
        const bool row = static_cast<int>(i) == editor.commandSearchRow;
        if (ImGui::Selectable(tr(command.label), row)) {
            chosen = &command;
        }
        if (row && (ImGui::IsKeyPressed(ImGuiKey_DownArrow) ||
                    ImGui::IsKeyPressed(ImGuiKey_UpArrow))) {
            ImGui::SetScrollHereY();     // the arrow keys keep the chosen one in view
        }
        const std::string keys = keysLabel(editor.keys, command.id);
        ImGui::SameLine(ImGui::GetWindowWidth() * 0.62f);
        ImGui::TextColored(theme::palette().textDim, "%s  %s", tr(command.group), keys.c_str());
        ImGui::PopID();
    }
    if (found.empty()) {
        ImGui::TextDisabled("%s", tr("No command matches."));
    }
    ImGui::EndChild();

    if (chosen == nullptr && !found.empty() && ImGui::IsKeyPressed(ImGuiKey_Enter) &&
        ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
        chosen = found[static_cast<size_t>(editor.commandSearchRow)];
    }
    if (chosen != nullptr) {
        editor.runCommand = chosen->id;
        editor.commandSearchOpen = false;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Escape) &&
        ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
        editor.commandSearchOpen = false;
    }
    ImGui::End();
}

} // namespace

void openCommandSearch(Editor& editor) {
    editor.commandSearchOpen = true;
    editor.commandSearchFresh = true;
    editor.commandSearchText[0] = '\0';
    editor.commandSearchRow = 0;
}

void drawHelpMenu(Editor& editor) {
    if (!ImGui::BeginMenu(tr("Help"))) {
        return;
    }
    if (ImGui::MenuItem(tr("Search commands..."), keysLabel(editor.keys, "app.search").c_str())) {
        openCommandSearch(editor);
    }
    ImGui::Separator();
    if (ImGui::MenuItem(tr("Keyboard shortcuts..."))) {
        editor.shortcutsOpen = true;
    }
    ImGui::Separator();
    if (ImGui::MenuItem(tr("About..."))) {
        editor.aboutOpen = true;
    }
    ImGui::EndMenu();
}

void drawHelpWindows(Editor& editor, SDL_Window* window) {
    drawAbout(editor, window);
    drawShortcuts(editor);
    drawCommandSearch(editor);
}

} // namespace fast
