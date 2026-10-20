// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors
#pragma once

// thumbnails.h — the little picture a saved document carries.
//
// Every save leaves a thumbnail in the package, so a list of files can show what
// each one is without opening it (the start screen's recent files, the library, a
// recovered copy). Decoding one costs a little, so a screen that shows ten spends
// a few a frame (`budget`) and the rest fill in over the next frames.

#include <imgui.h>

#include <string>

namespace fast {

class CanvasView;

// A square (`size` pixels on a side, already scaled for the interface) holding the
// document's thumbnail, fitted inside it in its own proportions, on the canvas colour
// with a hairline round it; an empty one when the file has none or is missing. It is
// an item, so it can be clicked: true when it was. `budget` is how many thumbnails
// may still be decoded this frame (null: no limit); each decode takes one.
bool drawDocumentThumbnail(CanvasView& canvas, const std::string& path, float size,
                           bool enabled = true, int* budget = nullptr);

} // namespace fast
