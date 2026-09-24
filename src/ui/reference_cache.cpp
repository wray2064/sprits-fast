// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

#include "ui/reference_cache.h"

#include "app/image_io.h"

#include <algorithm>

namespace fast {

ReferenceCache::~ReferenceCache() {
    clear();
}

void ReferenceCache::clear() {
    for (auto& [id, entry] : entries_) {
        if (entry.texture != nullptr) {
            SDL_DestroyTexture(entry.texture);
        }
    }
    entries_.clear();
    refused_.clear();
}

void ReferenceCache::retainOnly(const std::vector<Reference>& live) {
    std::vector<std::string> keys;
    keys.reserve(live.size());
    for (const Reference& reference : live) {
        keys.push_back(reference.id);
    }
    retainOnlyKeys(keys);
}

void ReferenceCache::retainOnlyKeys(const std::vector<std::string>& live) {
    for (auto it = entries_.begin(); it != entries_.end(); ) {
        const bool keep = std::any_of(live.begin(), live.end(),
            [&](const std::string& key) { return key == it->first; });
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

const ReferenceCache::Entry* ReferenceCache::entryFor(Document& doc,
                                                      const Reference& reference) {
    const std::vector<uint8_t>* bytes = referenceBytes(doc, reference);
    if (bytes == nullptr) {
        refused_[reference.id] = true;
        return nullptr;
    }
    return entryForBytes(reference.id, *bytes);
}

const ReferenceCache::Entry* ReferenceCache::entryForBytes(
        const std::string& key, const std::vector<uint8_t>& png, int* budget) {
    auto found = entries_.find(key);
    if (found != entries_.end()) {
        return found->second.texture != nullptr ? &found->second : nullptr;
    }
    if (refused_.count(key) != 0) {
        return nullptr;
    }
    if (budget != nullptr) {
        if (*budget <= 0) {
            return nullptr;           // next frame; nothing is lost by waiting
        }
        --*budget;
    }

    ls::RasterBuffer raster;
    std::string error;
    if (!decodeImage(png, &raster, &error) || raster.empty()) {
        refused_[key] = true;
        return nullptr;
    }

    Entry entry;
    entry.width = raster.width;
    entry.height = raster.height;
    entry.texture = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA32,
                                      SDL_TEXTUREACCESS_STATIC,
                                      static_cast<int>(raster.width),
                                      static_cast<int>(raster.height));
    if (entry.texture == nullptr) {
        refused_[key] = true;
        return nullptr;
    }
    // A reference is a photograph as often as it is pixel art, and it is
    // being matched to a size rather than magnified as pixels, so it is the
    // one thing here drawn with smoothing. The canvas brackets its own
    // artwork with nearest; this is drawn outside those brackets.
    SDL_SetTextureScaleMode(entry.texture, SDL_SCALEMODE_LINEAR);
    SDL_SetTextureBlendMode(entry.texture, SDL_BLENDMODE_BLEND);
    SDL_UpdateTexture(entry.texture, nullptr, raster.pixels.data(),
                      static_cast<int>(raster.stride));

    auto placed = entries_.emplace(key, entry);
    return &placed.first->second;
}

} // namespace fast
