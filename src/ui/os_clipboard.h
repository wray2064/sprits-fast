// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors
#pragma once

// os_clipboard.h — images on the system clipboard.
//
// Offered as PNG, which keeps alpha and is what browsers and most editors
// read first, and as a bitmap for the programs that read only that. Read back
// from either, PNG first.
//
// On Windows this talks to the clipboard directly: SDL puts only one image
// format there, and cannot read back a bitmap whose header carries its own
// channel masks -- which is the kind that keeps alpha. Elsewhere it is SDL.

#include <livesprite/livesprite.h>

#include <string>

struct SDL_Window;

namespace fast {

// The window that owns what is put on the clipboard. Call once, after the
// window exists.
void initSystemClipboard(SDL_Window* window);

// Puts an image on the system clipboard. False if nothing could be put.
bool putImageOnClipboard(const ls::RasterBuffer& image);

// Whether the system clipboard holds an image this can read.
bool clipboardHasImage();

// Whether another program has changed the clipboard since Fast last put an
// image on it or took one off it -- that is, whether what is there is news.
bool clipboardChangedElsewhere();

// What was on the clipboard has been dealt with; until it changes again,
// Fast's own clip is the newer.
void markClipboardSeen();

// SDL's clipboard event, for the platforms where that is how a change by
// another program is learnt of.
void noteClipboardEvent(bool owner);

// The image on the system clipboard, decoded; false with a reason otherwise.
bool imageFromClipboard(ls::RasterBuffer* out, std::string* error);

} // namespace fast
