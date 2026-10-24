// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

#include "ui/stage_bar.h"

#include "app/i18n.h"
#include "ui/layout.h"
#include "ui/theme.h"

#include <string>

namespace fast {

float stageBarHeight(const Editor& editor) {
    if (editor.stages.empty()) {
        return 0.f;
    }
    return ImGui::GetFrameHeight() + theme::px(12.f);
}

void drawStageBar(Editor& editor, float height) {
    if (height <= 0.f) {
        return;
    }
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const theme::Palette& colours = theme::palette();
    const int count = static_cast<int>(editor.stages.size());

    // Ctrl+1 .. Ctrl+9, unless a field is taking the keys.
    if (!ImGui::GetIO().WantTextInput && ImGui::GetIO().KeyCtrl && !ImGui::GetIO().KeyAlt) {
        for (int i = 0; i < count && i < 9; ++i) {
            if (ImGui::IsKeyPressed(static_cast<ImGuiKey>(ImGuiKey_1 + i), false)) {
                useStage(editor, i);
            }
        }
    }

    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize({ viewport->WorkSize.x, height });
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { theme::px(8.f), theme::px(6.f) });
    ImGui::PushStyleColor(ImGuiCol_WindowBg, colours.windowBackground);
    ImGui::Begin("##stagebar", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus |
                     ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoScrollbar |
                     ImGuiWindowFlags_NoScrollWithMouse);
    for (int i = 0; i < count; ++i) {
        const Stage& stage = editor.stages[static_cast<size_t>(i)];
        const bool here = i == editor.stage;
        if (i > 0) {
            ImGui::SameLine();
            ImGui::TextColored(colours.textMuted, ">");
            ImGui::SameLine();
        }
        ImGui::PushStyleColor(ImGuiCol_Button, here ? colours.accent : colours.control);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, here ? colours.accentHover : colours.controlHovered);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, here ? colours.accentPressed : colours.controlActive);
        ImGui::PushStyleColor(ImGuiCol_Text, here ? colours.textOnAccent : colours.text);
        const std::string label = std::to_string(i + 1) + "  " + tr(stage.title.c_str()) +
                                  "###stage." + stage.id;
        if (ImGui::Button(label.c_str())) {
            useStage(editor, i);
        }
        ImGui::PopStyleColor(4);
    }
    if (editor.stage >= 0 && editor.stage < count) {
        if (editor.stageStatus) {
            const std::string status = editor.stageStatus(editor);
            if (!status.empty()) {
                ImGui::SameLine(0.f, theme::px(16.f));
                ImGui::AlignTextToFramePadding();
                ImGui::TextColored(colours.textDim, "%s", status.c_str());
            }
        }
        if (editor.stage + 1 < count) {
            const Stage& next = editor.stages[static_cast<size_t>(editor.stage + 1)];
            const std::string label = std::string(tr("Next")) + ": " + tr(next.title.c_str()) +
                                      "  >###stage.next";
            const float wide = ImGui::CalcTextSize(label.c_str(), nullptr, true).x +
                               ImGui::GetStyle().FramePadding.x * 2.f;
            ImGui::SameLine(ImGui::GetWindowWidth() - wide - theme::px(10.f));
            if (ImGui::Button(label.c_str())) {
                useStage(editor, editor.stage + 1);
            }
        }
    }
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
}

} // namespace fast
