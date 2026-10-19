// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

#include "ui/new_document.h"

#include "app/document.h"
#include "app/i18n.h"
#include "app/palette_tools.h"
#include "ui/theme.h"
#include "ui/widgets.h"

#include <imgui.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

namespace fast {

bool drawNewDocumentFields(FileState::NewDocument& spec) {
    // The name: what the tab and the window say until the file has one of its
    // own, and what Save As offers.
    char name[64];
    std::snprintf(name, sizeof name, "%s", spec.name.c_str());
    widgets::field("name");
    if (ImGui::InputText("##newname", name, sizeof name)) {
        spec.name = name;
    }

    int width = static_cast<int>(spec.width);
    int height = static_cast<int>(spec.height);
    widgets::field("width");
    ImGui::InputInt("##newwidth", &width);
    widgets::field("height");
    ImGui::InputInt("##newheight", &height);

    // The sizes pixel art is usually made at, and portrait to landscape as
    // Photoshop's orientation does.
    widgets::field("sizes");
    // They wrap, under the first, when the form is narrower than the seven: in the
    // start screen's column, or at a larger interface size.
    const float sizesLeft = ImGui::GetCursorPosX();
    bool firstSize = true;
    for (int size : { 16, 24, 32, 48, 64, 128, 256 }) {
        const std::string label = std::to_string(size);
        if (!firstSize &&
            !widgets::sameLineIfFits(ImGui::CalcTextSize(label.c_str()).x +
                                     ImGui::GetStyle().FramePadding.x * 2.f)) {
            ImGui::SetCursorPosX(sizesLeft);
        }
        firstSize = false;
        if (ImGui::SmallButton(label.c_str())) {
            width = height = size;
        }
    }
    widgets::field("");
    if (ImGui::SmallButton(tr("Swap width and height"))) {
        std::swap(width, height);
    }
    spec.width = static_cast<uint32_t>(std::clamp(width, 1, static_cast<int>(kMaxCanvasDimension)));
    spec.height = static_cast<uint32_t>(std::clamp(height, 1, static_cast<int>(kMaxCanvasDimension)));
    const bool fits = static_cast<uint64_t>(spec.width) * spec.height <= kMaxCanvasPixels;

    ImGui::Dummy(ImVec2(0.f, 4.f));
    widgets::field("background");
    const char* backgrounds[] = { "Transparent", "White", "Black", "The palette's first colour" };
    ImGui::Combo("##newbackground", &spec.background, backgrounds, 4);

    widgets::field("palette");
    const std::vector<PalettePreset>& presets = palettePresets();
    const char* current = spec.preset < 0 || spec.preset >= static_cast<int>(presets.size())
                              ? "Starter"
                              : presets[static_cast<size_t>(spec.preset)].name.c_str();
    if (ImGui::BeginCombo("##newpalette", current)) {
        if (ImGui::Selectable(tr("Starter"), spec.preset < 0)) {
            spec.preset = -1;
        }
        for (size_t i = 0; i < presets.size(); ++i) {
            if (ImGui::Selectable(presets[i].name.c_str(), spec.preset == static_cast<int>(i))) {
                spec.preset = static_cast<int>(i);
            }
        }
        ImGui::EndCombo();
    }
    if (!fits) {
        ImGui::Dummy(ImVec2(0.f, 4.f));
        widgets::banner(widgets::Tone::Danger, "Larger than a canvas Fast works on.");
    }
    return fits;
}

} // namespace fast
