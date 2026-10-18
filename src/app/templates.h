// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors
#pragma once

// templates.h — the sizes a new file usually starts at.
//
// A pixel artist rarely starts from an arbitrary size: an icon, a tile, a
// character, a screen. The start screen offers those as cards, each one click
// from a canvas. A template is only the New window's answers written down in
// advance -- a size, what the canvas starts on, which palette -- so choosing
// one is exactly File > New with those fields filled in, and nothing about a
// document remembers that it came from one.

#include <cstdint>
#include <string>
#include <vector>

namespace fast {

struct NewTemplate {
    std::string name;            // "Character"
    uint32_t    width = 32;
    uint32_t    height = 32;
    int         background = 0;  // as the New window: 0 transparent, 1 white, 2 black, 3 first colour
    int         preset = -1;     // -1 the starter palette, else palettePresets()[preset]
};

// The shipped templates, in the order the start screen shows them.
const std::vector<NewTemplate>& newTemplates();

// "Character 64 x 64": what a card is called, and what a script clicks.
std::string templateLabel(const NewTemplate& t);

} // namespace fast
