// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

#include "app/bucket.h"
#include "app/transform.h"

#include <algorithm>
#include <cmath>
#include <deque>
#include <vector>

namespace fast {
namespace {

bool within(ls::Color a, ls::Color b, int tolerance) {
    if (tolerance <= 0) {
        return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
    }
    // Two fully transparent pixels match whatever their colour channels say,
    // because nothing has been drawn in either and their RGB is meaningless.
    if (a.a == 0 && b.a == 0) {
        return true;
    }
    const int dr = static_cast<int>(a.r) - b.r;
    const int dg = static_cast<int>(a.g) - b.g;
    const int db = static_cast<int>(a.b) - b.b;
    const int da = static_cast<int>(a.a) - b.a;
    return std::abs(dr) <= tolerance && std::abs(dg) <= tolerance &&
           std::abs(db) <= tolerance && std::abs(da) <= tolerance;
}

// The flood over one picture, from `seed`.
std::vector<ls::Vec2i> flood(const ls::RasterBuffer& raster, ls::Vec2i seed,
                             const BucketSettings& settings) {
    std::vector<ls::Vec2i> filled;
    const int32_t width = static_cast<int32_t>(raster.width);
    const int32_t height = static_cast<int32_t>(raster.height);
    if (seed.x < 0 || seed.y < 0 || seed.x >= width || seed.y >= height) {
        return filled;
    }
    const ls::Color wanted = ls::readPixel(raster, seed.x, seed.y);
    if (settings.global) {
        for (int32_t y = 0; y < height; ++y) {
            for (int32_t x = 0; x < width; ++x) {
                if (within(ls::readPixel(raster, x, y), wanted, settings.tolerance)) {
                    filled.push_back({x, y});
                }
            }
        }
        return filled;
    }
    // Breadth-first rather than recursive: a flood of a large canvas would
    // otherwise be limited by the call stack, and 4096x4096 is 16 million deep
    // in the worst case.
    std::vector<uint8_t> seen(static_cast<size_t>(width) * height, 0);
    std::deque<ls::Vec2i> queue;
    queue.push_back(seed);
    seen[static_cast<size_t>(seed.y) * width + seed.x] = 1;
    static const ls::Vec2i kOrthogonal[4] = { {1,0}, {-1,0}, {0,1}, {0,-1} };
    static const ls::Vec2i kDiagonal[4]   = { {1,1}, {1,-1}, {-1,1}, {-1,-1} };
    while (!queue.empty()) {
        const ls::Vec2i at = queue.front();
        queue.pop_front();
        if (!within(ls::readPixel(raster, at.x, at.y), wanted, settings.tolerance)) {
            continue;
        }
        filled.push_back(at);
        const int neighbours = settings.diagonal ? 8 : 4;
        for (int i = 0; i < neighbours; ++i) {
            const ls::Vec2i step = i < 4 ? kOrthogonal[i] : kDiagonal[i - 4];
            const ls::Vec2i next { at.x + step.x, at.y + step.y };
            if (next.x < 0 || next.y < 0 || next.x >= width || next.y >= height) {
                continue;
            }
            uint8_t& visited = seen[static_cast<size_t>(next.y) * width + next.x];
            if (visited) {
                continue;
            }
            visited = 1;
            queue.push_back(next);
        }
    }
    return filled;
}

} // namespace

std::vector<ls::Vec2i> bucketArea(Document& doc, ls::SpriteId sprite, ls::Vec2i seed,
                                  const BucketSettings& settings) {
    std::vector<ls::Vec2i> filled;

    auto size = doc.engine().getCanvasSize(doc.id());
    if (size.fail() || size.value.x <= 0 || size.value.y <= 0) {
        return filled;
    }
    const int32_t width = size.value.x;
    const int32_t height = size.value.y;

    if (seed.x < 0 || seed.y < 0 || seed.x >= width || seed.y >= height) {
        return filled;
    }

    // What the user is looking at, which is what they are pointing at.
    const ls::CompileProfile profile = compileProfile(
        ls::CompileProfileType::Preview, static_cast<uint32_t>(width), static_cast<uint32_t>(height));

    auto compiled = doc.engine().compileSprite(sprite, profile);
    if (compiled.fail()) {
        return filled;
    }
    return flood(compiled.value.raster, seed, settings);
}

bool bucketFill(Document& doc, ls::SpriteId sprite, const InkStroke& stroke,
                ls::Vec2i seed, const BucketSettings& settings,
                const ls::IntervalSet* within) {
    if (!stroke.layer.valid()) {
        return false;
    }

    // A layer that is turned or scaled is flooded in its own space, over its
    // own drawing as drawn, from the point clicked carried back: the fill then
    // sits inside the outline as it was drawn and turns with it exactly. The
    // canvas's picture would be the wrong thing to flood -- a turned outline
    // can show gaps a fill runs out through -- and carrying a flooded canvas
    // area back pixel by pixel leaves holes, since a turn does not map pixels
    // one to one.
    if (!listTransforms(doc, stroke.layer).empty()) {
        auto size = doc.engine().getCanvasSize(doc.id());
        ls::Vec2f mapped;
        if (size.fail() ||
            !mapCanvasPointToLayer(doc, stroke.layer,
                                   { static_cast<float>(seed.x), static_cast<float>(seed.y) },
                                   &mapped)) {
            return false;
        }
        ls::CompileProfile profile = compileProfile(ls::CompileProfileType::Preview,
                                                    static_cast<uint32_t>(size.value.x),
                                                    static_cast<uint32_t>(size.value.y));
        profile.resolveTransforms = false;
        auto drawn = doc.engine().compileLayer(stroke.layer, profile);
        if (drawn.fail()) {
            return false;
        }
        std::vector<ls::Vec2i> area = flood(
            drawn.value.raster,
            { static_cast<int32_t>(std::floor(mapped.x + 0.5f)),
              static_cast<int32_t>(std::floor(mapped.y + 0.5f)) },
            settings);
        if (within != nullptr) {
            const ls::Mat3f toCanvas = layerTransform(doc, stroke.layer);
            area.erase(std::remove_if(area.begin(), area.end(), [&](ls::Vec2i pixel) {
                           const ls::Vec2f at = toCanvas.transformPoint(
                               { static_cast<float>(pixel.x) + 0.5f, static_cast<float>(pixel.y) + 0.5f });
                           return !ls::geom::contains(*within,
                               { static_cast<int32_t>(std::floor(at.x)),
                                 static_cast<int32_t>(std::floor(at.y)) });
                       }),
                       area.end());
        }
        return !area.empty() && strokeInk(doc, stroke, area);
    }

    std::vector<ls::Vec2i> area = bucketArea(doc, sprite, seed, settings);
    if (within != nullptr) {
        area.erase(std::remove_if(area.begin(), area.end(), [&](ls::Vec2i pixel) {
                       return !ls::geom::contains(*within, pixel);
                   }),
                   area.end());
    }
    if (area.empty()) {
        return false;
    }

    // The flood found pixels on the canvas; the region lives in the layer's own
    // space. With a transform on the layer those are not the same place, so the
    // pixels are carried back the same way the pencil's are.
    std::vector<ls::Vec2i> inLayerSpace;
    inLayerSpace.reserve(area.size());
    for (ls::Vec2i pixel : area) {
        ls::Vec2f mapped;
        if (!mapCanvasPointToLayer(doc, stroke.layer,
                                   {static_cast<float>(pixel.x),
                                    static_cast<float>(pixel.y)}, &mapped)) {
            return false;
        }
        inLayerSpace.push_back({
            static_cast<int32_t>(std::floor(mapped.x + 0.5f)),
            static_cast<int32_t>(std::floor(mapped.y + 0.5f)) });
    }

    return strokeInk(doc, stroke, inLayerSpace);
}

bool bucketFill(Document& doc, ls::SpriteId sprite, const PaintLayer& target,
                ls::Vec2i seed, const BucketSettings& settings) {
    InkStroke stroke;
    if (!beginElementStroke(doc, target, &stroke)) {
        return false;
    }
    return bucketFill(doc, sprite, stroke, seed, settings);
}

} // namespace fast
