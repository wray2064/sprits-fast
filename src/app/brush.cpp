// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

#include "app/brush.h"

#include "app/paint.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace fast {

std::vector<ls::Vec2i> brushStamp(ls::Vec2i at, int size, bool round) {
    std::vector<ls::Vec2i> out;
    size = std::clamp(size, 1, kMaxBrushSize);
    // Odd sizes centre on the pixel; even ones hang right and down from it,
    // which keeps the pointer's pixel inside the stamp at every size.
    const int before = (size - 1) / 2;
    const int x0 = at.x - before;
    const int y0 = at.y - before;
    const float centre = static_cast<float>(size) * 0.5f;
    const float radius = centre;
    out.reserve(static_cast<size_t>(size) * static_cast<size_t>(size));
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            if (round && size >= 3) {
                // Distance from the stamp's centre to the pixel's centre, in
                // pixels; inside the circle the stamp is inscribed in. A
                // touch of slack so a size-3 brush is a plus, not a dot.
                const float dx = static_cast<float>(x) + 0.5f - centre;
                const float dy = static_cast<float>(y) + 0.5f - centre;
                if (dx * dx + dy * dy > radius * radius * 0.8f) {
                    continue;
                }
            }
            out.push_back({ x0 + x, y0 + y });
        }
    }
    return out;
}

std::vector<ls::Vec2i> strokePixels(ls::Vec2i from, ls::Vec2i to, int size, bool round) {
    if (size <= 1) {
        return linePixels(from, to);
    }
    std::vector<ls::Vec2i> out;
    std::set<std::pair<int32_t, int32_t>> seen;
    for (ls::Vec2i centre : linePixels(from, to)) {
        for (ls::Vec2i pixel : brushStamp(centre, size, round)) {
            if (seen.insert({ pixel.x, pixel.y }).second) {
                out.push_back(pixel);
            }
        }
    }
    return out;
}

int pressuredSize(const BrushSettings& brush, float pressure) {
    if (!brush.pressureSize) {
        return brush.size;
    }
    const float clamped = std::clamp(pressure, 0.f, 1.f);
    return std::max(1, static_cast<int>(std::lround(clamped * static_cast<float>(brush.size))));
}

namespace {

bool adjacent4(ls::Vec2i a, ls::Vec2i b) {
    return std::abs(a.x - b.x) + std::abs(a.y - b.y) == 1;
}

bool diagonal(ls::Vec2i a, ls::Vec2i b) {
    return std::abs(a.x - b.x) == 1 && std::abs(a.y - b.y) == 1;
}

} // namespace

std::vector<ls::Vec2i> PixelPerfect::push(ls::Vec2i point) {
    std::vector<ls::Vec2i> ready;
    if (havePending_ && pending_.x == point.x && pending_.y == point.y) {
        return ready;
    }
    if (!havePending_) {
        pending_ = point;
        havePending_ = true;
        return ready;
    }
    // The pending point is the corner of an L when the last painted point
    // and the new one touch diagonally through it. Dropped, the path steps
    // straight from one to the other.
    if (havePainted_ && adjacent4(lastPainted_, pending_) && adjacent4(pending_, point) &&
        diagonal(lastPainted_, point)) {
        pending_ = point;
        return ready;
    }
    ready.push_back(pending_);
    lastPainted_ = pending_;
    havePainted_ = true;
    pending_ = point;
    return ready;
}

std::vector<ls::Vec2i> PixelPerfect::finish() {
    std::vector<ls::Vec2i> ready;
    if (havePending_) {
        ready.push_back(pending_);
    }
    reset();
    return ready;
}

void PixelPerfect::reset() {
    havePainted_ = false;
    havePending_ = false;
}

} // namespace fast
