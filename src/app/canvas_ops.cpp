// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

#include "app/canvas_ops.h"
#include "app/tilemap.h"
#include "app/layers.h"
#include "app/guides.h"
#include "app/slices.h"

#include "app/animation.h"
#include "app/reference.h"
#include "app/pixel_font.h"
#include "app/selection.h"
#include "app/text.h"
#include "app/transform.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
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
        return;
    }
    if (auto polygon = engine.getPolygon(geometry); polygon.ok()) {
        ls::PolygonDesc desc = polygon.value;
        for (ls::Vec2f& p : desc.vertices) {
            p = remap.point(p);
        }
        engine.updatePolygon(geometry, desc);
        return;
    }
    if (auto curve = engine.getCurve(geometry); curve.ok()) {
        ls::CurveDesc desc = curve.value;
        for (ls::CurveDesc::Segment& s : desc.segments) {
            s.p0 = remap.point(s.p0);
            s.cp0 = remap.point(s.cp0);
            s.cp1 = remap.point(s.cp1);
            s.p1 = remap.point(s.p1);
        }
        engine.updateCurve(geometry, desc);
        return;
    }
    // Freehand marks, areas and fills: every point moved, a brush grown or
    // shrunk with what it drew.
    const auto moveArea = [&remap](ls::AreaDesc& area) {
        for (auto& contour : area.contours) {
            for (ls::Vec2f& p : contour) {
                p = remap.point(p);
            }
        }
    };
    if (auto strokes = engine.getStrokes(geometry); strokes.ok()) {
        ls::StrokesDesc desc = strokes.value;
        for (ls::PenStroke& mark : desc.strokes) {
            for (ls::Vec2f& p : mark.points) {
                p = remap.point(p);
            }
            mark.size = std::max(1.f, mark.size * remap.lengthScale);
            for (float& size : mark.sizes) {
                size = std::max(1.f, size * remap.lengthScale);
            }
            moveArea(mark.area);
        }
        engine.updateStrokes(geometry, desc);
        return;
    }
    if (auto area = engine.getArea(geometry); area.ok()) {
        ls::AreaDesc desc = area.value;
        moveArea(desc);
        engine.updateArea(geometry, desc);
        return;
    }
    if (auto face = engine.getFace(geometry); face.ok()) {
        ls::FaceDesc desc = face.value;
        desc.seed = remap.point(desc.seed);
        moveArea(desc.area);
        engine.updateFace(geometry, desc);
    }
}

// The turn a remap gives a tile: its linear part, as the images of one step
// across and one step down. False when it is not a flip or a quarter turn --
// a scale, which tiles cannot follow, since they keep their size.
bool tileTurn(const Remap& remap, int m[4]) {
    const ls::Vec2f o = remap.point({ 0.f, 0.f });
    const ls::Vec2f a = remap.point({ 1.f, 0.f });
    const ls::Vec2f b = remap.point({ 0.f, 1.f });
    const float v[4] = { a.x - o.x, b.x - o.x, a.y - o.y, b.y - o.y };
    for (int i = 0; i < 4; ++i) {
        const float r = std::round(v[i]);
        if (std::fabs(v[i] - r) > 0.001f || std::fabs(r) > 1.f) {
            return false;
        }
        m[i] = static_cast<int>(r);
    }
    const bool straight = m[1] == 0 && m[2] == 0 && m[0] != 0 && m[3] != 0;
    const bool crossed = m[0] == 0 && m[3] == 0 && m[1] != 0 && m[2] != 0;
    return straight || crossed;
}

// Every tilemap layer of every frame.
std::vector<TilemapLayer> allTilemaps(Document& doc) {
    std::vector<TilemapLayer> out;
    for (const Frame& frame : readFrames(doc)) {
        for (ls::LayerId layer : layerOrder(doc, frame.sprite)) {
            TilemapLayer map;
            if (readTilemapLayer(doc, layer, &map)) {
                out.push_back(std::move(map));
            }
        }
    }
    return out;
}

// Whether the tilemaps can follow `remap`: a flip, a quarter turn of square
// tiles, or a move. Said when not.
bool tilemapsFollow(Document& doc, const Remap& remap, std::string* error) {
    const std::vector<TilemapLayer> maps = allTilemaps(doc);
    if (maps.empty()) {
        return true;
    }
    int m[4];
    if (!tileTurn(remap, m)) {
        if (error) { *error = "tilemap layers keep their tiles' size, so the sprite cannot be scaled with them"; }
        return false;
    }
    if (m[1] != 0) {
        for (const TilemapLayer& map : maps) {
            if (map.grid.tileWidth != map.grid.tileHeight) {
                if (error) { *error = "a quarter turn needs square tiles, and a tilemap layer's are not"; }
                return false;
            }
        }
    }
    return true;
}

