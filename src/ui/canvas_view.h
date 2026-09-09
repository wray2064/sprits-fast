// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors
#pragma once

// canvas_view.h — the compiled sprite, on screen.
//
// The engine hands back an RGBA buffer; this turns it into a texture, draws it
// at a whole-number zoom with nearest-neighbour sampling, and converts mouse
// positions back into sprite pixels.
//
// The important thing it does is *not* compile every frame. A full compile at
// 256x256 with twenty layers costs about 37 ms, which inside a frame is a
// visibly stuttering canvas; a cached one costs microseconds. So the raster is
// recompiled only when the document says it has changed, and the texture is
// reuploaded only when the raster does.

#include "app/document.h"

#include <imgui.h>

#include <SDL3/SDL.h>

namespace fast {

class CanvasView {
public:
    explicit CanvasView(SDL_Renderer* renderer) : renderer_(renderer) {}
    ~CanvasView();

    CanvasView(const CanvasView&) = delete;
    CanvasView& operator=(const CanvasView&) = delete;

    // Called after anything that changes the picture. Cheap: it sets a flag.
    void invalidate() { dirty_ = true; }

    // Draws the canvas into the current ImGui window, handling zoom and pan.
    // Returns true while the pointer is over the artwork, with the pixel it is
    // over in `hovered`.
    bool draw(Document& doc, ls::SpriteId sprite, ls::Vec2i* hovered);

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

    // Draws the compiled sprite somewhere else at a whole-number scale, over a
    // background of the caller's choosing.
    //
    // Reuses the texture the canvas has already uploaded, so a preview costs a
    // textured quad and nothing else. Compiling a second time for it would be
    // the obvious implementation and the wrong one: the same picture would be
    // resolved twice a frame for no reason.
    //
    // `checker` draws the transparency chequer instead of a flat colour, which
    // is what "no background" has to mean for a sprite with holes in it.
    void drawSample(ImDrawList* draw, ImVec2 at, float scale,
                    ImU32 background, bool checker) const;

    // The size of the sprite as last compiled, so a caller can lay out a
    // preview before drawing it.
    uint32_t compiledWidth() const { return textureWidth_; }
    uint32_t compiledHeight() const { return textureHeight_; }

    // Milliseconds the last real compile took, for the status bar. An editor
    // should show this: it is the number that decides whether the canvas needs
    // to move to a background thread.
    double lastCompileMs() const { return lastCompileMs_; }

private:
    bool recompile(Document& doc, ls::SpriteId sprite);

    SDL_Renderer* renderer_ = nullptr;
    SDL_Texture*  texture_  = nullptr;
    uint32_t      textureWidth_  = 0;
    uint32_t      textureHeight_ = 0;

    // The last compile, kept so the eyedropper and anything else that wants to
    // read a pixel does not have to ask for another one.
    ls::RasterBuffer raster_;
    mutable ls::Color sampled_;

    bool   dirty_ = true;
    bool   grid_  = true;
    bool   fitPending_ = false;
    float  zoom_  = 8.f;
    float  panX_  = 0.f;
    float  panY_  = 0.f;
    double lastCompileMs_ = 0.0;
};

} // namespace fast
