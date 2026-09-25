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

std::vector<ls::Vec2i> mirrored(const std::vector<ls::Vec2i>& pixels, const Symmetry& symmetry) {
    if (!symmetry.active()) {
        return pixels;
    }
    std::vector<ls::Vec2i> out;
    out.reserve(pixels.size() * 4);
    const auto add = [&](ls::Vec2i p) {
        for (ls::Vec2i seen : out) {
            if (seen.x == p.x && seen.y == p.y) {
                return;
            }
        }
        out.push_back(p);
    };
    for (ls::Vec2i p : pixels) {
        const ls::Vec2i flippedX { symmetry.axisX - 1 - p.x, p.y };
        const ls::Vec2i flippedY { p.x, symmetry.axisY - 1 - p.y };
        const ls::Vec2i both { symmetry.axisX - 1 - p.x, symmetry.axisY - 1 - p.y };
        add(p);
        if (symmetry.across) { add(flippedX); }
        if (symmetry.down)   { add(flippedY); }
        if (symmetry.across && symmetry.down) { add(both); }
    }
    return out;
}

std::vector<ls::Vec2i> sprayPixels(ls::Vec2i at, int radius, int count, uint32_t seed) {
    std::vector<ls::Vec2i> out;
    if (radius < 1 || count < 1) {
        return out;
    }
    out.reserve(static_cast<size_t>(count));
    uint32_t state = seed * 2654435761u + 1u;
    const auto next = [&state]() {
        state = state * 1664525u + 1013904223u;
        return state >> 8;
    };
    // Rejection inside the square: uniform over the disc, and cheap.
    const int side = radius * 2 + 1;
    for (int tries = 0; static_cast<int>(out.size()) < count && tries < count * 4; ++tries) {
        const int dx = static_cast<int>(next() % static_cast<uint32_t>(side)) - radius;
        const int dy = static_cast<int>(next() % static_cast<uint32_t>(side)) - radius;
        if (dx * dx + dy * dy <= radius * radius) {
            out.push_back({ at.x + dx, at.y + dy });
        }
    }
    return out;
}

ls::Vec2f Stabiliser::follow(ls::Vec2f pointer, float length) {
    const float dx = pointer.x - at_.x;
    const float dy = pointer.y - at_.y;
    const float distance = std::sqrt(dx * dx + dy * dy);
    if (distance > length && distance > 0.f) {
        const float pull = (distance - length) / distance;
        at_.x += dx * pull;
        at_.y += dy * pull;
    }
    return at_;
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
