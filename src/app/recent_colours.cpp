// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

#include "app/recent_colours.h"

#include <algorithm>
#include <cstdio>
#include <utility>

namespace fast {

void RecentColours::add(ls::Color colour) {
    if (colour.a == 0) {
        return;
    }
    entries_.erase(std::remove(entries_.begin(), entries_.end(), colour), entries_.end());
    entries_.insert(entries_.begin(), colour);
    if (entries_.size() > kMaxEntries) {
        entries_.resize(kMaxEntries);
    }
}

void RecentColours::set(std::vector<ls::Color> entries) {
    entries_.clear();
    // Through add, in reverse, so the bound and the no-duplicates rule hold
    // for whatever came in.
    for (auto it = entries.rbegin(); it != entries.rend(); ++it) {
        add(*it);
    }
}

std::string encodeRecentColours(const std::vector<ls::Color>& colours) {
    std::string out;
    for (const ls::Color& c : colours) {
        char text[16];
        std::snprintf(text, sizeof text, "%02x%02x%02x%02x", c.r, c.g, c.b, c.a);
        if (!out.empty()) {
            out += ' ';
        }
        out += text;
    }
    return out;
}

std::vector<ls::Color> decodeRecentColours(const std::string& text) {
    std::vector<ls::Color> out;
    size_t at = 0;
    while (at < text.size() && out.size() < RecentColours::kMaxEntries) {
        while (at < text.size() && text[at] == ' ') {
            ++at;
        }
        const size_t end = text.find(' ', at);
        const std::string word = text.substr(at, end == std::string::npos ? end : end - at);
        at = end == std::string::npos ? text.size() : end;
        if (word.size() != 8) {
            continue;
        }
        unsigned long value = 0;
        bool ok = true;
        for (char ch : word) {
            const int digit = ch >= '0' && ch <= '9' ? ch - '0'
                            : ch >= 'a' && ch <= 'f' ? ch - 'a' + 10
                            : ch >= 'A' && ch <= 'F' ? ch - 'A' + 10 : -1;
            if (digit < 0) {
                ok = false;
                break;
            }
            value = value * 16 + static_cast<unsigned long>(digit);
        }
        if (ok) {
            out.push_back({ static_cast<uint8_t>(value >> 24), static_cast<uint8_t>(value >> 16),
                            static_cast<uint8_t>(value >> 8), static_cast<uint8_t>(value) });
        }
    }
    return out;
}

} // namespace fast
