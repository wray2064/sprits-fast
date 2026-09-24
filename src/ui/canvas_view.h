// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors
#pragma once

// canvas_view.h — the compiled sprite, on screen.
//
// Zoom, pan, hit testing, and drawing the active frame at a whole-number scale
// with nearest-neighbour sampling.
//
// It does not own a texture. Every frame's texture lives in the FrameCache,
// which compiles a frame only when the engine says that frame changed -- see
// frame_cache.h for why that is the whole performance story and a background
// thread is not.

#include "app/document.h"
#include "ui/frame_cache.h"
#include "ui/reference_cache.h"

#include <imgui.h>

#include <SDL3/SDL.h>

#include <functional>

namespace fast {

class CanvasView {
public:
    explicit CanvasView(SDL_Renderer* renderer)
        : renderer_(renderer), frames_(renderer), references_(renderer),
          libraryThumbs_(renderer) {}

    CanvasView(const CanvasView&) = delete;
    CanvasView& operator=(const CanvasView&) = delete;

    // Kept because every tool calls it, but it no longer decides anything: the
    // cache asks the engine which frames changed, which is exact where a flag
    // set by hand is only as good as the last person to remember it.
    void invalidate() {}

    // Every frame's texture. The timeline, the onion skin and the corner
    // preview all draw from here rather than compiling anything of their own.
    FrameCache&       frames()       { return frames_; }
    const FrameCache& frames() const { return frames_; }
    ReferenceCache&   referenceTextures() { return references_; }
    ReferenceCache&   libraryThumbnails() { return libraryThumbs_; }

    // Where a canvas pixel lands on screen, and the reverse, for anything
    // drawing or picking in canvas coordinates outside this class -- the
    // reference overlay, which has to place an image in canvas pixels.
    ImVec2 pixelToScreen(ImVec2 origin, float x, float y) const {
        return ImVec2(origin.x + x * zoom_, origin.y + y * zoom_);
    }

    // Anything drawn beneath the artwork: the onion skin is the only user.
    //
    // It is a hook rather than a second call after draw() because "beneath" is
    // the whole point -- ghosts painted over the live frame haze the thing the
    // artist is judging. Only draw() knows where the artwork landed, so this is
    // called from inside it, between the chequer and the image.
    using Underlay = std::function<void(ImDrawList*, ImVec2 origin, float zoom)>;

    // Draws the canvas into the current ImGui window, handling zoom and pan.
    // Returns true while the pointer is over the artwork, with the pixel it is
    // over in `hovered`.
    // `underlay` draws between the chequer and the artwork, `overlay` between
    // the artwork and the pixel grid. References use both: one sits under the
    // drawing to trace from, one over it to compare against.
    bool draw(Document& doc, ls::SpriteId sprite, ls::Vec2i* hovered,
              const Underlay& underlay = nullptr,
              const Underlay& overlay = nullptr);

    // How many pixels across the hover outline is: the brush, so what a click
    // would cover is what is outlined. 1 is the pixel under the pointer.
    void setHoverSize(int size) { hoverSize_ = size < 1 ? 1 : size; }

    // Zoom is kept to whole numbers: a pixel-art canvas at 3.7x looks wrong,
    // and a pixel that is sometimes three and sometimes four screen pixels wide
    // is worse than one that is simply small.
    void  setZoom(float zoom);
    float zoom() const { return zoom_; }

    void  setPan(float x, float y) { panX_ = x; panY_ = y; }
    float panX() const { return panX_; }
    float panY() const { return panY_; }

    void resetView() { zoom_ = 8.f; panX_ = 0.f; panY_ = 0.f; }

    // Fit on the next draw, when the window size is known. Fitting needs the
    // available area, which is not known until the panel is being laid out.
    void requestFit() { fitPending_ = true; }

    void setGridVisible(bool visible) { grid_ = visible; }
    bool gridVisible() const { return grid_; }

    // The colour of a pixel in the last compile, for the eyedropper. Null when
    // the point is outside the canvas or nothing has been compiled yet. Reading
    // the cached raster rather than recompiling means picking is free.
    const ls::Color* colorAt(ls::Vec2i pixel) const;

    // Draws the active frame somewhere else at a whole-number scale, over a
    // background of the caller's choosing.
    //
    // Reuses the texture the cache already holds, so a preview costs a textured
    // quad and nothing else. Compiling a second time for it would be the
    // obvious implementation and the wrong one: the same picture would be
    // resolved twice a frame for no reason.
    //
    // `checker` draws the transparency chequer instead of a flat colour, which
    // is what "no background" has to mean for a sprite with holes in it.
    void drawSample(ImDrawList* draw, ImVec2 at, float scale,
                    ImU32 background, bool checker) const;

    // The same, for any frame and with a tint -- the onion skin draws the
    // frames either side of this one through it. Takes the texture as it is:
    // a frame with nothing cached draws nothing rather than compiling.
    void drawFrameTinted(ImDrawList* draw, const FrameCache::Entry& entry,
                         ImVec2 at, float scale, ImU32 tint) const;

    // The size of the sprite as last compiled, so a caller can lay out a
    // preview before drawing it.
    uint32_t compiledWidth() const { return textureWidth_; }
    uint32_t compiledHeight() const { return textureHeight_; }

    // Milliseconds the last real compile took, and how many happened this UI
    // frame. Both are on the status bar, because "compiles this frame" is the
    // number that says whether the cache is doing its job: 0 at rest, 1 while
    // drawing, and never the number of frames on screen.
    double lastCompileMs() const { return frames_.lastCompileMs(); }
    int    compilesThisFrame() const { return frames_.compilesThisFrame(); }

private:
    SDL_Renderer* renderer_ = nullptr;
    FrameCache      frames_;
    ReferenceCache  references_;
    ReferenceCache  libraryThumbs_;
    int           hoverSize_ = 1;

    // The active frame, as of the last draw. Held so drawSample and the
    // eyedropper can read it without asking for anything.
    SDL_Texture*  texture_  = nullptr;
    uint32_t      textureWidth_  = 0;
    uint32_t      textureHeight_ = 0;
    const ls::RasterBuffer* raster_ = nullptr;
    mutable ls::Color sampled_;

    bool   grid_  = true;
    bool   fitPending_ = false;
    float  zoom_  = 8.f;
    float  panX_  = 0.f;
    float  panY_  = 0.f;
};

} // namespace fast
