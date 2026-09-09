// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors
#pragma once

// ui_state.h — where you were when you last closed the file.
//
// Zoom, pan and which layer was selected. None of it is part of the artwork, so
// none of it belongs in the engine's document; it rides along in the package as
// a `fast/` entry, which is exactly what namespaced app entries are for. Another
// application opening the same file ignores it, and Fast writing the file back
// does not disturb whatever that application keeps beside it.
//
// **This is untrusted input.** It arrives from whoever sent the file. A zoom of
// 1e30, a NaN pan, or a layer index of four billion are all things a text editor
// can produce in ten seconds, so `apply` clamps rather than trusts. The parser
// reads a deliberately small subset of JSON and rejects anything it does not
// recognise instead of guessing.

#include <string>

namespace fast {

struct UiState {
    float zoom = 8.f;
    float panX = 0.f;
    float panY = 0.f;
    int   activeLayer = 0;

    // The corner preview. Which backdrop someone checks their sprite against is
    // a property of the sprite, not of the session: a character for a night
    // level wants a dark one every time it is opened.
    int   previewScale = 1;
    int   previewTransparent = 1;    // stored as a number; the format has no bool
    int   previewColor = 0x5C8CC8;   // packed 0xRRGGBB

    // Forces every field into a range the editor can actually use. Called on
    // everything that comes out of a file, before any of it reaches the canvas.
    void clamp(int layerCount);
};

std::string toJson(const UiState& state);

// Returns false if the text is not a flat JSON object this understands. `out` is
// left at its defaults in that case rather than half filled.
bool fromJson(const std::string& text, UiState* out);

} // namespace fast
