// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

#include "ui/frame_cache.h"

#include <algorithm>
#include <chrono>

namespace fast {

FrameCache::~FrameCache() {
    clear();
}

void FrameCache::clear() {
    for (auto& [id, entry] : entries_) {
        if (entry.texture != nullptr) {
            SDL_DestroyTexture(entry.texture);
        }
    }
    entries_.clear();
}

void FrameCache::retainOnly(const std::vector<ls::SpriteId>& live) {
    for (auto it = entries_.begin(); it != entries_.end(); ) {
        const bool keep = std::any_of(live.begin(), live.end(),
            [&](ls::SpriteId sprite) { return sprite.value == it->first; });
        if (keep) {
            ++it;
            continue;
        }
        if (it->second.texture != nullptr) {
            SDL_DestroyTexture(it->second.texture);
        }
        it = entries_.erase(it);
    }
}

const FrameCache::Entry* FrameCache::cachedEntry(ls::SpriteId sprite) const {
    auto found = entries_.find(sprite.value);
    if (found == entries_.end() || found->second.texture == nullptr) {
        return nullptr;
    }
    return &found->second;
}

const FrameCache::Entry* FrameCache::entryFor(Document& doc, ls::SpriteId sprite) {
    if (!sprite.valid()) {
        return nullptr;
    }
    Entry& entry = entries_[sprite.value];

    // The engine is asked rather than told. It tracks this exactly -- a sprite
    // is dirty when something it draws with has changed, an undo dirties
    // everything, and editing one frame leaves its neighbours clean -- so
    // trusting it is both correct and the only way the timeline stays cheap.
    auto dirty = doc.engine().isDirty(sprite.value);
    const bool needsCompile = entry.texture == nullptr ||
                              !dirty.ok() || dirty.value;
    if (!needsCompile) {
        return &entry;
    }
    return compile(doc, sprite, entry);
}

FrameCache::Entry* FrameCache::compile(Document& doc, ls::SpriteId sprite, Entry& into) {
    auto size = doc.engine().getCanvasSize(doc.id());
    if (size.fail() || size.value.x <= 0 || size.value.y <= 0) {
        return nullptr;
    }
    const uint32_t width  = static_cast<uint32_t>(size.value.x);
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
    ++compilesThisFrame_;

    if (compiled.fail()) {
        // Keep whatever was there. A frame that briefly fails to compile should
        // show its last good picture rather than a hole.
        return into.texture != nullptr ? &into : nullptr;
    }
    into.raster = std::move(compiled.value.raster);

    if (into.texture == nullptr || into.width != width || into.height != height) {
        if (into.texture != nullptr) {
            SDL_DestroyTexture(into.texture);
        }
        // RGBA32 is byte-order RGBA on every platform, which is exactly how the
        // engine lays out a raster, so the upload is a straight copy.
        into.texture = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA32,
                                         SDL_TEXTUREACCESS_STREAMING,
                                         static_cast<int>(width),
                                         static_cast<int>(height));
        if (into.texture == nullptr) {
            return nullptr;
        }
        // Pixel art, so never interpolate when magnifying.
        SDL_SetTextureScaleMode(into.texture, SDL_SCALEMODE_NEAREST);
        SDL_SetTextureBlendMode(into.texture, SDL_BLENDMODE_BLEND);
        into.width = width;
        into.height = height;
    }

    if (!into.raster.pixels.empty()) {
        SDL_UpdateTexture(into.texture, nullptr, into.raster.pixels.data(),
                          static_cast<int>(into.raster.stride));
    }
    return &into;
}

} // namespace fast
