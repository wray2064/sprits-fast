// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

#include "app/slices.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <set>

namespace fast {

namespace {

constexpr const char* kSlicesKey = "fast.slices";
constexpr size_t kMaxSlices = 512;
constexpr size_t kMaxNameLength = 64;

std::vector<std::string> split(const std::string& text, char by) {
    std::vector<std::string> parts;
    size_t start = 0;
    for (;;) {
        const size_t at = text.find(by, start);
        parts.push_back(text.substr(start, at == std::string::npos ? std::string::npos : at - start));
        if (at == std::string::npos) {
            return parts;
        }
        start = at + 1;
    }
}

bool integer(const std::string& text, int32_t* out) {
    if (text.empty()) {
        return false;
    }
    char* end = nullptr;
    const long value = std::strtol(text.c_str(), &end, 10);
    if (end != text.c_str() + text.size() || value < -1000000 || value > 1000000) {
        return false;
    }
    *out = static_cast<int32_t>(value);
    return true;
}

// A name as a line can carry it: no tabs, no line breaks, not too long.
std::string clean(const std::string& name) {
    std::string out;
    for (char c : name) {
        out += (c == '\t' || c == '\n' || c == '\r') ? ' ' : c;
    }
    if (out.size() > kMaxNameLength) {
        out.resize(kMaxNameLength);
    }
    return out;
}

} // namespace

std::string encodeSlices(const std::vector<Slice>& slices) {
    std::string out;
    for (const Slice& s : slices) {
        char numbers[256];
        std::snprintf(numbers, sizeof(numbers),
                      "\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%02x%02x%02x",
                      s.bounds.min.x, s.bounds.min.y, s.bounds.width(), s.bounds.height(),
                      s.nine ? 1 : 0, s.centre.min.x, s.centre.min.y, s.centre.width(),
                      s.centre.height(), s.hasPivot ? 1 : 0, s.pivot.x, s.pivot.y,
                      s.colour.r, s.colour.g, s.colour.b);
        out += clean(s.name) + numbers + "\n";
    }
    return out;
}

std::vector<Slice> decodeSlices(const std::string& text) {
    std::vector<Slice> out;
    for (const std::string& line : split(text, '\n')) {
        if (line.empty() || out.size() >= kMaxSlices) {
            continue;
        }
        const std::vector<std::string> f = split(line, '\t');
        if (f.size() != 14) {
            continue;
        }
        int32_t v[12];
        bool ok = true;
        for (int i = 0; i < 12 && ok; ++i) {
            ok = integer(f[static_cast<size_t>(i + 1)], &v[i]);
        }
        if (!ok || v[2] <= 0 || v[3] <= 0 || f[13].size() != 6) {
            continue;
        }
        Slice s;
        s.name = clean(f[0]);
        s.bounds = { { v[0], v[1] }, { v[0] + v[2], v[1] + v[3] } };
        s.nine = v[4] != 0;
        s.centre = { { v[5], v[6] }, { v[5] + v[7], v[6] + v[8] } };
        s.hasPivot = v[9] != 0;
        s.pivot = { v[10], v[11] };
        const unsigned long rgb = std::strtoul(f[13].c_str(), nullptr, 16);
        s.colour = { static_cast<uint8_t>((rgb >> 16) & 0xFF), static_cast<uint8_t>((rgb >> 8) & 0xFF),
                     static_cast<uint8_t>(rgb & 0xFF), 255 };
        // A centre has to be inside the bounds to mean anything.
        if (s.nine && (s.centre.min.x < 0 || s.centre.min.y < 0 || s.centre.empty() ||
                       s.centre.max.x > s.bounds.width() || s.centre.max.y > s.bounds.height())) {
            s.nine = false;
        }
        out.push_back(s);
    }
    return out;
}

std::vector<Slice> readSlices(Document& doc) {
    auto text = doc.engine().getMetadata(doc.id().value, kSlicesKey);
    return text.ok() ? decodeSlices(text.value) : std::vector<Slice>{};
}

bool writeSlices(Document& doc, const std::vector<Slice>& slices) {
    if (slices.empty()) {
        doc.engine().clearMetadata(doc.id().value, kSlicesKey);
        return true;
    }
    const std::string text = encodeSlices(slices);
    return doc.engine().setMetadata(doc.id().value, kSlicesKey, text).ok();
}

std::string freeSliceName(const std::vector<Slice>& slices) {
    std::set<std::string> taken;
    for (const Slice& s : slices) {
        taken.insert(s.name);
    }
    for (int n = 1;; ++n) {
        const std::string name = "Slice " + std::to_string(n);
        if (taken.count(name) == 0) {
            return name;
        }
    }
}

} // namespace fast
