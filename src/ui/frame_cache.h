// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors
#pragma once

// frame_cache.h — one texture per frame, compiled only when that frame changed.
//
// This is the answer to "how does the canvas stay live", and it is worth saying
// why it is not a background thread.
//
// Measured on this machine, a compile is O(area) and barely notices layers:
// 32x32 costs 0.14 ms, 64x64 0.82 ms, 128x128 3.9 ms, 256x256 16.5 ms; going
// from 4 layers to 32 at 64x64 moves 0.69 ms to 1.22 ms. So for every sprite a
// person actually draws, a full compile fits inside a frame with room to spare
// and needs no help at all.
//
// A thread would not have helped anyway. LSContext is not thread-safe, so a
// background compile means either handing it the context for the duration --
// which blocks the edit that the compile exists to show -- or copying the
// document per compile, which costs more than the compile. And it would not
// have touched the problem that actually arrives with frames: a timeline shows
// eight frames at once, and eight compiles a frame is 31 ms at 128x128 whether
// they happen on one thread or four.
//
// What fixes that is not compiling them. Each frame keeps its texture, and the
// engine is asked which sprites actually changed -- it tracks that exactly, and
// editing one frame does not dirty its neighbours. So editing frame 3 costs one
// compile of frame 3, and the timeline, the onion skin, the corner preview and
// playback are all textured quads over textures that already exist.
//
// The number to watch is on the status bar: compiles this frame. Steady-state
// it is 0, and 1 while drawing. If it is ever N, something is asking for work
// it already has.

#include "app/document.h"

#include <SDL3/SDL.h>

#include <cstdint>
#include <map>
#include <vector>

namespace fast {

class FrameCache {
public:
    explicit FrameCache(SDL_Renderer* renderer) : renderer_(renderer) {}
    ~FrameCache();

    FrameCache(const FrameCache&) = delete;
    FrameCache& operator=(const FrameCache&) = delete;

    struct Entry {
        SDL_Texture*     texture = nullptr;
        uint32_t         width   = 0;
        uint32_t         height  = 0;
        ls::RasterBuffer raster;         // kept so reading a pixel needs no compile
    };

    // The frame's texture, compiled if and only if the engine says that sprite
    // has changed since it was last compiled. Null if it cannot be compiled at
    // all -- a sprite that no longer exists, or a canvas of no size.
    const Entry* entryFor(Document& doc, ls::SpriteId sprite);

    // Same, but never compiles: it returns what is already there, or null. The
    // timeline uses this for frames scrolled off the strip, so a long animation
    // does not compile every frame the moment it is opened.
    const Entry* cachedEntry(ls::SpriteId sprite) const;

    // One layer on its own, for the layer panel's thumbnails. Invalidated the
    // same way: the engine clears a layer's dirty bit when its compile is
    // cached, so a layer that did not change is not recompiled when its
    // neighbour is. A layer the engine never caches alone -- one holding an
    // outline that traces the whole sprite -- never goes clean, and for that
    // one the frame's own recompile is the trigger instead, so it costs one
    // extra compile per edit rather than one per UI frame.
    const Entry* entryForLayer(Document& doc, ls::SpriteId sprite, ls::LayerId layer);

    // Drops layer thumbnails for layers not in `live`, the way retainOnly
    // drops frames. The panel calls it with the frame it is showing.
    void retainOnlyLayers(const std::vector<ls::LayerId>& live);

    // Call once at the top of each UI frame.
    void beginFrame() { compilesThisFrame_ = 0; }
    int  compilesThisFrame() const { return compilesThisFrame_; }
    double lastCompileMs()   const { return lastCompileMs_; }
    size_t heldTextures()    const { return entries_.size(); }

    // Drops textures for frames that no longer exist. Called after anything
    // that can remove one -- a delete, an undo, opening another file -- so a
    // long session does not accumulate textures for sprites that are gone.
    void retainOnly(const std::vector<ls::SpriteId>& live);

    // Throws everything away. For a new document, where every id is different
    // and nothing cached means anything.
    void clear();

private:
    Entry* compile(Document& doc, ls::SpriteId sprite, Entry& into);
    Entry* upload(Entry& into, ls::RasterBuffer raster, uint32_t width, uint32_t height);

    struct LayerEntry {
        Entry    entry;
        bool     followsSprite = false;   // never goes clean; track the frame instead
        uint64_t spriteGeneration = 0;    // the frame's compile count it was made at
    };

    SDL_Renderer* renderer_ = nullptr;
    std::map<uint64_t, Entry> entries_;
    std::map<uint64_t, LayerEntry> layers_;
    std::map<uint64_t, uint64_t> generations_;   // per sprite: how many times compiled
    int    compilesThisFrame_ = 0;
    double lastCompileMs_ = 0.0;
};

} // namespace fast
