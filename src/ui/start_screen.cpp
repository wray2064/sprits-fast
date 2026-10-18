// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

#include "ui/start_screen.h"

#include "app/file_io.h"
#include "app/i18n.h"
#include "ui/tabs.h"
#include "ui/theme.h"

#include <SDL3/SDL.h>
#include <imgui.h>

#include <algorithm>
#include <ctime>

namespace fast {

namespace {

constexpr const char* kPopup = "Start";

// A quick new file is one click: the size sprites are usually drawn at, on
// what the New window last said the canvas starts on and which palette.
void quickNew(Editor& editor, CanvasView& canvas, SDL_Window* window, uint32_t size) {
    editor.files.newDocument.width = size;
    editor.files.newDocument.height = size;
    requestAction(editor, canvas, window, PendingAction::NewDocument);
}

void drawActions(Editor& editor, CanvasView& canvas, SDL_Window* window) {
    ImGui::TextColored(theme::palette().textDim, "%s", tr("Start"));
    ImGui::Dummy(ImVec2(0.f, 2.f));

    const float width = ImGui::GetContentRegionAvail().x;
    if (ImGui::Button(tr("New file..."), ImVec2(width, 38.f))) {
        editor.startOpen = false;
        editor.newDocumentOpen = true;
        ImGui::CloseCurrentPopup();
    }
    ImGui::Dummy(ImVec2(0.f, 2.f));
    ImGui::TextColored(theme::palette().textDim, "%s", tr("A blank canvas, right now"));
    for (uint32_t size : { 16u, 32u, 64u, 128u }) {
        const std::string label = std::to_string(size) + " x " + std::to_string(size);
        if (ImGui::Button(label.c_str(), ImVec2((width - 8.f) / 2.f, 0.f))) {
            editor.startOpen = false;
            quickNew(editor, canvas, window, size);
            ImGui::CloseCurrentPopup();
        }
        if (size == 16u || size == 64u) {
            ImGui::SameLine();
        }
    }

    ImGui::Dummy(ImVec2(0.f, 10.f));
    if (ImGui::Button(tr("Open..."), ImVec2(width, 30.f))) {
        editor.startOpen = false;
        requestAction(editor, canvas, window, PendingAction::OpenDialog);
        ImGui::CloseCurrentPopup();
    }
    if (ImGui::Button(tr("Import sprite sheet..."), ImVec2(width, 30.f))) {
        editor.startOpen = false;
        showImportSheetDialog(editor.files, window, editor.doc);
        ImGui::CloseCurrentPopup();
    }
    if (ImGui::Button(tr("Library..."), ImVec2(width, 30.f))) {
        editor.startOpen = false;
        editor.libraryOpen = true;
        editor.libraryStale = true;
        ImGui::CloseCurrentPopup();
    }
}

void drawRecent(Editor& editor, CanvasView& canvas, SDL_Window* window) {
    ImGui::TextColored(theme::palette().textDim, "%s", tr("Recent"));
    ImGui::Dummy(ImVec2(0.f, 2.f));
    if (editor.startRecent.empty()) {
        ImGui::TextDisabled("%s", tr("Files you open or save appear here."));
        return;
    }

    const uint64_t now = static_cast<uint64_t>(std::time(nullptr));
    std::string chosen;
    std::string removed;
    for (size_t i = 0; i < editor.startRecent.size(); ++i) {
        const RecentInfo& info = editor.startRecent[i];
        ImGui::PushID(static_cast<int>(i));

        std::string label = fileName(info.path);
        if (!info.exists) {
            label += " (missing)";
        }
        ImGui::BeginDisabled(!info.exists);
        if (ImGui::Selectable(label.c_str(), false, 0, ImVec2(0.f, 0.f))) {
            chosen = info.path;
        }
        ImGui::EndDisabled();
        if (ImGui::BeginPopupContextItem("recentRow")) {
            if (ImGui::MenuItem(tr("Remove from this list"))) {
                removed = info.path;
            }
            ImGui::EndPopup();
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("%s", info.path.c_str());
        }

        std::string detail = directoryOf(info.path);
        const std::string age = ageText(info.modifiedSeconds, now);
        if (!age.empty()) {
            detail += detail.empty() ? age : "   " + age;
        }
        ImGui::TextDisabled("%s", detail.c_str());
        ImGui::Dummy(ImVec2(0.f, 4.f));
        ImGui::PopID();
    }

    ImGui::Dummy(ImVec2(0.f, 4.f));
    if (ImGui::SmallButton(tr("Clear list"))) {
        editor.files.recent.clear();
        editor.files.recent.save();
        editor.startRecent.clear();
    }

    if (!removed.empty()) {
        editor.files.recent.remove(removed);
        editor.files.recent.save();
        editor.startRecent.erase(
            std::remove_if(editor.startRecent.begin(), editor.startRecent.end(),
                           [&](const RecentInfo& info) { return info.path == removed; }),
            editor.startRecent.end());
    }
    if (!chosen.empty()) {
        editor.startOpen = false;
        requestAction(editor, canvas, window, PendingAction::OpenPath, chosen);
        ImGui::CloseCurrentPopup();
    }
}

} // namespace

void openStartScreen(Editor& editor) {
    editor.startOpen = true;
    editor.startStale = true;
    editor.startTabs = tabCount(editor);
    editor.startPath = editor.doc.path();
}

void drawStartScreen(Editor& editor, CanvasView& canvas, SDL_Window* window) {
    if (!editor.startOpen || editor.askingToRecover) {
        return;
    }
    // Something else changed the document under it -- a file dropped on the
    // window, say -- so the choice has been made.
    if (tabCount(editor) != editor.startTabs || editor.doc.path() != editor.startPath) {
        editor.startOpen = false;
        return;
    }
    if (editor.startStale) {
        editor.startRecent = describeRecent(editor.files.recent.entries());
        editor.startStale = false;
    }

    if (!ImGui::IsPopupOpen(kPopup)) {
        ImGui::OpenPopup(kPopup);
    }
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImVec2 size(std::min(860.f, viewport->WorkSize.x * 0.92f),
                      std::min(520.f, viewport->WorkSize.y * 0.92f));
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(size, ImGuiCond_Always);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                   ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                   ImGuiWindowFlags_NoScrollbar |
                                   ImGuiWindowFlags_NoScrollWithMouse;
    if (!ImGui::BeginPopupModal(kPopup, nullptr, flags)) {
        return;
    }

    ImGui::SetWindowFontScale(1.5f);
    ImGui::TextColored(theme::palette().textBright, "Sprit's'fast");
    ImGui::SetWindowFontScale(1.f);
    ImGui::Dummy(ImVec2(0.f, 6.f));
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0.f, 6.f));

    const float footer = 56.f;
    const float actionsWidth = 250.f;
    ImGui::BeginChild("startActions", ImVec2(actionsWidth, -footer), false);
    drawActions(editor, canvas, window);
    ImGui::EndChild();
    ImGui::SameLine(0.f, 24.f);
    ImGui::BeginChild("startRecent", ImVec2(0.f, -footer), false);
    drawRecent(editor, canvas, window);
    ImGui::EndChild();

    ImGui::Separator();
    ImGui::Dummy(ImVec2(0.f, 4.f));
    if (ImGui::Checkbox(tr("Show this screen when Sprit's'fast starts"), &editor.prefs.showStart)) {
        saveSettings(editor);
    }
    const char* leave = tr("Start with a blank canvas");
    const float leaveWidth = ImGui::CalcTextSize(leave).x + 32.f;
    ImGui::SameLine(ImGui::GetWindowWidth() - leaveWidth - ImGui::GetStyle().WindowPadding.x);
    if (ImGui::Button(leave, ImVec2(leaveWidth, 0.f)) ||
        ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        editor.startOpen = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

} // namespace fast
