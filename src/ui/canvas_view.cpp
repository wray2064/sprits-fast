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

// Sprite pixels are drawn with nearest sampling, and everything else is not.
//
// Setting the texture's own scale mode is not enough: from 1.92.8 the
// SDL_Renderer backend owns the sampler and re-applies its current choice to
// every texture it binds, linear unless told otherwise, so a canvas at 18x
// came out as a smear. The backend's own callbacks are the way to tell it,
// bracketing exactly the commands that carry artwork.
void beginPixels(ImDrawList* draw) {
    draw->AddCallback(ImGui::GetPlatformIO().DrawCallback_SetSamplerNearest, nullptr);
}

void endPixels(ImDrawList* draw) {
    draw->AddCallback(ImGui::GetPlatformIO().DrawCallback_SetSamplerLinear, nullptr);
}

} // namespace

ls::Vec2i CanvasView::wrap(ls::Vec2i pixel) const {
    const auto modulo = [](int32_t value, uint32_t size) {
        if (size == 0) { return value; }
        const int32_t s = static_cast<int32_t>(size);
        return ((value % s) + s) % s;
    };
    return { tilesAcross() ? modulo(pixel.x, textureWidth_) : pixel.x,
             tilesDown() ? modulo(pixel.y, textureHeight_) : pixel.y };
}

void CanvasView::setZoom(float zoom) {
    fitted_ = false;
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
        draw->AddRectFilled(at, corner, checkerDark_);
        draw->PushClipRect(at, corner, true);
        for (float y = 0.f; y < height; y += checkerSize_) {
            for (float x = 0.f; x < width; x += checkerSize_) {
                if ((static_cast<int>(x / checkerSize_) +
                     static_cast<int>(y / checkerSize_)) % 2 == 0) {
                    continue;
                }
                draw->AddRectFilled(
                    ImVec2(at.x + x, at.y + y),
                    ImVec2(at.x + std::min(x + checkerSize_, width),
                           at.y + std::min(y + checkerSize_, height)),
                    checkerLight_);
            }
        }
        draw->PopClipRect();
    } else {
        draw->AddRectFilled(at, corner, background);
    }

    beginPixels(draw);
    draw->AddImage(reinterpret_cast<ImTextureID>(texture_), at, corner);
    endPixels(draw);
}

