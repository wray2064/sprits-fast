// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors
#pragma once

// palette.h — colours as names, not values.
//
// A pixel artist works from a fixed set of colours, and the set matters as much
// as the drawing. Fast had no palette at all, which is a strange omission for a
// pixel-art tool and a stranger one on an engine whose colours are *roles* in
// the first place.
//
// The engine's model: an operation references a `ColorRole`, and the document's
// palette turns that role into a colour when the sprite is compiled. Nothing
// stores the colour with the drawing. So changing a palette entry recolours
// every layer that references it, on the next compile, from the same drawing --
// no repaint, no selection, no undo of a hundred pixels.
//
// That is a real workflow -- swapping a character's palette for a damage flash,
// a night version, a second team colour -- and it is the same property the rest
// of the program rests on, in the place a pixel artist meets it first.
//
// A layer may still carry its own colour instead, with role `kColorRoleNone`.
// Files written before palettes existed do exactly that and keep working.

#include "app/document.h"
#include "app/paint.h"

#include <string>
#include <vector>

namespace fast {

struct PaletteEntry {
    ls::ColorRole role = ls::kColorRoleNone;
    ls::Color     color;
    std::string   label;      // "skin", "outline" -- empty means unnamed
};

// Makes sure the document has a palette and that `sprite` is bound to it,
// creating a starter palette the first time. Safe to call repeatedly.
bool ensurePalette(Document& doc, ls::SpriteId sprite);

std::vector<PaletteEntry> paletteEntries(Document& doc);

// Changes what a role means. Every layer painting through that role recolours
// on the next compile -- which is the point of the whole arrangement.
bool setPaletteEntry(Document& doc, ls::ColorRole role, ls::Color color);

// Appends a colour and hands back the role it was given.
ls::ColorRole addPaletteEntry(Document& doc, ls::SpriteId sprite, ls::Color color);

// Takes a slot out. Everything that painted through it falls back to its own
// literal colour on the next compile -- so removing never breaks a picture,
// but it can change one, which is what usedBy is for asking first.
bool removePaletteEntry(Document& doc, ls::ColorRole role);

// Whether anything in the document paints through this slot: a layer's fill,
// an outline, a ramp end, a region. What to ask before removing it.
bool paletteRoleInUse(Document& doc, ls::ColorRole role);

// Names a slot. Empty clears the name.
bool setPaletteLabel(Document& doc, ls::ColorRole role, const std::string& label);

// Which role a layer paints through, or kColorRoleNone when it carries its own
// colour.
ls::ColorRole layerRole(Document& doc, const PaintLayer& layer);

// Points a layer at a palette role. Passing kColorRoleNone detaches it, leaving
// it with the colour it is currently showing so nothing changes on screen.
bool setLayerRole(Document& doc, const PaintLayer& layer, ls::ColorRole role);

// The colour a layer actually resolves to, whether that comes from a role or
// from the layer itself. This is what the interface should show.
ls::Color effectiveLayerColor(Document& doc, ls::SpriteId sprite,
                              const PaintLayer& layer);

} // namespace fast
