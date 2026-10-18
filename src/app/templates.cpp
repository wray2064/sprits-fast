// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

#include "app/templates.h"

namespace fast {

const std::vector<NewTemplate>& newTemplates() {
    // Sizes only, on the starter palette and a transparent canvas: the
    // choices any pixel artist would otherwise type in, none of them a
    // style. The last two are screens -- the 16:9 of today's games and the
    // 160 x 144 of the handhelds pixel art grew up on.
    static const std::vector<NewTemplate> templates = {
        { "Icon",      16,  16, 0, -1 },
        { "Tile",      32,  32, 0, -1 },
        { "Character", 64,  64, 0, -1 },
        { "Portrait", 128, 128, 0, -1 },
        { "Scene",    320, 180, 0, -1 },
        { "Handheld", 160, 144, 0, -1 },
    };
    return templates;
}

std::string templateLabel(const NewTemplate& t) {
    return t.name + " " + std::to_string(t.width) + " x " + std::to_string(t.height);
}

} // namespace fast