// One tilemap moved by `remap`: its cells put where the canvas takes them and
// turned with it, then the grid grown to cover a canvas `size` big. Cells off
// the canvas are kept, as pixels are.
void remapTilemap(Document& doc, const TilemapLayer& map, const Remap& remap, ls::Vec2i size,
                  ls::Vec2i* origin) {
    int m[4];
    if (!tileTurn(remap, m)) {
        return;
    }
    const ls::TilemapDesc& old = map.grid;
    const bool swaps = m[1] != 0;
    ls::TilemapDesc grid;
    grid.tileWidth = swaps ? old.tileHeight : old.tileWidth;
    grid.tileHeight = swaps ? old.tileWidth : old.tileHeight;
    grid.columns = swaps ? old.rows : old.columns;
    grid.rows = swaps ? old.columns : old.rows;
    grid.cells.assign(static_cast<size_t>(grid.columns) * grid.rows, 0u);
    const float tw = static_cast<float>(old.tileWidth);
    const float th = static_cast<float>(old.tileHeight);
    const ls::Vec2f o { static_cast<float>(map.origin.x), static_cast<float>(map.origin.y) };
    ls::Vec2f min, max;
    mappedBox(remap, o, { o.x + tw * static_cast<float>(old.columns), o.y + th * static_cast<float>(old.rows) },
              &min, &max);
    for (uint32_t r = 0; r < old.rows; ++r) {
        for (uint32_t c = 0; c < old.columns; ++c) {
            const ls::Vec2f at { o.x + tw * static_cast<float>(c), o.y + th * static_cast<float>(r) };
            ls::Vec2f a, b;
            mappedBox(remap, at, { at.x + tw, at.y + th }, &a, &b);
            const long nc = std::lround((a.x - min.x) / static_cast<float>(grid.tileWidth));
            const long nr = std::lround((a.y - min.y) / static_cast<float>(grid.tileHeight));
            if (nc < 0 || nr < 0 || nc >= static_cast<long>(grid.columns) ||
                nr >= static_cast<long>(grid.rows)) {
                continue;
            }
            grid.cells[static_cast<size_t>(nr) * grid.columns + static_cast<size_t>(nc)] =
                turnedCell(old.cells[static_cast<size_t>(r) * old.columns + c], m);
        }
    }
    ls::Vec2i placed { static_cast<int32_t>(std::lround(min.x)), static_cast<int32_t>(std::lround(min.y)) };

    // Grown to cover the canvas, whichever sides it now falls short of.
    const int32_t gw = static_cast<int32_t>(grid.tileWidth);
    const int32_t gh = static_cast<int32_t>(grid.tileHeight);
    int32_t left = 0, top = 0, right = 0, bottom = 0;
    while (placed.x - left * gw > 0) { ++left; }
    while (placed.y - top * gh > 0) { ++top; }
    while (placed.x + (static_cast<int32_t>(grid.columns) + right) * gw < size.x) { ++right; }
    while (placed.y + (static_cast<int32_t>(grid.rows) + bottom) * gh < size.y) { ++bottom; }
    if (left + top + right + bottom > 0) {
        ls::TilemapDesc grown = grid;
        grown.columns = grid.columns + static_cast<uint32_t>(left + right);
        grown.rows = grid.rows + static_cast<uint32_t>(top + bottom);
        grown.cells.assign(static_cast<size_t>(grown.columns) * grown.rows, 0u);
        for (uint32_t r = 0; r < grid.rows; ++r) {
            for (uint32_t c = 0; c < grid.columns; ++c) {
                grown.cells[static_cast<size_t>(r + static_cast<uint32_t>(top)) * grown.columns +
                            c + static_cast<uint32_t>(left)] =
                    grid.cells[static_cast<size_t>(r) * grid.columns + c];
            }
        }
        grid = std::move(grown);
        placed.x -= left * gw;
        placed.y -= top * gh;
    }
    doc.engine().updateTilemap(map.map, grid);
    *origin = placed;
}

