// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

#include "app/guides.h"

#include <cstdlib>

namespace fast {

namespace {

constexpr const char* kGuidesKey = "fast.guides";
constexpr size_t kMaxGuides = 256;

} // namespace

std::string encodeGuides(const std::vector<Guide>& guides) {
    std::string out;
    for (const Guide& g : guides) {
        if (!out.empty()) {
            out += ';';
        }
        out += (g.vertical ? 'v' : 'h') + std::to_string(g.at);
    }
    return out;
}

std::vector<Guide> decodeGuides(const std::string& text) {
    std::vector<Guide> out;
    size_t start = 0;
    while (start < text.size() && out.size() < kMaxGuides) {
        size_t end = text.find(';', start);
        if (end == std::string::npos) {
            end = text.size();
        }
        const std::string part = text.substr(start, end - start);
        start = end + 1;
        if (part.size() < 2 || (part[0] != 'v' && part[0] != 'h')) {
            continue;
        }
        char* stop = nullptr;
        const long at = std::strtol(part.c_str() + 1, &stop, 10);
        if (stop != part.c_str() + part.size() || at < -100000 || at > 100000) {
            continue;
        }
        out.push_back({ part[0] == 'v', static_cast<int32_t>(at) });
    }
    return out;
}

std::vector<Guide> readGuides(Document& doc) {
    auto text = doc.engine().getMetadata(doc.id().value, kGuidesKey);
    return text.ok() ? decodeGuides(text.value) : std::vector<Guide>{};
}

bool writeGuides(Document& doc, const std::vector<Guide>& guides) {
    if (guides.empty()) {
        doc.engine().clearMetadata(doc.id().value, kGuidesKey);
        return true;
    }
    return doc.engine().setMetadata(doc.id().value, kGuidesKey, encodeGuides(guides)).ok();
}

} // namespace fast
