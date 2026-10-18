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

std::string documentNameFrom(const std::string& typed) {
    std::string kept;
    for (char c : typed) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (u < 0x20 || u == 0x7F || c == '\\' || c == '/' || c == ':' || c == '*' ||
            c == '?' || c == '"' || c == '<' || c == '>' || c == '|') {
            continue;
        }
        kept += c;
    }
    // Trim spaces and dots: a name may not end in either on Windows.
    while (!kept.empty() && (kept.back() == ' ' || kept.back() == '.')) {
        kept.pop_back();
    }
    size_t start = 0;
    // Leading dots too: ".." with its slashes taken out must not be left
    // looking like a path, nor a name made hidden.
    while (start < kept.size() && (kept[start] == ' ' || kept[start] == '.')) {
        ++start;
    }
    kept = kept.substr(start);
    constexpr size_t kMaxBytes = 48;
    if (kept.size() > kMaxBytes) {
        size_t cut = kMaxBytes;
        while (cut > 0 && (static_cast<unsigned char>(kept[cut]) & 0xC0) == 0x80) {
            --cut;                      // not inside a UTF-8 character
        }
        kept.resize(cut);
        while (!kept.empty() && (kept.back() == ' ' || kept.back() == '.')) {
            kept.pop_back();
        }
    }
    return kept.empty() ? std::string("untitled") : kept;
}

} // namespace fast
