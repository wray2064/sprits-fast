// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors
#pragma once

// canvas_view.h — the compiled sprite, on screen.
//
// The engine hands back an RGBA buffer; this turns it into a texture, draws it
// at an integer zoom with nearest-neighbour sampling, and converts mouse
// positions back into sprite pixels.
//
// The important thing it does is *not* compile every frame. A full compile at
// 256x256 with twenty layers costs about 37 ms, which inside a frame is a
// visibly stuttering canvas; a cached one costs microseconds. So the raster is
// recompiled only when the document says it has changed, and the texture is
// reuploaded only when the raster does.

#include "app/document.h"

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

    void  setZoom(float zoom);
    float zoom() const { return zoom_; }
    void  resetView() { zoom_ = 8.f; panX_ = 0.f; panY_ = 0.f; }

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

    bool   dirty_ = true;
    float  zoom_  = 8.f;
    float  panX_  = 0.f;
    float  panY_  = 0.f;
    double lastCompileMs_ = 0.0;
};

} // namespace fast