bool CanvasView::draw(Document& doc, ls::SpriteId sprite, ls::Vec2i* hovered,
                      const Underlay& underlay, const Underlay& overlay) {
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
    viewTopLeft_ = topLeft;
    viewSize_ = available;

    // Fitting needs the area, which is only known here.
    if (fitted_ && (available.x != fittedArea_.x || available.y != fittedArea_.y)) {
        fitPending_ = true;
    }
    if (fitPending_ && textureWidth_ > 0 && textureHeight_ > 0) {
        // Enough that the artwork sits *in* the panel rather than against its
        // edges. A sprite touching the frame is hard to judge, which is the one
        // thing the canvas exists for.
        const float margin = 72.f;
        const float tw = static_cast<float>(textureWidth_);
        const float th = static_cast<float>(textureHeight_);
        const auto fitIn = [&](float w, float h) {
            return std::clamp(std::floor(std::min((w - margin) / tw, (h - margin) / th) + 0.5f),
                              1.f, 64.f);
        };
        // Whether the artwork, centred at `z` and moved by the pan, reaches
        // under the corner overlay.
        const auto covered = [&](float z, float px, float py) {
            if (cornerOverlay_.x <= 0.f || cornerOverlay_.y <= 0.f) {
                return false;
            }
            const float right = (available.x + tw * z) * 0.5f + px;
            const float bottom = (available.y + th * z) * 0.5f + py;
            return right > available.x - cornerOverlay_.x &&
                   bottom > available.y - cornerOverlay_.y;
        };
        float zoom = fitIn(available.x, available.y);
        float px = 0.f;
        float py = 0.f;
        if (covered(zoom, px, py)) {
            // Beside the overlay or above it, whichever leaves the artwork
            // bigger; then smaller still if rounding put it back under.
            const float beside = fitIn(available.x - cornerOverlay_.x, available.y);
            const float above = fitIn(available.x, available.y - cornerOverlay_.y);
            if (beside >= above) {
                zoom = beside;
                px = -cornerOverlay_.x * 0.5f;
            } else {
                zoom = above;
                py = -cornerOverlay_.y * 0.5f;
            }
            while (zoom > 1.f && covered(zoom, px, py)) {
                zoom -= 1.f;
            }
        }
        setZoom(zoom);
        panX_ = px;
        panY_ = py;
        fitPending_ = false;
        fitted_ = true;
        fittedArea_ = available;
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
        float step = io.MouseWheel;
        if (zoomOnClick_) {
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                step = io.KeyAlt ? -1.f : 1.f;
            } else if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
                step = -1.f;
            }
        }
        if (step != 0.f) {
            // Zoom toward the cursor: the pixel under the pointer stays under
            // the pointer. Zooming to the centre instead means hunting for
            // what you were looking at after every step.
            const ImVec2 before = artworkOrigin(zoom_);
            const float localX = (io.MousePos.x - before.x) / zoom_;
            const float localY = (io.MousePos.y - before.y) / zoom_;

            const float previous = zoom_;
            setZoom(zoom_ + (step > 0.f ? 1.f : -1.f));

            if (zoom_ != previous) {
                const ImVec2 after = artworkOrigin(zoom_);
                fitted_ = false;
                panX_ += (io.MousePos.x - after.x) - localX * zoom_;
                panY_ += (io.MousePos.y - after.y) - localY * zoom_;
            }
        }
        // Middle-drag pans, and so does space-drag, which is the habit most
        // people bring with them.
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle) ||
            ((ImGui::IsKeyDown(ImGuiKey_Space) || panWithPrimary_) &&
             ImGui::IsMouseDragging(ImGuiMouseButton_Left))) {
            fitted_ = false;
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
    draw->AddRectFilled(origin, corner, checkerDark_);
    for (float y = 0.f; y < drawHeight; y += checkerSize_) {
        for (float x = 0.f; x < drawWidth; x += checkerSize_) {
            const bool odd = (static_cast<int>(x / checkerSize_) +
                              static_cast<int>(y / checkerSize_)) % 2 == 1;
            if (!odd) {
                continue;
            }
            draw->AddRectFilled(
                ImVec2(origin.x + x, origin.y + y),
                ImVec2(origin.x + std::min(x + checkerSize_, drawWidth),
                       origin.y + std::min(y + checkerSize_, drawHeight)),
                checkerLight_);
        }
    }
    draw->PopClipRect();

    if (underlay) {
        underlay(draw, origin, zoom_);
    }

    beginPixels(draw);
    draw->AddImage(reinterpret_cast<ImTextureID>(texture_), origin, corner);
    endPixels(draw);

    // Tiled mode: the same texture again on every side it tiles, faintly
    // darkened so the real canvas is still the one that reads as the canvas.
    // No compile -- the picture is the one already uploaded.
    if (tiled_ != TiledMode::None) {
        const int across = tilesAcross() ? 1 : 0;
        const int down = tilesDown() ? 1 : 0;
        for (int ty = -down; ty <= down; ++ty) {
            for (int tx = -across; tx <= across; ++tx) {
                if (tx == 0 && ty == 0) {
                    continue;
                }
                const ImVec2 at(origin.x + static_cast<float>(tx) * drawWidth,
                                origin.y + static_cast<float>(ty) * drawHeight);
                const ImVec2 to(at.x + drawWidth, at.y + drawHeight);
                draw->AddRectFilled(at, to, checkerDark_);
                beginPixels(draw);
                draw->AddImage(reinterpret_cast<ImTextureID>(texture_), at, to);
                endPixels(draw);
                draw->AddRectFilled(at, to, IM_COL32(0, 0, 0, 60));
            }
        }
    }

    if (overlay) {
        overlay(draw, origin, zoom_);
    }

    // The pixel grid, once the zoom is large enough for it to help rather than
    // turn the artwork into a mesh.
    if (grid_ && zoom_ >= 8.f) {
        const ImU32 line = gridColour_;
        for (uint32_t x = 1; x < textureWidth_; ++x) {
            const float at = origin.x + static_cast<float>(x) * zoom_;
            draw->AddLine(ImVec2(at, origin.y), ImVec2(at, corner.y), line);
        }
        for (uint32_t y = 1; y < textureHeight_; ++y) {
            const float at = origin.y + static_cast<float>(y) * zoom_;
            draw->AddLine(ImVec2(origin.x, at), ImVec2(corner.x, at), line);
        }
    }

    // The tile grid: stronger than the pixel grid, and shown at any zoom,
    // since cells are what it is for and they are large.
    if (tiles_.visible && tiles_.width > 0 && tiles_.height > 0) {
        const ImU32 line = IM_COL32(120, 190, 255, 90);
        draw->PushClipRect(origin, corner, true);
        const int ox = ((tiles_.offsetX % tiles_.width) + tiles_.width) % tiles_.width;
        const int oy = ((tiles_.offsetY % tiles_.height) + tiles_.height) % tiles_.height;
        if (tiles_.isometric) {
            // Two families of diagonals, u = k and v = k, where
            // u = (x - ox) / W + (y - oy) / H and v = (x - ox) / W - (y - oy) / H.
            // Each is drawn top to bottom across the canvas; the clip trims it.
            const float W = static_cast<float>(tiles_.width);
            const float H = static_cast<float>(tiles_.height);
            const float fx = static_cast<float>(ox);
            const float fy = static_cast<float>(oy);
            const float w = static_cast<float>(textureWidth_);
            const float h = static_cast<float>(textureHeight_);
            const auto toScreen = [&](float x, float y) {
                return ImVec2(origin.x + x * zoom_, origin.y + y * zoom_);
            };
            const int first = static_cast<int>(std::floor(-fx / W - fy / H - (h / H))) - 1;
            const int last = static_cast<int>(std::ceil((w - fx) / W + (h - fy) / H + h / H)) + 1;
            for (int k = first; k <= last; ++k) {
                const float kk = static_cast<float>(k);
                // u = k: x = ox + W * (k - (y - oy) / H)
                draw->AddLine(toScreen(fx + W * (kk + fy / H), 0.f),
                              toScreen(fx + W * (kk - (h - fy) / H), h), line);
                // v = k: x = ox + W * (k + (y - oy) / H)
                draw->AddLine(toScreen(fx + W * (kk - fy / H), 0.f),
                              toScreen(fx + W * (kk + (h - fy) / H), h), line);
            }
        } else {
            for (int x = ox; x <= static_cast<int>(textureWidth_); x += tiles_.width) {
                const float at = origin.x + static_cast<float>(x) * zoom_;
                draw->AddLine(ImVec2(at, origin.y), ImVec2(at, corner.y), line);
            }
            for (int y = oy; y <= static_cast<int>(textureHeight_); y += tiles_.height) {
                const float at = origin.y + static_cast<float>(y) * zoom_;
                draw->AddLine(ImVec2(origin.x, at), ImVec2(corner.x, at), line);
            }
        }
        draw->PopClipRect();
    }

    draw->AddRect(origin, corner, ImGui::GetColorU32(c.border), 0.f, 0, 1.f);

    ImGui::Dummy(available);

    // Screen back to sprite pixels.
    const float localX = (io.MousePos.x - origin.x) / zoom_;
    const float localY = (io.MousePos.y - origin.y) / zoom_;
    pointer_ = { static_cast<int32_t>(std::floor(localX)),
                 static_cast<int32_t>(std::floor(localY)) };
    pointerExact_ = { localX, localY };
    const float w = static_cast<float>(textureWidth_);
    const float h = static_cast<float>(textureHeight_);
    const float spanX = tilesAcross() ? w : 0.f;
    const float spanY = tilesDown() ? h : 0.f;
    const bool inside = localX >= -spanX && localY >= -spanY &&
                        localX < w + spanX && localY < h + spanY;

    if (inside && hovered != nullptr) {
        const ls::Vec2i wrapped = wrap(pointer_);
        hovered->x = wrapped.x;
        hovered->y = wrapped.y;

        // Outline what the tool would cover, so its target is never a guess:
        // the pixel under the pointer, or the brush around it, placed the way
        // brushStamp places it. Drawn in two passes -- dark then light -- so
        // it stays visible over both a dark and a light sprite.
        // Drawn where the pointer is, not where it wraps to: the outline
        // follows the hand, and the wrap is what the stroke does with it.
        const int before = (hoverSize_ - 1) / 2;
        const ImVec2 top(origin.x + static_cast<float>(pointer_.x - before) * zoom_,
                         origin.y + static_cast<float>(pointer_.y - before) * zoom_);
        const ImVec2 bottom(top.x + zoom_ * static_cast<float>(hoverSize_),
                            top.y + zoom_ * static_cast<float>(hoverSize_));
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
    beginPixels(draw);
    draw->AddImage(reinterpret_cast<ImTextureID>(entry.texture), at, corner,
                   ImVec2(0.f, 0.f), ImVec2(1.f, 1.f), tint);
    endPixels(draw);
}

} // namespace fast
