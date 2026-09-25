// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

#include "app/selection.h"

#include "app/paint.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace fast {

namespace {

ls::IntervalSet fromPixels(const std::vector<ls::Vec2i>& pixels) {
    ls::IntervalSet set;
    set.intervals.reserve(pixels.size());
    for (ls::Vec2i pixel : pixels) {
        set.intervals.push_back({ pixel.y, pixel.x, pixel.x + 1 });
    }
    return ls::geom::normalize(std::move(set));
}

} // namespace

bool Selection::contains(ls::Vec2i pixel) const {
    return ls::geom::contains(mask, pixel);
}

ls::Rect2i Selection::bounds() const {
    return ls::geom::bounds(mask);
}

ls::IntervalSet rectangleMask(ls::Vec2i a, ls::Vec2i b) {
    const int32_t x0 = std::min(a.x, b.x);
    const int32_t x1 = std::max(a.x, b.x);
    const int32_t y0 = std::min(a.y, b.y);
    const int32_t y1 = std::max(a.y, b.y);
    ls::IntervalSet set;
    set.intervals.reserve(static_cast<size_t>(y1 - y0 + 1));
    for (int32_t y = y0; y <= y1; ++y) {
        set.intervals.push_back({ y, x0, x1 + 1 });
    }
    return set;
}

ls::IntervalSet ellipseMask(ls::Vec2i a, ls::Vec2i b) {
    const int32_t x0 = std::min(a.x, b.x);
    const int32_t x1 = std::max(a.x, b.x);
    const int32_t y0 = std::min(a.y, b.y);
    const int32_t y1 = std::max(a.y, b.y);
    const float width = static_cast<float>(x1 - x0 + 1);
    const float height = static_cast<float>(y1 - y0 + 1);
    const float cx = static_cast<float>(x0) + width * 0.5f;
    const float cy = static_cast<float>(y0) + height * 0.5f;
    const float rx = width * 0.5f;
    const float ry = height * 0.5f;

    ls::IntervalSet set;
    for (int32_t y = y0; y <= y1; ++y) {
        // Each row is measured at its edge nearest the middle, so the widest
        // rows span the whole box however thin it is -- a 9x2 ellipse is two
        // full rows, not two rows missing their ends. The same distance above
        // and below the middle, so the result mirrors exactly both ways.
        const float fromMiddle = std::fabs(static_cast<float>(y) + 0.5f - cy);
        const float dy = std::max(0.f, fromMiddle - 0.5f) / ry;
        const float half = rx * std::sqrt(std::max(0.f, 1.f - dy * dy));
        int32_t start = static_cast<int32_t>(std::ceil(cx - half - 0.5f));
        int32_t end = static_cast<int32_t>(std::floor(cx + half - 0.5f));
        if (start > end) {
            // A row the curve only grazes still gets its middle pixel -- or
            // two, for an even width -- so a thin ellipse has no gaps.
            start = static_cast<int32_t>(std::floor(cx - 0.5f));
            end = static_cast<int32_t>(std::ceil(cx - 0.5f));
        }
        start = std::max(start, x0);
        end = std::min(end, x1);
        if (start <= end) {
            set.intervals.push_back({ y, start, end + 1 });
        }
    }
    return set;
}

ls::IntervalSet lassoMask(const std::vector<ls::Vec2i>& points) {
    if (points.empty()) {
        return {};
    }
    // The outline, so a lasso drawn along a one-pixel line still takes it.
    std::vector<ls::Vec2i> outline;
    for (size_t i = 0; i < points.size(); ++i) {
        const ls::Vec2i from = points[i];
        const ls::Vec2i to = points[(i + 1) % points.size()];
        for (ls::Vec2i pixel : linePixels(from, to)) {
            outline.push_back(pixel);
        }
    }
    ls::IntervalSet set = fromPixels(outline);
    if (points.size() < 3) {
        return set;
    }

    // The inside, by pixel centres and the even-odd rule: a scanline through
    // each row's centre, filled between successive crossings.
    int32_t top = points.front().y;
    int32_t bottom = points.front().y;
    for (ls::Vec2i p : points) {
        top = std::min(top, p.y);
        bottom = std::max(bottom, p.y);
    }
    ls::IntervalSet inside;
    std::vector<float> crossings;
    for (int32_t y = top; y <= bottom; ++y) {
        const float scan = static_cast<float>(y) + 0.5f;
        crossings.clear();
        for (size_t i = 0; i < points.size(); ++i) {
            const float ax = static_cast<float>(points[i].x) + 0.5f;
            const float ay = static_cast<float>(points[i].y) + 0.5f;
            const ls::Vec2i next = points[(i + 1) % points.size()];
            const float bx = static_cast<float>(next.x) + 0.5f;
            const float by = static_cast<float>(next.y) + 0.5f;
            // Half-open in y, so a vertex on the scanline counts once.
            if ((ay <= scan && by > scan) || (by <= scan && ay > scan)) {
                crossings.push_back(ax + (scan - ay) * (bx - ax) / (by - ay));
            }
        }
        std::sort(crossings.begin(), crossings.end());
        for (size_t i = 0; i + 1 < crossings.size(); i += 2) {
            const int32_t start = static_cast<int32_t>(std::ceil(crossings[i] - 0.5f));
            const int32_t end = static_cast<int32_t>(std::floor(crossings[i + 1] - 0.5f));
            if (start <= end) {
                inside.intervals.push_back({ y, start, end + 1 });
            }
        }
    }
    return ls::geom::unionSets(set, ls::geom::normalize(std::move(inside)));
}

