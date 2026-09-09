// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

#include "app/ui_state.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace fast {
namespace {

// The subset: a flat object of "name": number. No nesting, no strings, no
// arrays. Small enough to read in one sitting, which is the point -- this parses
// data from files other people send, and a parser nobody can check is a bad
// place for that.

void skipSpace(const std::string& text, size_t& at) {
    while (at < text.size() && (text[at] == ' ' || text[at] == '\t' ||
                                text[at] == '\n' || text[at] == '\r')) {
        ++at;
    }
}

bool readKey(const std::string& text, size_t& at, std::string& out) {
    skipSpace(text, at);
    if (at >= text.size() || text[at] != '"') {
        return false;
    }
    ++at;
    out.clear();
    while (at < text.size() && text[at] != '"') {
        // No escapes. A key containing a backslash is not one this writes, so it
        // is rejected rather than half interpreted.
        if (text[at] == '\\' || out.size() > 64) {
            return false;
        }
        out.push_back(text[at++]);
    }
    if (at >= text.size()) {
        return false;
    }
    ++at;                 // closing quote
    return true;
}

bool readNumber(const std::string& text, size_t& at, double& out) {
    skipSpace(text, at);
    const size_t start = at;
    if (at < text.size() && (text[at] == '-' || text[at] == '+')) {
        ++at;
    }
    while (at < text.size() && ((text[at] >= '0' && text[at] <= '9') ||
                                text[at] == '.' || text[at] == 'e' ||
                                text[at] == 'E' || text[at] == '-' ||
                                text[at] == '+')) {
        ++at;
    }
    if (at == start) {
        return false;
    }

    const std::string token = text.substr(start, at - start);
    char* end = nullptr;
    const double value = std::strtod(token.c_str(), &end);
    if (end != token.c_str() + token.size()) {
        return false;
    }
    // Infinity and NaN parse happily from text like "1e999". Neither belongs
    // anywhere near a view transform.
    if (!std::isfinite(value)) {
        return false;
    }
    out = value;
    return true;
}

bool expect(const std::string& text, size_t& at, char c) {
    skipSpace(text, at);
    if (at >= text.size() || text[at] != c) {
        return false;
    }
    ++at;
    return true;
}

} // namespace

void UiState::clamp(int layerCount, int frameCount, int cycleCount) {
    if (!std::isfinite(zoom)) { zoom = 8.f; }
    if (!std::isfinite(panX)) { panX = 0.f; }
    if (!std::isfinite(panY)) { panY = 0.f; }

    zoom = std::min(std::max(zoom, 1.f), 64.f);

    // A pan far outside any plausible window would put the artwork somewhere
    // the user cannot scroll back to, which looks exactly like a file that
    // failed to open.
    const float kPanLimit = 20000.f;
    panX = std::min(std::max(panX, -kPanLimit), kPanLimit);
    panY = std::min(std::max(panY, -kPanLimit), kPanLimit);

    if (layerCount <= 0) {
        activeLayer = 0;
    } else {
        activeLayer = std::min(std::max(activeLayer, 0), layerCount - 1);
    }

    // A frame index from a file can name a frame the document does not have.
    if (frameCount <= 0) {
        activeFrame = 0;
    } else {
        activeFrame = std::min(std::max(activeFrame, 0), frameCount - 1);
    }
    // Anything outside the cycle list means the default: every frame, in order.
    if (activeCycle < 0 || activeCycle >= cycleCount) {
        activeCycle = -1;
    }

    previewScale = std::min(std::max(previewScale, 1), 4);
    previewTransparent = previewTransparent != 0 ? 1 : 0;
    previewColor &= 0xFFFFFF;      // a packed colour, so the top byte is not ours
}

std::string toJson(const UiState& state) {
    char buffer[320];
    std::snprintf(buffer, sizeof(buffer),
                  "{\"zoom\":%.3f,\"panX\":%.3f,\"panY\":%.3f,\"activeLayer\":%d,"
                  "\"previewScale\":%d,\"previewTransparent\":%d,"
                  "\"previewColor\":%d,\"activeFrame\":%d,\"activeCycle\":%d}",
                  static_cast<double>(state.zoom),
                  static_cast<double>(state.panX),
                  static_cast<double>(state.panY),
                  state.activeLayer,
                  state.previewScale,
                  state.previewTransparent,
                  state.previewColor,
                  state.activeFrame,
                  state.activeCycle);
    return std::string(buffer);
}

bool fromJson(const std::string& text, UiState* out) {
    if (out == nullptr) {
        return false;
    }
    *out = UiState{};

    UiState parsed;
    size_t at = 0;
    if (!expect(text, at, '{')) {
        return false;
    }

    skipSpace(text, at);
    if (at < text.size() && text[at] == '}') {
        *out = parsed;             // an empty object is valid, just says nothing
        return true;
    }

    // A cap on how many members will be read at all, so a pathological file
    // cannot make this spin.
    for (int member = 0; member < 32; ++member) {
        std::string key;
        if (!readKey(text, at, key))    { return false; }
        if (!expect(text, at, ':'))     { return false; }

        double value = 0.0;
        if (!readNumber(text, at, value)) { return false; }

        if (key == "zoom")             { parsed.zoom = static_cast<float>(value); }
        else if (key == "panX")        { parsed.panX = static_cast<float>(value); }
        else if (key == "panY")        { parsed.panY = static_cast<float>(value); }
        else if (key == "activeLayer") { parsed.activeLayer = static_cast<int>(value); }
        else if (key == "previewScale") { parsed.previewScale = static_cast<int>(value); }
        else if (key == "previewTransparent") {
            parsed.previewTransparent = static_cast<int>(value);
        }
        else if (key == "previewColor") { parsed.previewColor = static_cast<int>(value); }
        else if (key == "activeFrame") { parsed.activeFrame = static_cast<int>(value); }
        else if (key == "activeCycle") { parsed.activeCycle = static_cast<int>(value); }
        // Anything else is ignored rather than refused: a newer Fast may write a
        // field this build has never heard of, and that should not stop the file
        // from opening.

        skipSpace(text, at);
        if (at < text.size() && text[at] == ',') {
            ++at;
            continue;
        }
        break;
    }

    if (!expect(text, at, '}')) {
        return false;
    }

    *out = parsed;
    return true;
}

} // namespace fast
