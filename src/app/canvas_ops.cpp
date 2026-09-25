// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

#include "app/canvas_ops.h"

#include "app/animation.h"
#include "app/reference.h"
#include "app/pixel_font.h"
#include "app/selection.h"
#include "app/text.h"
#include "app/transform.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <set>

namespace fast {

namespace {

// How a canvas change moves things. Pixels for authored regions, points --
// pixel corners -- for everything described by coordinates, and a length
// scale for radii.
struct Remap {
    std::function<ls::IntervalSet(const ls::IntervalSet&)> pixels;
    std::function<ls::Vec2f(ls::Vec2f)>                    point;
    float lengthScale = 1.f;
    bool  mirrors = false;       // a flip: turns reverse direction
    bool  swapsAxes = false;     // a quarter turn: x and y trade places
};

// A per-pixel mapping as a mask mapping, for the ones that are bijections.
std::function<ls::IntervalSet(const ls::IntervalSet&)>
eachPixel(std::function<ls::Vec2i(ls::Vec2i)> map) {
    return [map](const ls::IntervalSet& set) {
        ls::IntervalSet out;
        out.intervals.reserve(static_cast<size_t>(ls::geom::pixelCount(set)));
        for (ls::Vec2i pixel : pixelsOf(set)) {
            const ls::Vec2i to = map(pixel);
            out.intervals.push_back({ to.y, to.x, to.x + 1 });
        }
        return ls::geom::normalize(std::move(out));
    };
}

uint64_t handle(Document& doc, ls::OperationId op, const char* name) {
    auto value = doc.engine().getOperationParameter(op, name);
    if (value.fail()) {
        return 0;
    }
    if (const uint64_t* found = std::get_if<uint64_t>(&value.value)) {
        return *found;
    }
    return 0;
}

bool vecParam(Document& doc, ls::OperationId op, const char* name, ls::Vec2f* out) {
    auto value = doc.engine().getOperationParameter(op, name);
    if (value.fail()) {
        return false;
    }
    if (const ls::Vec2f* found = std::get_if<ls::Vec2f>(&value.value)) {
        *out = *found;
        return true;
    }
    return false;
}

// A box through two mapped corners, which is what a rectangle or an
// ellipse's extent becomes under any of these mappings.
void mappedBox(const Remap& remap, ls::Vec2f a, ls::Vec2f b, ls::Vec2f* min, ls::Vec2f* max) {
    const ls::Vec2f p = remap.point(a);
    const ls::Vec2f q = remap.point(b);
    *min = { std::min(p.x, q.x), std::min(p.y, q.y) };
    *max = { std::max(p.x, q.x), std::max(p.y, q.y) };
}

void remapGeometry(Document& doc, ls::GeometryId geometry, const Remap& remap) {
    ls::LSContext& engine = doc.engine();
    if (auto rect = engine.getRect(geometry); rect.ok()) {
        ls::RectDesc desc = rect.value;
        ls::Vec2f min, max;
        mappedBox(remap, desc.origin, { desc.origin.x + desc.width, desc.origin.y + desc.height },
                  &min, &max);
        desc.origin = min;
        desc.width = max.x - min.x;
        desc.height = max.y - min.y;
        desc.cornerRadius *= remap.lengthScale;
        engine.updateRect(geometry, desc);
        return;
    }
    if (auto oval = engine.getEllipse(geometry); oval.ok()) {
        ls::EllipseDesc desc = oval.value;
        ls::Vec2f min, max;
        mappedBox(remap, { desc.center.x - desc.radiusX, desc.center.y - desc.radiusY },
                  { desc.center.x + desc.radiusX, desc.center.y + desc.radiusY }, &min, &max);
        desc.center = { (min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f };
        desc.radiusX = (max.x - min.x) * 0.5f;
        desc.radiusY = (max.y - min.y) * 0.5f;
        engine.updateEllipse(geometry, desc);
        return;
    }
    if (auto line = engine.getPolyline(geometry); line.ok()) {
        ls::PolylineDesc desc = line.value;
        for (ls::Vec2f& p : desc.points) {
            p = remap.point(p);
        }
        engine.updatePolyline(geometry, desc);
    }
}

// Everything every frame holds, moved by `remap`. Each region and each
// geometry is moved once, however many operations name it.
void remapDocument(Document& doc, const Remap& remap) {
    ls::LSContext& engine = doc.engine();
    auto info = engine.getDocumentInfo(doc.id());
    if (info.fail()) {
        return;
    }
    std::set<uint64_t> regions;
    std::set<uint64_t> geometries;
    for (ls::SpriteId sprite : info.value.sprites) {
        auto spriteInfo = engine.getSpriteInfo(sprite);
        if (spriteInfo.fail()) {
            continue;
        }
        for (ls::LayerId layer : spriteInfo.value.layers) {
            auto operations = engine.getLayerOperations(layer);
            if (operations.fail()) {
                continue;
            }
            for (const ls::OperationInfo& op : operations.value) {
                if (const uint64_t region = handle(doc, op.id, "targetRegion"); region != 0) {
                    ls::RegionId id;
                    id.value = region;
                    auto source = engine.getRegionSourceGeometry(id);
                    if (source.ok() && source.value.valid()) {
                        if (geometries.insert(source.value.value).second) {
                            remapGeometry(doc, source.value, remap);
                        }
                    } else if (regions.insert(region).second) {
                        auto set = engine.getRegionIntervals(id);
                        if (set.ok()) {
                            engine.setRegionIntervals(id, remap.pixels(set.value));
                        }
                        // Text keeps its words, place and size on the region;
                        // they move with it, so retyping lands where it now is.
                        TextSpec text;
                        if (readTextElement(doc, id, &text)) {
                            int w = 0;
                            int h = 0;
                            layOutText(text.text, text.at, text.scale, &w, &h);
                            ls::Vec2f min, max;
                            mappedBox(remap,
                                      { static_cast<float>(text.at.x), static_cast<float>(text.at.y) },
                                      { static_cast<float>(text.at.x + w),
                                        static_cast<float>(text.at.y + h) }, &min, &max);
                            text.at = { static_cast<int32_t>(std::floor(min.x + 0.5f)),
                                        static_cast<int32_t>(std::floor(min.y + 0.5f)) };
                            if (remap.lengthScale >= 1.f) {
                                text.scale = std::min(16, static_cast<int>(
                                    static_cast<float>(text.scale) * remap.lengthScale + 0.5f));
                            }
                            engine.setMetadata(id.value, "fast.text.at",
                                               std::to_string(text.at.x) + "," +
                                                   std::to_string(text.at.y));
                            engine.setMetadata(id.value, "fast.text.scale",
                                               std::to_string(text.scale));
                        }
                    }
                }
                if (const uint64_t line = handle(doc, op.id, "polyline"); line != 0) {
                    ls::GeometryId id;
                    id.value = line;
                    if (geometries.insert(line).second) {
                        remapGeometry(doc, id, remap);
                    }
                }
                // A dithered gradient's axis is in canvas coordinates.
                if (op.type == "FillDitherOp") {
                    ls::Vec2f start, end;
                    if (vecParam(doc, op.id, "gradientStart", &start) &&
                        vecParam(doc, op.id, "gradientEnd", &end)) {
                        engine.setOperationParameter(op.id, "gradientStart",
                                                     ls::ParameterValue{ remap.point(start) });
                        engine.setOperationParameter(op.id, "gradientEnd",
                                                     ls::ParameterValue{ remap.point(end) });
                    }
                }
            }
            // A layer's transforms turn about points on the canvas; those move
            // too, and a mirror of the canvas reverses which way a turn goes.
            for (const TransformEntry& entry : listTransforms(doc, layer)) {
                setTransformPivot(doc, entry.id, remap.point(entry.pivot));
                if (entry.kind == TransformKind::Rotate && remap.mirrors) {
                    setRotateAngle(doc, entry.id, -entry.angleDegrees);
                }
                if (entry.kind == TransformKind::Scale && remap.swapsAxes) {
                    setScaleFactor(doc, entry.id, { entry.factor.y, entry.factor.x });
                }
            }
        }
    }

    // References go where the canvas takes them; the picture itself is not
    // turned -- it is somebody else's image, drawn over the work.
    std::vector<Reference> references = readReferences(doc);
    if (!references.empty()) {
        for (Reference& reference : references) {
            ls::Vec2f min, max;
            mappedBox(remap, { reference.x, reference.y },
                      { reference.x + static_cast<float>(reference.width) * reference.scale,
                        reference.y + static_cast<float>(reference.height) * reference.scale },
                      &min, &max);
            reference.x = min.x;
            reference.y = min.y;
            reference.scale *= remap.lengthScale;
        }
        writeReferences(doc, references);
    }
}

bool sizeAllowed(int64_t width, int64_t height, std::string* error) {
    if (width < 1 || height < 1 || width > kMaxCanvasDimension ||
        height > kMaxCanvasDimension ||
        static_cast<uint64_t>(width) * static_cast<uint64_t>(height) > kMaxCanvasPixels) {
        if (error) {
            *error = std::to_string(width) + " x " + std::to_string(height) +
                     " is not a canvas size Fast works on";
        }
        return false;
    }
    return true;
}

ls::Vec2i canvasSize(Document& doc) {
    auto size = doc.engine().getCanvasSize(doc.id());
    return size.ok() ? size.value : ls::Vec2i{ 0, 0 };
}

// One action: move everything, then set the size.
bool apply(Document& doc, const char* label, const Remap& remap, int64_t width,
           int64_t height, std::string* error) {
    if (!sizeAllowed(width, height, error)) {
        return false;
    }
    doc.beginAction(label);
    remapDocument(doc, remap);
    if (doc.engine().setCanvasSize(doc.id(), static_cast<uint32_t>(width),
                                   static_cast<uint32_t>(height)).fail()) {
        doc.abandonAction();
        if (error) { *error = "the engine would not take that size"; }
        return false;
    }
    doc.endAction();
    return true;
}

Remap translation(ls::Vec2i by) {
    Remap remap;
    remap.pixels = [by](const ls::IntervalSet& set) { return translated(set, by); };
    const ls::Vec2f f { static_cast<float>(by.x), static_cast<float>(by.y) };
    remap.point = [f](ls::Vec2f p) { return ls::Vec2f{ p.x + f.x, p.y + f.y }; };
    return remap;
}

} // namespace

bool resizeCanvas(Document& doc, uint32_t width, uint32_t height, CanvasAnchor anchor,
                  std::string* error) {
    const ls::Vec2i old = canvasSize(doc);
    const int column = static_cast<int>(anchor) % 3;      // 0 left, 1 centre, 2 right
    const int row = static_cast<int>(anchor) / 3;
    const int dw = static_cast<int>(width) - old.x;
    const int dh = static_cast<int>(height) - old.y;
    // Halves round toward the top left, so centring an odd difference is
    // stable: grow by one and shrink by one and nothing has moved.
    const auto share = [](int difference, int where) {
        if (where == 0) { return 0; }
        if (where == 2) { return difference; }
        return static_cast<int>(std::floor(static_cast<float>(difference) / 2.f));
    };
    return apply(doc, "Canvas size", translation({ share(dw, column), share(dh, row) }),
                 width, height, error);
}

bool cropCanvas(Document& doc, ls::Rect2i to, std::string* error) {
    if (to.empty()) {
        if (error) { *error = "there is nothing there to crop to"; }
        return false;
    }
    return apply(doc, "Crop", translation({ -to.min.x, -to.min.y }), to.width(),
                 to.height(), error);
}

ls::Rect2i contentBounds(Document& doc) {
    const ls::Vec2i size = canvasSize(doc);
    ls::Rect2i bounds { { size.x, size.y }, { 0, 0 } };
    bool any = false;
    for (const Frame& frame : readFrames(doc)) {
        const ls::CompileProfile profile =
            compileProfile(ls::CompileProfileType::Export, static_cast<uint32_t>(size.x),
                           static_cast<uint32_t>(size.y));
        auto compiled = doc.engine().compileSprite(frame.sprite, profile);
        if (compiled.fail()) {
            continue;
        }
        const ls::RasterBuffer& raster = compiled.value.raster;
        for (uint32_t y = 0; y < raster.height; ++y) {
            const uint8_t* row = raster.row(y);
            for (uint32_t x = 0; x < raster.width; ++x) {
                if (row[static_cast<size_t>(x) * 4u + 3u] == 0) {
                    continue;
                }
                any = true;
                bounds.min.x = std::min(bounds.min.x, static_cast<int32_t>(x));
                bounds.min.y = std::min(bounds.min.y, static_cast<int32_t>(y));
                bounds.max.x = std::max(bounds.max.x, static_cast<int32_t>(x) + 1);
                bounds.max.y = std::max(bounds.max.y, static_cast<int32_t>(y) + 1);
            }
        }
    }
    return any ? bounds : ls::Rect2i{};
}

bool trimCanvas(Document& doc, std::string* error) {
    const ls::Rect2i bounds = contentBounds(doc);
    if (bounds.empty()) {
        if (error) { *error = "nothing is drawn, so there is nothing to trim to"; }
        return false;
    }
    return cropCanvas(doc, bounds, error);
}

bool flipCanvas(Document& doc, bool horizontally, std::string* error) {
    const ls::Vec2i size = canvasSize(doc);
    Remap remap;
    remap.mirrors = true;
    if (horizontally) {
        remap.pixels = eachPixel([size](ls::Vec2i p) { return ls::Vec2i{ size.x - 1 - p.x, p.y }; });
        remap.point = [size](ls::Vec2f p) { return ls::Vec2f{ static_cast<float>(size.x) - p.x, p.y }; };
    } else {
        remap.pixels = eachPixel([size](ls::Vec2i p) { return ls::Vec2i{ p.x, size.y - 1 - p.y }; });
        remap.point = [size](ls::Vec2f p) { return ls::Vec2f{ p.x, static_cast<float>(size.y) - p.y }; };
    }
    return apply(doc, horizontally ? "Flip canvas horizontally" : "Flip canvas vertically",
                 remap, size.x, size.y, error);
}

bool rotateCanvas(Document& doc, int quarterTurns, std::string* error) {
    const ls::Vec2i size = canvasSize(doc);
    const float w = static_cast<float>(size.x);
    const float h = static_cast<float>(size.y);
    const int turns = ((quarterTurns % 4) + 4) % 4;
    if (turns == 0) {
        return true;
    }
    Remap remap;
    remap.swapsAxes = turns != 2;
    if (turns == 1) {            // clockwise: the left edge becomes the top
        remap.pixels = eachPixel([size](ls::Vec2i p) { return ls::Vec2i{ size.y - 1 - p.y, p.x }; });
        remap.point = [h](ls::Vec2f p) { return ls::Vec2f{ h - p.y, p.x }; };
    } else if (turns == 2) {
        remap.pixels = eachPixel([size](ls::Vec2i p) {
            return ls::Vec2i{ size.x - 1 - p.x, size.y - 1 - p.y };
        });
        remap.point = [w, h](ls::Vec2f p) { return ls::Vec2f{ w - p.x, h - p.y }; };
    } else {                     // anticlockwise
        remap.pixels = eachPixel([size](ls::Vec2i p) { return ls::Vec2i{ p.y, size.x - 1 - p.x }; });
        remap.point = [w](ls::Vec2f p) { return ls::Vec2f{ p.y, w - p.x }; };
    }
    const bool quarter = turns != 2;
    return apply(doc, turns == 2 ? "Rotate canvas 180" : "Rotate canvas 90", remap,
                 quarter ? size.y : size.x, quarter ? size.x : size.y, error);
}

bool enlargeSprite(Document& doc, uint32_t factor, std::string* error) {
    if (factor < 2) {
        if (error) { *error = "enlarging is by two or more"; }
        return false;
    }
    const ls::Vec2i size = canvasSize(doc);
    const int32_t k = static_cast<int32_t>(factor);
    Remap remap;
    remap.lengthScale = static_cast<float>(factor);
    // Each run becomes k runs k times as long: exact, with no sampling.
    remap.pixels = [k](const ls::IntervalSet& set) {
        ls::IntervalSet out;
        out.intervals.reserve(set.intervals.size() * static_cast<size_t>(k));
        for (const ls::Interval& run : set.intervals) {
            for (int32_t dy = 0; dy < k; ++dy) {
                out.intervals.push_back({ run.y * k + dy, run.x0 * k, run.x1 * k });
            }
        }
        return ls::geom::normalize(std::move(out));
    };
    const float f = static_cast<float>(factor);
    remap.point = [f](ls::Vec2f p) { return ls::Vec2f{ p.x * f, p.y * f }; };
    return apply(doc, "Enlarge sprite", remap, static_cast<int64_t>(size.x) * k,
                 static_cast<int64_t>(size.y) * k, error);
}

namespace {

int64_t floorDiv(int64_t a, int64_t b) {
    return a >= 0 ? a / b : -((-a + b - 1) / b);
}

// The first new pixel whose centre falls at or past the old boundary `x`,
// scaling `from` pixels to `to`: the least X with (X + 0.5) * from / to >= x.
int32_t firstCentreAt(int64_t x, int64_t from, int64_t to) {
    return static_cast<int32_t>(-floorDiv(-(2 * x * to - from), 2 * from));
}

} // namespace

bool resizeSprite(Document& doc, uint32_t width, uint32_t height, std::string* error) {
    const ls::Vec2i size = canvasSize(doc);
    if (size.x <= 0 || size.y <= 0) {
        if (error) { *error = "there is no canvas to resize"; }
        return false;
    }
    const int64_t sw = size.x;
    const int64_t sh = size.y;
    const int64_t dw = width;
    const int64_t dh = height;
    if (dw == sw && dh == sh) {
        if (error) { *error = "that is the size it already is"; }
        return false;
    }
    Remap remap;
    const float sx = static_cast<float>(dw) / static_cast<float>(sw);
    const float sy = static_cast<float>(dh) / static_cast<float>(sh);
    remap.lengthScale = std::sqrt(sx * sy);
    // Row by row: an old row becomes the new rows whose centres fall in it,
    // and each run in it becomes the new columns whose centres fall in it.
    remap.pixels = [sw, sh, dw, dh](const ls::IntervalSet& set) {
        ls::IntervalSet out;
        const std::vector<ls::Interval>& runs = set.intervals;
        size_t i = 0;
        while (i < runs.size()) {
            const int32_t y = runs[i].y;
            size_t j = i;
            while (j < runs.size() && runs[j].y == y) {
                ++j;
            }
            const int32_t y0 = firstCentreAt(y, sh, dh);
            const int32_t y1 = firstCentreAt(int64_t(y) + 1, sh, dh);
            for (int32_t row = y0; row < y1; ++row) {
                for (size_t k = i; k < j; ++k) {
                    const int32_t x0 = firstCentreAt(runs[k].x0, sw, dw);
                    const int32_t x1 = firstCentreAt(runs[k].x1, sw, dw);
                    if (x0 < x1) {
                        out.intervals.push_back({ row, x0, x1 });
                    }
                }
            }
            i = j;
        }
        return ls::geom::normalize(std::move(out));
    };
    remap.point = [sx, sy](ls::Vec2f p) { return ls::Vec2f{ p.x * sx, p.y * sy }; };
    return apply(doc, "Resize sprite", remap, dw, dh, error);
}

bool reduceSprite(Document& doc, uint32_t factor, std::string* error) {
    if (factor < 2) {
        if (error) { *error = "reducing is by two or more"; }
        return false;
    }
    const ls::Vec2i size = canvasSize(doc);
    const int32_t k = static_cast<int32_t>(factor);
    if (size.x / k < 1 || size.y / k < 1) {
        if (error) { *error = "the canvas is too small to reduce that far"; }
        return false;
    }
    Remap remap;
    remap.lengthScale = 1.f / static_cast<float>(factor);
    // A pixel survives when the top-left pixel of its block was drawn: the
    // nearest-neighbour rule, which keeps hard edges hard.
    remap.pixels = [k](const ls::IntervalSet& set) {
        ls::IntervalSet out;
        for (const ls::Interval& run : set.intervals) {
            if (run.y % k != 0) {
                continue;
            }
            const int32_t first = (run.x0 + k - 1) / k;       // blocks whose corner is in the run
            const int32_t last = (run.x1 - 1) / k;
            if (run.x0 < 0 || first > last) {
                continue;
            }
            out.intervals.push_back({ run.y / k, first, last + 1 });
        }
        return ls::geom::normalize(std::move(out));
    };
    const float f = static_cast<float>(factor);
    remap.point = [f](ls::Vec2f p) { return ls::Vec2f{ p.x / f, p.y / f }; };
    return apply(doc, "Reduce sprite", remap, size.x / k, size.y / k, error);
}

} // namespace fast
