// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

#include "ui/canvas_view.h"

#include <imgui.h>

#include <algorithm>
#include <chrono>
#include <cmath>

namespace fast {

CanvasView::~CanvasView() {
    if (texture_ != nullptr) {
        SDL_DestroyTexture(texture_);
    }
}

void CanvasView::setZoom(float zoom) {
    zoom_ = std::clamp(zoom, 1.f, 64.f);
}

bool CanvasView::recompile(Document& doc, ls::SpriteId sprite) {
    auto size = doc.engine().getCanvasSize(doc.id());
    if (size.fail() || size.value.x <= 0 || size.value.y <= 0) {
        return false;
    }
    const uint32_t width = static_cast<uint32_t>(size.value.x);
    const uint32_t height = static_cast<uint32_t>(size.value.y);

    ls::CompileProfile profile;
    profile.type = ls::CompileProfileType::Preview;   // Export is for files
    profile.outputWidth = width;
    profile.outputHeight = height;
    profile.palette = ls::PalettePolicy::Unconstrained;

    const auto started = std::chrono::steady_clock::now();
    auto compiled = doc.engine().compileSprite(sprite, profile);
    const auto finished = std::chrono::steady_clock::now();
    lastCompileMs_ =
        std::chrono::duration<double, std::milli>(finished - started).count();

    if (compiled.fail()) {
        return false;
    }

    if (texture_ == nullptr || textureWidth_ != width || textureHeight_ != height) {
        if (texture_ != nullptr) {
            SDL_DestroyTexture(texture_);
        }
        // RGBA32 is byte-order RGBA on every platform, which is exactly how the
        // engine lays out a raster, so the upload is a straight copy.
        texture_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA32,
                                     SDL_TEXTUREACCESS_STREAMING,
                                     static_cast<int>(width), static_cast<int>(height));
        if (texture_ == nullptr) {
            return false;
        }
        // Pixel art, so never interpolate when magnifying.
        SDL_SetTextureScaleMode(texture_, SDL_SCALEMODE_NEAREST);
        textureWidth_ = width;
        textureHeight_ = height;
    }

    const ls::RasterBuffer& raster = compiled.value.raster;
    if (!raster.pixels.empty()) {
        SDL_UpdateTexture(texture_, nullptr, raster.pixels.data(),
                          static_cast<int>(raster.stride));
    }
    dirty_ = false;
    return true;
}

bool CanvasView::draw(Document& doc, ls::SpriteId sprite, ls::Vec2i* hovered) {
    if (dirty_ && !recompile(doc, sprite)) {
        ImGui::TextUnformatted("nothing to compile");
        return false;
    }
    if (texture_ == nullptr) {
        return false;
    }

    ImGuiIO& io = ImGui::GetIO();

    // Ctrl+wheel zooms, plain wheel pans, middle-drag pans. Zoom is kept to
    // whole numbers because a pixel-art canvas at 3.7x looks wrong.
    if (ImGui::IsWindowHovered()) {
        if (io.KeyCtrl && io.MouseWheel != 0.f) {
            setZoom(zoom_ + (io.MouseWheel > 0.f ? 1.f : -1.f));
        } else if (io.MouseWheel != 0.f) {
            panY_ += io.MouseWheel * 32.f;
        }
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
            panX_ += io.MouseDelta.x;
            panY_ += io.MouseDelta.y;
        }
    }

    const float drawWidth = static_cast<float>(textureWidth_) * zoom_;
    const float drawHeight = static_cast<float>(textureHeight_) * zoom_;

    const ImVec2 available = ImGui::GetContentRegionAvail();
    const ImVec2 origin {
        ImGui::GetCursorScreenPos().x + std::max(0.f, (available.x - drawWidth) * 0.5f) + panX_,
        ImGui::GetCursorScreenPos().y + std::max(0.f, (available.y - drawHeight) * 0.5f) + panY_,
    };

    ImDrawList* draw = ImGui::GetWindowDrawList();

    // A checkerboard behind the artwork, so transparent reads as transparent
    // rather than as the window background.
    const float checker = 8.f;
    for (float y = 0.f; y < drawHeight; y += checker) {
        for (float x = 0.f; x < drawWidth; x += checker) {
            const bool odd = (static_cast<int>(x / checker) +
                              static_cast<int>(y / checker)) % 2 == 1;
            draw->AddRectFilled(
                {origin.x + x, origin.y + y},
                {origin.x + std::min(x + checker, drawWidth),
                 origin.y + std::min(y + checker, drawHeight)},
                odd ? IM_COL32(60, 60, 64, 255) : IM_COL32(48, 48, 52, 255));
        }
    }

    draw->AddImage(reinterpret_cast<ImTextureID>(texture_), origin,
                   {origin.x + drawWidth, origin.y + drawHeight});
    draw->AddRect(origin, {origin.x + drawWidth, origin.y + drawHeight},
                  IM_COL32(90, 90, 96, 255));

    // A grid, once the zoom is large enough for it to help rather than blur.
    if (zoom_ >= 8.f) {
        for (uint32_t x = 1; x < textureWidth_; ++x) {
            const float at = origin.x + static_cast<float>(x) * zoom_;
            draw->AddLine({at, origin.y}, {at, origin.y + drawHeight},
                          IM_COL32(255, 255, 255, 18));
        }
        for (uint32_t y = 1; y < textureHeight_; ++y) {
            const float at = origin.y + static_cast<float>(y) * zoom_;
            draw->AddLine({origin.x, at}, {origin.x + drawWidth, at},
                          IM_COL32(255, 255, 255, 18));
        }
    }

    ImGui::Dummy(available);

    // Screen back to sprite pixels.
    const ImVec2 mouse = io.MousePos;
    const float localX = (mouse.x - origin.x) / zoom_;
    const float localY = (mouse.y - origin.y) / zoom_;
    const bool inside = localX >= 0.f && localY >= 0.f &&
                        localX < static_cast<float>(textureWidth_) &&
                        localY < static_cast<float>(textureHeight_);

    if (inside && hovered != nullptr) {
        hovered->x = static_cast<int32_t>(std::floor(localX));
        hovered->y = static_cast<int32_t>(std::floor(localY));

        // Outline the pixel under the pointer, so the tool's target is never a
        // guess.
        const ImVec2 top { origin.x + static_cast<float>(hovered->x) * zoom_,
                           origin.y + static_cast<float>(hovered->y) * zoom_ };
        draw->AddRect(top, {top.x + zoom_, top.y + zoom_}, IM_COL32(255, 255, 255, 160));
    }

    return inside && ImGui::IsWindowHovered();
}

} // namespace fast
