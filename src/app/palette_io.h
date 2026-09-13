// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors
#pragma once

// palette_io.h — palettes as files other programs understand.
//
// A pixel artist's palette usually arrives from somewhere else: Lospec, a
// previous project, a teammate's Aseprite. Two formats cover nearly all of it.
//
//   .gpl  GIMP's, and what Aseprite reads and writes. One colour per line as
//         "R G B" with an optional name after it, under a small header. It
//         carries names, so it is the one to round-trip our own palettes in.
//   .hex  Lospec's, and the simplest thing that could work: one "rrggbb" per
//         line. No names, no header.
//
// **Both are untrusted input.** They arrive from wherever, and a parser that
// trusts them is the wrong place to be generous. Lines that are not colours are
// skipped rather than failing the file -- one stray line should not cost a
// person the other thirty-one -- but the number of entries is bounded, and a
// value outside 0..255 is a rejected line, not a clamped one, because a colour
// somebody did not mean is worse than a colour that is missing.

#include "app/document.h"
#include "app/palette.h"

#include <string>
#include <vector>

namespace fast {

// Aseprite's indexed ceiling, and what a .gpl in the wild never exceeds. A file
// with more is either not a palette or not one worth loading whole.
constexpr size_t kMaxPaletteEntries = 256;

struct PaletteFile {
    std::string name;                  // from a .gpl header; empty otherwise
    std::vector<PaletteEntry> entries; // roles 0..n-1 in file order
};

// Reads either format, chosen by looking at the text rather than trusting the
// extension. Returns false only if nothing in the text was a colour at all.
bool parsePalette(const std::string& text, PaletteFile* out, std::string* error);

// The two writers. Labels go into .gpl; .hex has nowhere to put them.
std::string toGpl(const std::string& name, const std::vector<PaletteEntry>& entries);
std::string toHex(const std::vector<PaletteEntry>& entries);

// Replaces the document's palette with the file's entries, roles 0..n-1.
//
// Replacing, not appending, because that is what loading a palette means
// everywhere else and because it is the useful thing: a sprite drawn through
// sixteen roles, given a different sixteen-colour palette, recolours -- which
// is the point of roles. A role the new palette does not have falls back to
// the literal colour on every layer that used it; `dropped` says how many
// roles that affected, so the interface can say so.
bool applyPaletteFile(Document& doc, ls::SpriteId sprite, const PaletteFile& file,
                      int* dropped);

// File-level convenience: read, parse, apply; and read the palette, format,
// write. Paths are UTF-8 and go through app/file_io like everything else.
bool importPaletteFile(Document& doc, ls::SpriteId sprite, const std::string& path,
                       int* dropped, std::string* error);
// Writes the palette `sprite` uses -- the one the panel is showing.
bool exportPaletteFile(Document& doc, ls::SpriteId sprite, const std::string& path,
                       std::string* error);

} // namespace fast
