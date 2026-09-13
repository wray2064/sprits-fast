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
//
// A document may hold several palettes. The document is bound to one -- the
// one every frame uses unless it says otherwise -- and switching that binding
// is a swap: the whole animation recolours in one step, from the drawing. A
// frame may be bound to a palette of its own instead, which is how a single
// frame flashes, and how a palette per frame gives colour cycling for nothing.
//
// Slot edits act on the palette a given frame actually uses, so the panel
// edits what the canvas shows: for a frame with its own palette, that one;
// for every other frame, the document's.

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

struct PaletteInfo {
    ls::PaletteId id;
    std::string   name;
    size_t        colours = 0;
};

// Makes sure the document has a palette, creating a starter palette the first
// time and binding the document to it. `sprite` is the frame being edited;
// a binding of its own that merely repeats the document's is dropped, so the
// frame follows a swap. Safe to call repeatedly.
bool ensurePalette(Document& doc, ls::SpriteId sprite);

// --- several palettes ---------------------------------------------------------

// Every palette the document holds, in the order they were made.
std::vector<PaletteInfo> listPalettes(Document& doc);

// The palette the document is bound to: what every frame uses unless it has
// one of its own. Null only for a document with no palette at all.
ls::PaletteId documentPalette(Document& doc);

// The palette a frame actually uses -- its own, or the document's.
ls::PaletteId paletteFor(Document& doc, ls::SpriteId sprite);

// The palette a layer's frame uses, found from the layer alone.
ls::PaletteId paletteOfLayer(Document& doc, ls::LayerId layer);

// The swap: binds the document to `palette`. Every frame without a binding of
// its own recolours on the next compile.
bool usePalette(Document& doc, ls::PaletteId palette);

// The palette after `current` in the document's list, wrapping -- for a
// quick swap that steps through them.
ls::PaletteId nextPalette(Document& doc, ls::PaletteId current);

// A new palette: a copy of `copyOf` when that is valid, otherwise the starter
// set. Named as given. Returns null on failure.
ls::PaletteId addPalette(Document& doc, const std::string& name, ls::PaletteId copyOf);

bool renamePalette(Document& doc, ls::PaletteId palette, const std::string& name);

// Removes a palette. Refuses the last one, because a document with no palette
// is one where roles mean nothing. Frames bound to it follow the document's;
// if the document was bound to it, the document moves to the first remaining.
bool deletePalette(Document& doc, ls::PaletteId palette);

// A frame's own binding, or null when it follows the document's.
ls::PaletteId frameBinding(Document& doc, ls::SpriteId sprite);

// Gives a frame a palette of its own, or null to make it follow the
// document's again.
bool bindFrame(Document& doc, ls::SpriteId sprite, ls::PaletteId palette);

// --- the slots of one palette -------------------------------------------------
//
// The forms without a palette act on the document's palette.

std::vector<PaletteEntry> paletteEntries(Document& doc);
std::vector<PaletteEntry> paletteEntries(Document& doc, ls::PaletteId palette);

// Changes what a role means. Every layer painting through that role recolours
// on the next compile -- which is the point of the whole arrangement.
bool setPaletteEntry(Document& doc, ls::ColorRole role, ls::Color color);
bool setPaletteEntry(Document& doc, ls::PaletteId palette, ls::ColorRole role,
                     ls::Color color);

// Appends a colour to the palette `sprite` uses and hands back the role it
// was given.
ls::ColorRole addPaletteEntry(Document& doc, ls::SpriteId sprite, ls::Color color);

// Takes a slot out. Everything that painted through it falls back to its own
// literal colour on the next compile -- so removing never breaks a picture,
// but it can change one, which is what usedBy is for asking first.
bool removePaletteEntry(Document& doc, ls::ColorRole role);
bool removePaletteEntry(Document& doc, ls::PaletteId palette, ls::ColorRole role);

// Whether anything in the document paints through this slot: a layer's fill,
// an outline, a ramp end, a region. What to ask before removing it.
bool paletteRoleInUse(Document& doc, ls::ColorRole role);

// Names a slot. Empty clears the name.
bool setPaletteLabel(Document& doc, ls::ColorRole role, const std::string& label);
bool setPaletteLabel(Document& doc, ls::PaletteId palette, ls::ColorRole role,
                     const std::string& label);

// Which role a layer paints through, or kColorRoleNone when it carries its own
// colour.
ls::ColorRole layerRole(Document& doc, const PaintLayer& layer);

// Points a layer at a palette role. Passing kColorRoleNone detaches it, leaving
// it with the colour it is currently showing so nothing changes on screen.
bool setLayerRole(Document& doc, const PaintLayer& layer, ls::ColorRole role);

// The colour a layer actually resolves to, whether that comes from a role or
// from the layer itself. This is what the interface should show.
// What a role means right now, if the palette defines it. False for a role
// the palette does not have -- which is exactly when a layer naming it shows
// its own literal instead -- so the caller can show the same thing the
// compile does.
bool resolvePaletteRole(Document& doc, ls::ColorRole role, ls::Color* out);
bool resolvePaletteRole(Document& doc, ls::PaletteId palette, ls::ColorRole role,
                        ls::Color* out);

ls::Color effectiveLayerColor(Document& doc, ls::SpriteId sprite,
                              const PaintLayer& layer);

} // namespace fast
