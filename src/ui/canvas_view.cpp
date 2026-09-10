// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

#include "ui/canvas_view.h"
#include "ui/theme.h"

#include <imgui.h>

#include <algorithm>
#include <chrono>
#include <cmath>

namespace fast {
namespace {

// The checkerboard square, in screen pixels. Fixed rather than scaled with the
// zoom, so it stays a background texture instead of becoming a second pattern
// competing with whatever is being drawn.
constexpr float kCheckerSize = 8.f;

} // namespace

void CanvasView::setZoom(float zoom) {
    zoom_ = std::clamp(std::floor(zoom + 0.5f), 1.f, 64.f);
}

const ls::Color* CanvasView::colorAt(ls::Vec2i pixel) const {
    if (raster_ == nullptr || raster_->empty() || pixel.x < 0 || pixel.y < 0 ||
        pixel.x >= static_cast<int32_t>(raster_->width) ||
        pixel.y >= static_cast<int32_t>(raster_->height)) {
        return nullptr;
    }
    sampled_ = ls::readPixel(*raster_, pixel.x, pixel.y);
    return &sampled_;
}

void CanvasView::drawSample(ImDrawList* draw, ImVec2 at, float scale,
                            ImU32 background, bool checker) const {
    if (texture_ == nullptr || textureWidth_ == 0 || textureHeight_ == 0) {
        return;
    }
    const float width = static_cast<float>(textureWidth_) * scale;
    const float height = static_cast<float>(textureHeight_) * scale;
    const ImVec2 corner(at.x + width, at.y + height);

    if (checker) {
        const theme::Palette& c = theme::palette();
        draw->AddRectFilled(at, corner, ImGui::GetColorU32(c.checkerDark));
        draw->PushClipRect(at, corner, true);
        for (float y = 0.f; y < height; y += kCheckerSize) {
            for (float x = 0.f; x < width; x += kCheckerSize) {
                if ((static_cast<int>(x / kCheckerSize) +
                     static_cast<int>(y / kCheckerSize)) % 2 == 0) {
                    continue;
                }
                draw->AddRectFilled(
                    ImVec2(at.x + x, at.y + y),
                    ImVec2(at.x + std::min(x + kCheckerSize, width),
                           at.y + std::min(y + kCheckerSize, height)),
                    ImGui::GetColorU32(c.checkerLight));
            }
        }
        draw->PopClipRect();
    } else {
        draw->AddRectFilled(at, corner, background);
    }

    draw->AddImage(reinterpret_cast<ImTextureID>(texture_), at, corner);
}

bool CanvasView::draw(Document& doc, ls::SpriteId sprite, ls::Vec2i* hovered,
                      const Underlay& underlay) {
    // One lookup, and a compile only if the engine says this frame changed.
    const FrameCache::Entry* entry = frames_.entryFor(doc, sprite);
    if (entry == nullptr || entry->texture == nullptr) {
        ImGui::TextUnformatted("nothing to compile");
        texture_ = nullptr;
        raster_ = nullptr;
        return false;
    }
    texture_ = entry->texture;
    textureWidth_ = entry->width;
    textureHeight_ = entry->height;
    raster_ = &entry->raster;

    ImGuiIO& io = ImGui::GetIO();
    const ImVec2 available = ImGui::GetContentRegionAvail();
    const ImVec2 topLeft = ImGui::GetCursorScreenPos();

    // Fitting needs the area, which is only known here.
    if (fitPending_ && textureWidth_ > 0 && textureHeight_ > 0) {
        // Enough that the artwork sits *in* the panel rather than against its
        // edges. A sprite touching the frame is hard to judge, which is the one
        // thing the canvas exists for.
        const float margin = 72.f;
        const float byWidth = (available.x - margin) / static_cast<float>(textureWidth_);
        const float byHeight = (available.y - margin) / static_cast<float>(textureHeight_);
        setZoom(std::min(byWidth, byHeight));
        panX_ = 0.f;
        panY_ = 0.f;
        fitPending_ = false;
    }

    const bool windowHovered = ImGui::IsWindowHovered();

    // Where the artwork sits: centred in whatever room there is, then panned.
    const auto artworkOrigin = [&](float zoom) {
        const float drawWidth = static_cast<float>(textureWidth_) * zoom;
        const float drawHeight = static_cast<float>(textureHeight_) * zoom;
        return ImVec2(topLeft.x + (available.x - drawWidth) * 0.5f + panX_,
                      topLeft.y + (available.y - drawHeight) * 0.5f + panY_);
    };

    if (windowHovered) {
        if (io.MouseWheel != 0.f) {
            // Zoom toward the cursor: the pixel under the pointer stays under
            // the pointer. Zooming to the centre instead means hunting for
            // what you were looking at after every step.
            const ImVec2 before = artworkOrigin(zoom_);
            const float localX = (io.MousePos.x - before.x) / zoom_;
            const float localY = (io.MousePos.y - before.y) / zoom_;

            const float previous = zoom_;
            setZoom(zoom_ + (io.MouseWheel > 0.f ? 1.f : -1.f));

            if (zoom_ != previous) {
                const ImVec2 after = artworkOrigin(zoom_);
                panX_ += (io.MousePos.x - after.x) - localX * zoom_;
                panY_ += (io.MousePos.y - after.y) - localY * zoom_;
            }
        }
        // Middle-drag pans, and so does space-drag, which is the habit most
        // people bring with them.
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle) ||
            (ImGui::IsKeyDown(ImGuiKey_Space) &&
             ImGui::IsMouseDragging(ImGuiMouseButton_Left))) {
            panX_ += io.MouseDelta.x;
            panY_ += io.MouseDelta.y;
        }
    }

    const float drawWidth = static_cast<float>(textureWidth_) * zoom_;
    const float drawHeight = static_cast<float>(textureHeight_) * zoom_;
    const ImVec2 origin = artworkOrigin(zoom_);
    const ImVec2 corner(origin.x + drawWidth, origin.y + drawHeight);

    ImDrawList* draw = ImGui::GetWindowDrawList();
    const theme::Palette& c = theme::palette();

    // A soft shadow under the artwork, so it reads as a sheet on a surface
    // rather than a hole in the panel.
    draw->AddRectFilled(ImVec2(origin.x + 3.f, origin.y + 4.f),
                        ImVec2(corner.x + 3.f, corner.y + 4.f),
                        IM_COL32(0, 0, 0, 70), 2.f);

    // The transparency chequer, clipped to the artwork.
    draw->PushClipRect(origin, corner, true);
    draw->AddRectFilled(origin, corner, ImGui::GetColorU32(c.checkerDark));
    for (float y = 0.f; y < drawHeight; y += kCheckerSize) {
        for (float x = 0.f; x < drawWidth; x += kCheckerSize) {
            const bool odd = (static_cast<int>(x / kCheckerSize) +
                              static_cast<int>(y / kCheckerSize)) % 2 == 1;
            if (!odd) {
                continue;
            }
            draw->AddRectFilled(
                ImVec2(origin.x + x, origin.y + y),
                ImVec2(origin.x + std::min(x + kCheckerSize, drawWidth),
                       origin.y + std::min(y + kCheckerSize, drawHeight)),
                ImGui::GetColorU32(c.checkerLight));
        }
    }
    draw->PopClipRect();

    if (underlay) {
        underlay(draw, origin, zoom_);
    }

    draw->AddImage(reinterpret_cast<ImTextureID>(texture_), origin, corner);

    // The pixel grid, once the zoom is large enough for it to help rather than
    // turn the artwork into a mesh.
    if (grid_ && zoom_ >= 8.f) {
        const ImU32 line = IM_COL32(255, 255, 255, 16);
        for (uint32_t x = 1; x < textureWidth_; ++x) {
            const float at = origin.x + static_cast<float>(x) * zoom_;
            draw->AddLine(ImVec2(at, origin.y), ImVec2(at, corner.y), line);
        }
        for (uint32_t y = 1; y < textureHeight_; ++y) {
            const float at = origin.y + static_cast<float>(y) * zoom_;
            draw->AddLine(ImVec2(origin.x, at), ImVec2(corner.x, at), line);
        }
    }

    draw->AddRect(origin, corner, ImGui::GetColorU32(c.border), 0.f, 0, 1.f);

    ImGui::Dummy(available);

    // Screen back to sprite pixels.
    const float localX = (io.MousePos.x - origin.x) / zoom_;
    const float localY = (io.MousePos.y - origin.y) / zoom_;
    const bool inside = localX >= 0.f && localY >= 0.f &&
                        localX < static_cast<float>(textureWidth_) &&
                        localY < static_cast<float>(textureHeight_);

    if (inside && hovered != nullptr) {
        hovered->x = static_cast<int32_t>(std::floor(localX));
        hovered->y = static_cast<int32_t>(std::floor(localY));

        // Outline the pixel under the pointer, so the tool's target is never a
        // guess. Drawn in two passes -- dark then light -- so it stays visible
        // over both a dark and a light sprite.
        const ImVec2 top(origin.x + static_cast<float>(hovered->x) * zoom_,
                         origin.y + static_cast<float>(hovered->y) * zoom_);
        const ImVec2 bottom(top.x + zoom_, top.y + zoom_);
        draw->AddRect(ImVec2(top.x - 1.f, top.y - 1.f),
                      ImVec2(bottom.x + 1.f, bottom.y + 1.f),
                      IM_COL32(0, 0, 0, 140), 0.f, 0, 1.f);
        draw->AddRect(top, bottom, IM_COL32(255, 255, 255, 220), 0.f, 0, 1.f);
    } else if (hovered != nullptr) {
        *hovered = { -1, -1 };
    }

    return inside && windowHovered;
}

void CanvasView::drawFrameTinted(ImDrawList* draw, const FrameCache::Entry& entry,
                                 ImVec2 at, float scale, ImU32 tint) const {
    if (entry.texture == nullptr || entry.width == 0 || entry.height == 0) {
        return;
    }
    const ImVec2 corner(at.x + static_cast<float>(entry.width) * scale,
                        at.y + static_cast<float>(entry.height) * scale);
    draw->AddImage(reinterpret_cast<ImTextureID>(entry.texture), at, corner,
                   ImVec2(0.f, 0.f), ImVec2(1.f, 1.f), tint);
}

} // namespace fast
