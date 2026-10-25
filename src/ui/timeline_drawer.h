// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors
#pragma once

// timeline_drawer.h — the Timeline when it is not pinned.
//
// An animator wants the strip in front of them while animating and the whole
// canvas while drawing. So the Timeline is a drawer by default: a small tab along
// the bottom edge of the canvas, with the frame it is on and whether it plays,
// that opens a strip over the bottom of the canvas and puts itself away again.
// It does not take the canvas's height, so nothing moves or resizes when it
// comes and goes.
//
//   - Rest the pointer on the tab (a moment, so that reaching for the canvas's
//     edge does not open it) and the strip opens; move away and it tucks.
//   - Click the tab, or press T, and it stays open until T, Escape or Hide.
//   - Pin puts it back in the dock under the canvas, a panel like the rest,
//     which is what an animation layout wants; Unpin makes it a drawer again.
//
// What is in the strip is the Timeline's own (drawTimelinePanel); this only
// decides where it is and when.

#include "ui/editor.h"

#include <imgui.h>

#include <functional>

namespace fast {

// Brings the Timeline up: the docked panel when it is pinned, otherwise the
// drawer, open and kept open.
void showTimeline(Editor& editor);

// The T key. A pinned Timeline, or one that is not shown, is shown or hidden; a
// drawer is opened or tucked.
void toggleTimeline(Editor& editor);

// Pins it to the dock (shown, as a panel) or lets it go back to being a drawer.
void pinTimeline(Editor& editor, bool pinned);

// The row above the strip, in the panel and in the drawer: Pin or Unpin, and in
// the drawer its title and Hide.
void drawTimelineHeader(Editor& editor, bool inDrawer);

// The unpinned Timeline over `area`, the canvas's rectangle: its tab, and when
// it is open the strip, which `body` draws. Call once a frame while the Timeline
// is shown and not pinned.
void drawTimelineDrawer(Editor& editor, ImVec2 areaPos, ImVec2 areaSize,
                        const std::function<void()>& body);

} // namespace fast
