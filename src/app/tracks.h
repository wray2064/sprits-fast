// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors
#pragma once

// tracks.h — layers that are the same in every frame.
//
// A frame is a sprite, and a sprite has its own stack of layers. That is
// flexible, and it is not how an animator thinks: an "outline" layer is one
// layer that runs through the whole animation, hidden or moved or renamed
// once for all of it, holding different pixels in each frame -- Aseprite's
// timeline grid, a layer per row and a cel per frame.
//
// So a document can keep its frames' stacks in step. Each layer carries a
// track key, the same in every frame; each group too. After any change to one
// frame's stack -- a layer added, deleted, moved, renamed, hidden, grouped,
// tagged -- every other frame is brought into line with it: the same tracks
// in the same order with the same properties, each frame keeping its own
// pixels. A track new to the edited frame arrives empty elsewhere, unless it
// was a duplicate of another track, when each frame copies its own cel of
// that one. A layer with no key -- from an older file, or a frame made some
// other way -- is never thrown away: it is matched to a track by its name, or
// becomes a track of its own in every frame.
//
// The work is done in the same history action as the change, so one undo
// takes both back.

#include "app/document.h"

#include <string>

namespace fast {

// Whether this document keeps its frames' layers in step. New documents do;
// files from before this, whose frames were made separately, do not until
// asked (see adoptTracks).
bool tracksOn(Document& doc);
void setTracksOn(Document& doc, bool on);

// A layer's track key, or empty.
std::string trackKey(Document& doc, ls::LayerId layer);

// Marks a layer as a copy of `source`'s track, so that the next sync makes it
// a new track whose cel in every frame is a copy of that frame's cel of the
// source. Clears the copy's own key. What duplicating a layer calls.
void markTrackCopy(Document& doc, ls::LayerId copy, ls::LayerId source);

// Marks a layer as a new track of its own, arriving empty in other frames:
// clears the key a clone carried. What pasting a layer calls.
void markNewTrack(Document& doc, ls::LayerId layer);

// Brings every frame's stack into line with `master`'s. Nothing when tracks
// are off or every frame already matches. Does not bracket an action.
// Returns whether anything changed.
bool syncTracks(Document& doc, ls::SpriteId master);

// Turns tracks on for a document whose frames were made separately: layers
// are matched across frames by name, every track is put in every frame, and
// nothing any frame holds is lost. Does not bracket an action.
void adoptTracks(Document& doc, ls::SpriteId master);

// The layers of the same track in another frame: `layer`'s counterpart in
// `frame`, or null.
ls::LayerId layerOfTrack(Document& doc, ls::SpriteId frame, const std::string& key);

} // namespace fast