// Everything every frame holds, moved by `remap`. Each region and each
// geometry is moved once, however many operations name it.
void remapDocument(Document& doc, const Remap& remap, ls::Vec2i newSize) {
    ls::LSContext& engine = doc.engine();
    auto info = engine.getDocumentInfo(doc.id());
    if (info.fail()) {
        return;
    }
    std::set<uint64_t> regions;
    std::set<uint64_t> geometries;
    std::set<uint64_t> texts;
    // A tilemap two linked cels share is moved once; each cel's operation
    // gets the place it now has.
    std::map<uint64_t, ls::Vec2i> tilemaps;
    for (ls::SpriteId sprite : info.value.sprites) {
        // A tileset's tiles are in their own space, not the canvas's.
        if (isTileset(doc, sprite)) {
            continue;
        }
        auto spriteInfo = engine.getSpriteInfo(sprite);
        if (spriteInfo.fail()) {
            continue;
        }
        for (ls::LayerId layer : spriteInfo.value.layers) {
            TilemapLayer map;
            if (readTilemapLayer(doc, layer, &map)) {
                auto moved = tilemaps.find(map.map.value);
                if (moved == tilemaps.end()) {
                    ls::Vec2i origin = map.origin;
                    remapTilemap(doc, map, remap, newSize, &origin);
                    moved = tilemaps.emplace(map.map.value, origin).first;
                }
                engine.setOperationParameter(map.draw, "origin", ls::ParameterValue{ ls::Vec2f{
                    static_cast<float>(moved->second.x), static_cast<float>(moved->second.y) } });
                continue;
            }
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
                        // What was erased from it moves with it.
                        auto erase = engine.getRegionErase(id);
                        if (erase.ok() && erase.value.valid() &&
                            geometries.insert(erase.value.value).second) {
                            remapGeometry(doc, erase.value, remap);
                        }
                    } else if (regions.insert(region).second) {
                        auto set = engine.getRegionIntervals(id);
                        if (set.ok()) {
                            engine.setRegionIntervals(id, remap.pixels(set.value));
                        }
                    }
                    // Text keeps its words, place and size on the region;
                    // they move with it, so retyping lands where it now is.
                    TextSpec text;
                    if (texts.insert(region).second && readTextElement(doc, id, &text)) {
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
                for (const char* name : { "polyline", "path" }) {
                    if (const uint64_t line = handle(doc, op.id, name); line != 0) {
                        ls::GeometryId id;
                        id.value = line;
                        if (geometries.insert(line).second) {
                            remapGeometry(doc, id, remap);
                        }
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
                if (entry.kind == TransformKind::Offset) {
                    const ls::Vec2f zero = remap.point({ 0.f, 0.f });
                    const ls::Vec2f moved = remap.point(entry.delta);
                    setOffsetDelta(doc, entry.id, { moved.x - zero.x, moved.y - zero.y });
                }
                if (entry.kind == TransformKind::Scale && remap.swapsAxes) {
                    setScaleFactor(doc, entry.id, { entry.factor.y, entry.factor.x });
                }
            }
        }
    }

    // Guides go where the canvas takes them: a line down the canvas can come
    // out across it after a quarter turn.
    std::vector<Guide> guides = readGuides(doc);
    if (!guides.empty()) {
        for (Guide& guide : guides) {
            const float at = static_cast<float>(guide.at);
            const ls::Vec2f a = remap.point(guide.vertical ? ls::Vec2f{ at, 0.f } : ls::Vec2f{ 0.f, at });
            const ls::Vec2f b = remap.point(guide.vertical ? ls::Vec2f{ at, 1.f } : ls::Vec2f{ 1.f, at });
            if (std::fabs(a.x - b.x) < 0.001f) {
                guide = { true, static_cast<int32_t>(std::lround(a.x)) };
            } else {
                guide = { false, static_cast<int32_t>(std::lround(a.y)) };
            }
        }
        writeGuides(doc, guides);
    }

    // Slices go where the canvas takes them, corners and all.
    std::vector<Slice> slices = readSlices(doc);
    if (!slices.empty()) {
        const auto box = [&](ls::Rect2i r) {
            ls::Vec2f min, max;
            mappedBox(remap, { static_cast<float>(r.min.x), static_cast<float>(r.min.y) },
                      { static_cast<float>(r.max.x), static_cast<float>(r.max.y) }, &min, &max);
            return ls::Rect2i{ { static_cast<int32_t>(std::lround(min.x)),
                                 static_cast<int32_t>(std::lround(min.y)) },
                               { static_cast<int32_t>(std::lround(max.x)),
                                 static_cast<int32_t>(std::lround(max.y)) } };
        };
        for (Slice& slice : slices) {
            const ls::Rect2i centre { { slice.bounds.min.x + slice.centre.min.x,
                                        slice.bounds.min.y + slice.centre.min.y },
                                      { slice.bounds.min.x + slice.centre.max.x,
                                        slice.bounds.min.y + slice.centre.max.y } };
            const ls::Vec2f pivot = remap.point({ static_cast<float>(slice.bounds.min.x + slice.pivot.x),
                                                  static_cast<float>(slice.bounds.min.y + slice.pivot.y) });
            slice.bounds = box(slice.bounds);
            const ls::Rect2i movedCentre = box(centre);
            slice.centre = { { movedCentre.min.x - slice.bounds.min.x, movedCentre.min.y - slice.bounds.min.y },
                             { movedCentre.max.x - slice.bounds.min.x, movedCentre.max.y - slice.bounds.min.y } };
            slice.pivot = { static_cast<int32_t>(std::lround(pivot.x)) - slice.bounds.min.x,
                            static_cast<int32_t>(std::lround(pivot.y)) - slice.bounds.min.y };
        }
        writeSlices(doc, slices);
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
    if (!sizeAllowed(width, height, error) || !tilemapsFollow(doc, remap, error)) {
        return false;
    }
    doc.beginAction(label);
    remapDocument(doc, remap, { static_cast<int32_t>(width), static_cast<int32_t>(height) });
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
