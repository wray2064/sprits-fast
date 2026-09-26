// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

#include "app/bucket.h"
#include "app/shape.h"
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

ls::IntervalSet bucketAreaOnLayer(Document& doc, ls::SpriteId sprite, ls::LayerId layer,
                                  ls::Vec2i seed, const BucketSettings& settings,
                                  const ls::IntervalSet* within) {
    ls::IntervalSet area;
    // A layer that is turned or scaled is flooded in its own space, over its
    // own drawing as drawn, from the point clicked carried back: the fill then
    // sits inside the outline as it was drawn and turns with it. The canvas's
    // picture would be the wrong thing to flood, since a turned layer's
    // pixels are not where its drawing is.
    if (!listTransforms(doc, layer).empty()) {
        auto size = doc.engine().getCanvasSize(doc.id());
        ls::Vec2f mapped;
        if (size.fail() ||
            !mapCanvasPointToLayer(doc, layer,
                                   { static_cast<float>(seed.x), static_cast<float>(seed.y) },
                                   &mapped)) {
            return area;
        }
        ls::CompileProfile profile = compileProfile(ls::CompileProfileType::Preview,
                                                    static_cast<uint32_t>(size.value.x),
                                                    static_cast<uint32_t>(size.value.y));
        profile.resolveTransforms = false;
        auto drawn = doc.engine().compileLayer(layer, profile);
        if (drawn.fail()) {
            return area;
        }
        for (ls::Vec2i pixel : flood(drawn.value.raster,
                                     { static_cast<int32_t>(std::floor(mapped.x + 0.5f)),
                                       static_cast<int32_t>(std::floor(mapped.y + 0.5f)) },
                                     settings)) {
            area.intervals.push_back({ pixel.y, pixel.x, pixel.x + 1 });
        }
        area = ls::geom::normalize(area);
        if (within != nullptr) {
            const ls::Mat3f toCanvas = layerTransform(doc, layer);
            ls::IntervalSet kept;
            for (const ls::Interval& run : area.intervals) {
                for (int32_t x = run.x0; x < run.x1; ++x) {
                    const ls::Vec2f at = toCanvas.transformPoint(
                        { static_cast<float>(x) + 0.5f, static_cast<float>(run.y) + 0.5f });
                    if (ls::geom::contains(*within, { static_cast<int32_t>(std::floor(at.x)),
                                                      static_cast<int32_t>(std::floor(at.y)) })) {
                        kept.intervals.push_back({ run.y, x, x + 1 });
                    }
                }
            }
            area = ls::geom::normalize(kept);
        }
        return area;
    }
    for (ls::Vec2i pixel : bucketArea(doc, sprite, seed, settings)) {
        area.intervals.push_back({ pixel.y, pixel.x, pixel.x + 1 });
    }
    area = ls::geom::normalize(area);
    if (within != nullptr) {
        area = ls::geom::intersectSets(area, *within);
    }
    return area;
}

bool bucketFill(Document& doc, ls::SpriteId sprite, const InkStroke& stroke,
                ls::Vec2i seed, const BucketSettings& settings,
                const ls::IntervalSet* within, PaintLayer* made) {
    if (!stroke.layer.valid() || !stroke.target.valid()) {
        return false;
    }
    const ls::IntervalSet area = bucketAreaOnLayer(doc, sprite, stroke.layer, seed, settings, within);
    if (area.empty()) {
        return false;
    }
    ls::LSContext& engine = doc.engine();
    // A fill is an element of its own: the area its flood found, and a seed
    // deep inside it, so that wherever the layer is turned it is found again
    // against the line round it (see the engine's FaceDesc). A fill of every
    // pixel of a colour has no one inside to find again, and stays the area.
    ls::Result<ls::GeometryId> shape = ls::Result<ls::GeometryId>::err(ls::LSError::InvalidId);
    if (settings.global) {
        shape = engine.createArea(doc.id(), ls::geom::traceArea(area));
    } else {
        ls::FaceDesc face;
        face.area = ls::geom::traceArea(area);
        face.seed = ls::geom::deepestPoint(area);
        face.tolerance = settings.tolerance;
        face.diagonal = settings.diagonal;
        shape = engine.createFace(doc.id(), face);
    }
    if (shape.fail()) {
        return false;
    }
    auto region = engine.createRegionFromGeometry(shape.value);
    if (region.fail()) {
        engine.deleteGeometry(shape.value);
        return false;
    }
    // Coloured the way the stroke paints: its ink, or the rule of the element
    // it paints with.
    auto rule = engine.getOperation(stroke.target.fill);
    bool coloured = rule.ok();
    if (coloured) {
        if (auto* solid = std::get_if<ls::FillSolidOp>(&rule.value)) {
            solid->targetRegion = region.value;
        } else if (auto* dither = std::get_if<ls::FillDitherOp>(&rule.value)) {
            dither->targetRegion = region.value;
        } else {
            coloured = false;
        }
    }
    auto op = coloured ? engine.addOperation(stroke.layer, rule.value)
                       : ls::Result<ls::OperationId>::err(ls::LSError::InvalidParameter);
    if (op.fail()) {
        deleteRegionAndShapes(doc, region.value);
        return false;
    }
    keepEffectsLast(doc, stroke.layer);
    if (made != nullptr) {
        made->layer = stroke.layer;
        made->region = region.value;
        made->fill = op.value;
    }
    return true;
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