ls::IntervalSet wandMask(Document& doc, ls::SpriteId sprite, ls::Vec2i seed,
                         const BucketSettings& settings) {
    return fromPixels(bucketArea(doc, sprite, seed, settings));
}

ls::IntervalSet combine(const ls::IntervalSet& current, const ls::IntervalSet& shape,
                        SelectMode mode) {
    switch (mode) {
        case SelectMode::Replace:   return shape;
        case SelectMode::Add:       return ls::geom::unionSets(current, shape);
        case SelectMode::Subtract:  return ls::geom::subtractSets(current, shape);
        case SelectMode::Intersect: return ls::geom::intersectSets(current, shape);
    }
    return shape;
}

ls::IntervalSet clipToCanvas(const ls::IntervalSet& mask, uint32_t width, uint32_t height) {
    if (width == 0 || height == 0) {
        return {};
    }
    return ls::geom::intersectSets(
        mask, rectangleMask({ 0, 0 }, { static_cast<int32_t>(width) - 1,
                                         static_cast<int32_t>(height) - 1 }));
}

ls::IntervalSet translated(const ls::IntervalSet& mask, ls::Vec2i by) {
    ls::IntervalSet out = mask;
    for (ls::Interval& run : out.intervals) {
        run.y += by.y;
        run.x0 += by.x;
        run.x1 += by.x;
    }
    return out;
}

ls::IntervalSet flippedHorizontally(const ls::IntervalSet& mask, ls::Rect2i within) {
    ls::IntervalSet out;
    out.intervals.reserve(mask.intervals.size());
    const int32_t sum = within.min.x + within.max.x;
    for (const ls::Interval& run : mask.intervals) {
        out.intervals.push_back({ run.y, sum - run.x1, sum - run.x0 });
    }
    return ls::geom::normalize(std::move(out));
}

ls::IntervalSet flippedVertically(const ls::IntervalSet& mask, ls::Rect2i within) {
    ls::IntervalSet out;
    out.intervals.reserve(mask.intervals.size());
    const int32_t sum = within.min.y + within.max.y - 1;
    for (const ls::Interval& run : mask.intervals) {
        out.intervals.push_back({ sum - run.y, run.x0, run.x1 });
    }
    return ls::geom::normalize(std::move(out));
}

ls::IntervalSet rotatedQuarter(const ls::IntervalSet& mask, ls::Rect2i within, bool clockwise) {
    const int32_t width = within.width();
    const int32_t height = within.height();
    // The turned box shares the old one's centre, as nearly as whole pixels
    // allow; turning twice the same way is then a half turn about the same
    // point, and four turns are none.
    const ls::Vec2i origin { within.min.x + (width - height) / 2,
                             within.min.y + (height - width) / 2 };
    std::vector<ls::Vec2i> turned;
    for (ls::Vec2i pixel : pixelsOf(mask)) {
        const int32_t u = pixel.x - within.min.x;
        const int32_t v = pixel.y - within.min.y;
        const ls::Vec2i local = clockwise ? ls::Vec2i{ height - 1 - v, u }
                                          : ls::Vec2i{ v, width - 1 - u };
        turned.push_back({ origin.x + local.x, origin.y + local.y });
    }
    return fromPixels(turned);
}

std::vector<ls::Vec2i> pixelsOf(const ls::IntervalSet& mask) {
    std::vector<ls::Vec2i> out;
    out.reserve(static_cast<size_t>(ls::geom::pixelCount(mask)));
    for (const ls::Interval& run : mask.intervals) {
        for (int32_t x = run.x0; x < run.x1; ++x) {
            out.push_back({ x, run.y });
        }
    }
    return out;
}

std::vector<MaskEdge> maskOutline(const ls::IntervalSet& mask) {
    std::vector<MaskEdge> edges;
    if (mask.empty()) {
        return edges;
    }
    // Rows, so each can be compared with the one above it.
    std::map<int32_t, ls::IntervalSet> rows;
    for (const ls::Interval& run : mask.intervals) {
        rows[run.y].intervals.push_back(run);
        edges.push_back({ { run.x0, run.y }, { run.x0, run.y + 1 } });
        edges.push_back({ { run.x1, run.y }, { run.x1, run.y + 1 } });
    }
    const auto rowAt = [&](int32_t y) {
        auto found = rows.find(y);
        ls::IntervalSet row = found == rows.end() ? ls::IntervalSet{} : found->second;
        for (ls::Interval& run : row.intervals) {
            run.y = 0;
        }
        return row;
    };
    const int32_t top = rows.begin()->first;
    const int32_t bottom = rows.rbegin()->first;
    for (int32_t y = top; y <= bottom + 1; ++y) {
        const ls::IntervalSet differs = ls::geom::xorSets(rowAt(y - 1), rowAt(y));
        for (const ls::Interval& run : differs.intervals) {
            edges.push_back({ { run.x0, y }, { run.x1, y } });
        }
    }
    return edges;
}

} // namespace fast
