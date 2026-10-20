// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

#include "ui/thumbnails.h"

#include "app/library.h"
#include "ui/canvas_view.h"
#include "ui/reference_cache.h"
#include "ui/theme.h"

#include <algorithm>
#include <cstdint>
#include <vector>

namespace fast {

bool drawDocumentThumbnail(CanvasView& canvas, const std::string& path, float size, bool enabled,
                           int* budget) {
    // The picture each save leaves in the file, read without opening it.
    const ReferenceCache::Entry* picture = nullptr;
    if (enabled) {
        ReferenceCache& cache = canvas.libraryThumbnails();
        if (cache.holds(path)) {
            std::vector<uint8_t> none;
            picture = cache.entryForBytes(path, none);
        } else if (budget == nullptr || *budget > 0) {
            std::vector<uint8_t> png;
            if (readThumbnail(path, &png)) {
                picture = cache.entryForBytes(path, png, budget);
            } else if (budget != nullptr) {
                --*budget;
            }
        }
    }

    ImGui::BeginDisabled(!enabled);
    const bool clicked = ImGui::InvisibleButton("##thumb", ImVec2(size, size));
    ImGui::EndDisabled();
    const ImVec2 from = ImGui::GetItemRectMin();
    const ImVec2 to = ImGui::GetItemRectMax();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(from, to, ImGui::GetColorU32(theme::palette().canvasBackground), 3.f);
    if (picture != nullptr && picture->texture != nullptr && picture->width > 0 &&
        picture->height > 0) {
        const float pad = std::max(2.f, size * 0.04f);
        const float fit = std::min((size - 2.f * pad) / static_cast<float>(picture->width),
                                   (size - 2.f * pad) / static_cast<float>(picture->height));
        const float w = static_cast<float>(picture->width) * fit;
        const float h = static_cast<float>(picture->height) * fit;
        const ImVec2 at(from.x + (size - w) * 0.5f, from.y + (size - h) * 0.5f);
        draw->AddImage(reinterpret_cast<ImTextureID>(picture->texture), at, ImVec2(at.x + w, at.y + h));
    }
    draw->AddRect(from, to, ImGui::GetColorU32(theme::palette().border), 3.f);
    return clicked;
}

} // namespace fast
