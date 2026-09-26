// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors
#pragma once

// layers.h — the stack, as every other editor has it.
//
// Blend modes, opacity, order, copies, groups, clipping and a lock: the things a
// person expects of a layer panel because every editor since the first has had
// them. None of it is new to the engine -- a layer has always carried a blend
// and an opacity, a group has always composited its layers into one buffer --
// but none of it reached the interface, and a stack with nothing but a name
// and an eye reads as a toy.
//
// Everything here is by handle. The engine's own order is the truth about where
// a layer sits; nothing keeps a parallel list. A group is a run of adjacent
// layers with the same parent, which is how the compiler composites one, so
// grouping moves layers next to each other and moving a layer into the middle
// of a group puts it in the group.
//
// Every function that makes more than one engine call brackets its own undo
// action, so a panel button is one history entry however many steps it took.

#include "app/document.h"

#include <string>
#include <vector>

namespace fast {

constexpr const char* kLayerLockedKey = "fast.layer.locked";
constexpr const char* kLayerTagKey = "fast.layer.tag";
constexpr const char* kLayerNotesKey = "fast.layer.notes";

// In enum order, so a combo box is built from the engine's own list.
const std::vector<const char*>& blendModeNames();

struct LayerProps {
    std::string   name;
    float         opacity = 1.f;
    ls::BlendMode blend = ls::BlendMode::Normal;
    bool          visible = true;
    bool          locked = false;
    ls::GroupId   group;        // null at the top level
    ls::LayerId   clipBase;     // null when not clipped
    bool          tagged = false;
    ls::Color     tag;          // the row's colour, when tagged
    std::string   notes;
    bool          reference = false;    // drawn from, never exported
};

bool readLayerProps(Document& doc, ls::LayerId layer, LayerProps* out);

bool setLayerOpacity(Document& doc, ls::LayerId layer, float opacity);
bool setLayerBlend(Document& doc, ls::LayerId layer, ls::BlendMode blend);
bool setLayerVisible(Document& doc, ls::LayerId layer, bool visible);
bool renameLayer(Document& doc, ls::LayerId layer, const std::string& name);

// A lock is the interface's promise not to draw on a layer, kept as metadata
// on the layer: the engine draws whatever it is told to, and the pencil is the
// one that has to refuse. Locking changes no pixel, so it is not an action.
bool layerLocked(Document& doc, ls::LayerId layer);
bool setLayerLocked(Document& doc, ls::LayerId layer, bool locked);

// A reference layer: drawn on like any other and shown on the canvas, but
// left out of every export -- PNGs, sheets, animations, the thumbnail -- and
// out of the outline that traces the whole figure. For a sketch, a
// construction line, a grid to draw over. Brackets no action.
bool isReferenceLayer(Document& doc, ls::LayerId layer);
bool setReferenceLayer(Document& doc, ls::LayerId layer, bool reference);

// A colour to find a layer by in a long stack, and notes about it -- what
// Aseprite calls user data. Metadata like the lock, and like it neither
// changes a pixel. A null tag clears it; empty notes clear them.
bool layerTag(Document& doc, ls::LayerId layer, ls::Color* out);
bool setLayerTag(Document& doc, ls::LayerId layer, const ls::Color* tag);
std::string layerNotes(Document& doc, ls::LayerId layer);
bool setLayerNotes(Document& doc, ls::LayerId layer, const std::string& notes);

// --- order --------------------------------------------------------------------
//
// Index 0 is the bottom of the stack, which is the engine's compositing order.

std::vector<ls::LayerId> layerOrder(Document& doc, ls::SpriteId sprite);
int indexOfLayer(Document& doc, ls::SpriteId sprite, ls::LayerId layer);

// Puts a layer at `toIndex` in its sprite's order. A layer that lands inside a
// group's run joins the group; one that leaves the run leaves the group.
bool moveLayer(Document& doc, ls::LayerId layer, int toIndex);
bool raiseLayer(Document& doc, ls::LayerId layer);     // one step up
bool lowerLayer(Document& doc, ls::LayerId layer);     // one step down

// --- copies -------------------------------------------------------------------

// A copy right above the original, in the same group, named "<name> copy".
// Its drawing is its own from the first stroke. Returns null on failure.
ls::LayerId duplicateLayer(Document& doc, ls::LayerId layer);

// A copy of `source` into `into` at `atIndex` (-1 for the top), which may be
// another frame. This is paste; copy is remembering the handle.
ls::LayerId pasteLayer(Document& doc, ls::LayerId source, ls::SpriteId into, int atIndex);

// --- merging ------------------------------------------------------------------
//
// Merge down, without flattening anything. A layer is a list of elements --
// colours of pixels, shapes -- so merging moves the upper layer's elements into
// the one below, on top of its own, and removes the upper layer. Every element
// stays what it was: a rectangle is still a rectangle, a colour is still a
// slot. The upper layer's opacity and blend are folded into each element it
// gives up, so the picture does not change.
//
// Refused, with the reason, where folding would change the picture or lose
// something: either layer transformed, clipped or outlined, the upper one
// hidden, the two in different groups, or both blending non-normally.
// Brackets its own action. Returns the layer merged into, or null.
ls::LayerId mergeDown(Document& doc, ls::LayerId upper, std::string* why);

// --- groups -------------------------------------------------------------------

struct GroupProps {
    std::string              name;
    float                    opacity = 1.f;
    ls::BlendMode            blend = ls::BlendMode::Normal;
    bool                     visible = true;
    std::vector<ls::LayerId> layers;        // in stack order, bottom first
};

std::vector<ls::GroupId> groupOrder(Document& doc, ls::SpriteId sprite);
bool readGroupProps(Document& doc, ls::GroupId group, GroupProps* out);
ls::GroupId groupOf(Document& doc, ls::LayerId layer);

// Makes a group of the given layers, moving them next to each other at the
// position of the topmost. Layers already in a group leave it. Returns null
// for no layers, or layers of different sprites.
ls::GroupId groupLayers(Document& doc, const std::vector<ls::LayerId>& layers,
                        const std::string& name);

// Dissolves a group: its layers stay where they are, on their own.
bool ungroup(Document& doc, ls::GroupId group);

// Puts a layer into an existing group, at the top of its run. Leaves the
// group it was in, which goes if that empties it.
bool addToGroup(Document& doc, ls::LayerId layer, ls::GroupId group);

// Takes a layer out of its group, placing it just above the group's run so
// it keeps drawing over the same things. False when it was in none.
bool removeFromGroup(Document& doc, ls::LayerId layer);

bool setGroupOpacity(Document& doc, ls::GroupId group, float opacity);
bool setGroupBlend(Document& doc, ls::GroupId group, ls::BlendMode blend);
bool setGroupVisible(Document& doc, ls::GroupId group, bool visible);
bool renameGroup(Document& doc, ls::GroupId group, const std::string& name);

// --- clipping -----------------------------------------------------------------

// Clips a layer to the one below it: it draws only where that one does. Off
// clears the clip. False when there is no layer below.
bool clipToBelow(Document& doc, ls::LayerId layer, bool on);

} // namespace fast
