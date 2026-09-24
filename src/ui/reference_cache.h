// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors
#pragma once

// reference_cache.h — a texture per reference image.
//
// The same shape as FrameCache and for the same reason, with one difference
// that makes it much simpler: a reference never changes. The bytes in the
// package are what they were when they were imported, so there is no
// dirtiness to track -- a reference is decoded and uploaded once, and kept
// until the document that owns it is closed.
//
// What does change is the *set* of references, so entries are dropped by id
// when one is removed or another document is opened.

#include "app/document.h"
#include "app/reference.h"

#include <SDL3/SDL.h>

#include <map>
#include <string>
#include <vector>

namespace fast {

class ReferenceCache {
public:
    explicit ReferenceCache(SDL_Renderer* renderer) : renderer_(renderer) {}
    ~ReferenceCache();

    ReferenceCache(const ReferenceCache&) = delete;
    ReferenceCache& operator=(const ReferenceCache&) = delete;

    struct Entry {
        SDL_Texture* texture = nullptr;
        uint32_t     width = 0;
        uint32_t     height = 0;
    };

    // The texture for this reference, decoding and uploading it the first
    // time. Null when the image will not decode -- which is possible, because
    // the package came off a disk and the bytes in it are only a claim.
    const Entry* entryFor(Document& doc, const Reference& reference);

    // The same for any PNG under any key: the library's thumbnails, which are
    // the same problem -- bytes that never change, decoded once, kept until
    // the thing they belong to goes away.
    //
    // `budget` is how many may be decoded this frame. A folder of fifty
    // sprites would otherwise decode fifty images on the frame it is opened;
    // with a budget the listing fills in over the next few frames instead,
    // which nobody notices and no dropped frame announces.
    const Entry* entryForBytes(const std::string& key, const std::vector<uint8_t>& png,
                               int* budget = nullptr);
    bool holds(const std::string& key) const { return entries_.count(key) != 0; }

    // Drops everything not in `live`. Called when the reference list changes.
    void retainOnly(const std::vector<Reference>& live);
    void retainOnlyKeys(const std::vector<std::string>& live);

    void clear();
    size_t held() const { return entries_.size(); }

private:
    SDL_Renderer*              renderer_ = nullptr;
    std::map<std::string, Entry> entries_;
    // Ids that failed to decode, so a broken image is not retried on every
    // frame for as long as the document is open.
    std::map<std::string, bool> refused_;
};

} // namespace fast
