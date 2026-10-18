// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors
#pragma once

// recent_colours.h — the colours just painted with that are not in the palette.
//
// A colour from the palette is already one click away, so it is not kept
// here. This is for the others: one picked off a reference, typed as a hex
// value, found with the sliders -- gone the moment another is chosen, and
// hard to find again by eye. Most recent first, bounded, no duplicates, kept
// between sessions in the preferences.

#include "app/paint.h"

#include <string>
#include <vector>

namespace fast {

class RecentColours {
public:
    static constexpr size_t kMaxEntries = 16;

    // Moves `colour` to the front, dropping an earlier mention of it and
    // whatever falls off the end. A fully transparent colour is not kept: it
    // is the eraser's, not a colour to come back to.
    void add(ls::Color colour);
    void clear() { entries_.clear(); }

    const std::vector<ls::Color>& entries() const { return entries_; }
    void set(std::vector<ls::Color> entries);

private:
    std::vector<ls::Color> entries_;
};

// "e08f40ff 3060d0ff ...": eight hex digits a colour, most recent first. A
// preferences file is untrusted, so decoding skips what is not eight hex
// digits and stops at the bound.
std::string encodeRecentColours(const std::vector<ls::Color>& colours);
std::vector<ls::Color> decodeRecentColours(const std::string& text);

} // namespace fast
