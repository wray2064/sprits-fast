// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

#include "app/shape.h"

#include "app/palette.h"

#include <algorithm>
#include <cmath>

namespace fast {
namespace {

// A drag gives two corners in either order; geometry wants an origin and a size.
struct Box {
    ls::Vec2f origin;
    float width = 0.f;
    float height = 0.f;
};

Box boxOf(const ShapeParams& params) {
    Box box;
    box.origin = { std::min(params.from.x, params.to.x),
                   std::min(params.from.y, params.to.y) };
    box.width = std::abs(params.to.x - params.from.x);
    box.height = std::abs(params.to.y - params.from.y);

    // A zero-sized shape compiles to nothing and looks like a failed tool. One
    // pixel is the smallest thing a person can have meant.
    box.width = std::max(box.width, 1.f);
    box.height = std::max(box.height, 1.f);
    return box;
}

ls::CurveDesc curveOf(const ShapeParams& params) {
    ls::CurveDesc desc;
    const std::vector<ls::Vec2f>& p = params.points;
    for (size_t i = 0; i + 3 < p.size(); i += 3) {
        desc.segments.push_back({ p[i], p[i + 1], p[i + 2], p[i + 3] });
    }
    desc.closed = params.closed;
    return desc;
}

std::vector<ls::Vec2f> pointsOf(const ls::CurveDesc& desc) {
    std::vector<ls::Vec2f> points;
    for (const ls::CurveDesc::Segment& segment : desc.segments) {
        if (points.empty()) {
            points.push_back(segment.p0);
        }
        points.push_back(segment.cp0);
        points.push_back(segment.cp1);
        points.push_back(segment.p1);
    }
    return points;
}

// The box round a list of points, as the two corners a panel shows.
void boundsOf(const std::vector<ls::Vec2f>& points, ShapeParams* out) {
    if (points.empty()) {
        return;
    }
    ls::Vec2f lo = points.front();
    ls::Vec2f hi = points.front();
    for (const ls::Vec2f& p : points) {
        lo = { std::min(lo.x, p.x), std::min(lo.y, p.y) };
        hi = { std::max(hi.x, p.x), std::max(hi.y, p.y) };
    }
    out->from = lo;
    out->to = hi;
}

bool writeGeometry(Document& doc, ls::GeometryId geometry, ShapeKind kind,
                   const ShapeParams& params) {
    ls::LSContext& engine = doc.engine();
    const Box box = boxOf(params);

    switch (kind) {
        case ShapeKind::Rectangle:
            return engine.updateRect(geometry,
                { box.origin, box.width, box.height, params.cornerRadius }).ok();

        case ShapeKind::Ellipse:
            return engine.updateEllipse(geometry,
                { { box.origin.x + box.width * 0.5f,
                    box.origin.y + box.height * 0.5f },
                  box.width * 0.5f, box.height * 0.5f }).ok();

        case ShapeKind::Line: {
            // A line has no area, so it is stored as the polyline it is and
            // stroked. Its two points are the drag itself rather than a box,
            // since a line from bottom-left to top-right is not the same line as
            // one from top-left to bottom-right.
            ls::PolylineDesc desc;
            desc.points = { params.from, params.to };
            desc.closed = false;
            return engine.updatePolyline(geometry, desc).ok();
        }

        case ShapeKind::Polygon: {
            if (params.points.size() < 3) {
                return false;
            }
            ls::PolygonDesc desc;
            desc.vertices = params.points;
            return engine.updatePolygon(geometry, desc).ok();
        }

        case ShapeKind::Curve:
            if (params.points.size() < 4) {
                return false;
            }
            return engine.updateCurve(geometry, curveOf(params)).ok();
    }
    return false;
}

// The operation index of a layer's outline, or -1.
int32_t outlineIndex(Document& doc, const PaintLayer& layer) {
    auto operations = doc.engine().getLayerOperations(layer.layer);
    if (operations.fail()) {
        return -1;
    }
    for (size_t i = 0; i < operations.value.size(); ++i) {
        if (operations.value[i].type == "GenerateSilhouetteOutlineOp") {
            return static_cast<int32_t>(i);
        }
    }
    return -1;
}

ls::OperationId outlineOperation(Document& doc, const PaintLayer& layer) {
    auto operations = doc.engine().getLayerOperations(layer.layer);
    if (operations.fail()) {
        return ls::OperationId{};
    }
    for (const ls::OperationInfo& op : operations.value) {
        if (op.type == "GenerateSilhouetteOutlineOp") {
            return op.id;
        }
    }
    return ls::OperationId{};
}

} // namespace

ShapeKind shapeKindOf(Document& doc, ls::GeometryId geometry) {
    ls::LSContext& engine = doc.engine();
    if (engine.getRect(geometry).ok()) {
        return ShapeKind::Rectangle;
    }
    if (engine.getEllipse(geometry).ok()) {
        return ShapeKind::Ellipse;
    }
    if (engine.getPolygon(geometry).ok()) {
        return ShapeKind::Polygon;
    }
    if (engine.getCurve(geometry).ok()) {
        return ShapeKind::Curve;
    }
    if (engine.getPolyline(geometry).ok()) {
        return ShapeKind::Line;
    }
    return ShapeKind::Rectangle;
}

const char* shapeKindName(ShapeKind kind) {
    switch (kind) {
        case ShapeKind::Rectangle: return "Rectangle";
        case ShapeKind::Ellipse:   return "Ellipse";
        case ShapeKind::Line:      return "Line";
        case ShapeKind::Polygon:   return "Polygon";
        case ShapeKind::Curve:     return "Curve";
    }
    return "Shape";
}

void mapShapePoints(ShapeParams& params, const std::function<ls::Vec2f(ls::Vec2f)>& map) {
    params.from = map(params.from);
    params.to = map(params.to);
    for (ls::Vec2f& p : params.points) {
        p = map(p);
    }
}

std::vector<ls::Vec2f> shapeHandles(ShapeKind kind, const ShapeParams& params) {
    switch (kind) {
        case ShapeKind::Polygon:
            return params.points;
        case ShapeKind::Curve: {
            std::vector<ls::Vec2f> handles = params.points;
            // A closed curve ends where it began: that anchor is one handle.
            if (params.closed && handles.size() >= 4) {
                handles.pop_back();
            }
            return handles;
        }
        default:
            return { params.from, params.to };
    }
}

bool isControlHandle(ShapeKind kind, size_t index) {
    return kind == ShapeKind::Curve && index % 3 != 0;
}

void moveShapeHandle(ShapeKind kind, ShapeParams& params, size_t index, ls::Vec2f to) {
    if (kind == ShapeKind::Polygon) {
        if (index < params.points.size()) {
            params.points[index] = to;
        }
        return;
    }
    if (kind == ShapeKind::Curve) {
        std::vector<ls::Vec2f>& p = params.points;
        if (index >= p.size()) {
            return;
        }
        if (index % 3 != 0) {
            p[index] = to;          // a control point moves alone
            return;
        }
        const ls::Vec2f by { to.x - p[index].x, to.y - p[index].y };
        const auto nudge = [&](size_t i) {
            if (i < p.size()) {
                p[i] = { p[i].x + by.x, p[i].y + by.y };
            }
        };
        nudge(index);
        if (index > 0) {
            nudge(index - 1);
        }
        nudge(index + 1);
        // The first anchor of a closed curve is its last as well, with the
        // last segment's second control point beside it.
        if (params.closed && index == 0 && p.size() >= 4) {
            nudge(p.size() - 1);
            nudge(p.size() - 2);
        }
        return;
    }
    if (index == 0) {
        params.from = to;
    } else if (index == 1) {
        params.to = to;
    }
}

std::vector<ls::Vec2f> curveThrough(const std::vector<ls::Vec2f>& anchors,
                                    const std::vector<ls::Vec2f>& handles, bool closed) {
    std::vector<ls::Vec2f> points;
    if (anchors.empty()) {
        return points;
    }
    const auto handle = [&](size_t i) {
        return i < handles.size() ? handles[i] : ls::Vec2f{ 0.f, 0.f };
    };
    const size_t n = anchors.size();
    const size_t segments = closed ? n : n - 1;
    points.push_back(anchors.front());
    for (size_t s = 0; s < segments; ++s) {
        const size_t a = s;
        const size_t b = (s + 1) % n;
        const ls::Vec2f out = handle(a);
        const ls::Vec2f in = handle(b);
        points.push_back({ anchors[a].x + out.x, anchors[a].y + out.y });
        points.push_back({ anchors[b].x - in.x, anchors[b].y - in.y });
        points.push_back(anchors[b]);
    }
    return points;
}

bool addShapeTo(Document& doc, ls::LayerId layer, ShapeKind kind,
                const ShapeParams& params, ls::Color colour, ls::ColorRole role,
                ShapeLayer* out) {
    if (out == nullptr || !layer.valid()) {
        return false;
    }
    ls::LSContext& engine = doc.engine();
    const Box box = boxOf(params);

    doc.beginAction(std::string("Draw ") + shapeKindName(kind));

    ls::Result<ls::GeometryId> geometry = ls::Result<ls::GeometryId>::err(
        ls::LSError::InvalidParameter);
    switch (kind) {
        case ShapeKind::Rectangle:
            geometry = engine.createRect(doc.id(),
                { box.origin, box.width, box.height, params.cornerRadius });
            break;
        case ShapeKind::Ellipse:
            geometry = engine.createEllipse(doc.id(),
                { { box.origin.x + box.width * 0.5f,
                    box.origin.y + box.height * 0.5f },
                  box.width * 0.5f, box.height * 0.5f });
            break;
        case ShapeKind::Line: {
            ls::PolylineDesc desc;
            desc.points = { params.from, params.to };
            desc.closed = false;
            geometry = engine.createPolyline(doc.id(), desc);
            break;
        }
        case ShapeKind::Polygon: {
            if (params.points.size() >= 3) {
                ls::PolygonDesc desc;
                desc.vertices = params.points;
                geometry = engine.createPolygon(doc.id(), desc);
            }
            break;
        }
        case ShapeKind::Curve:
            if (params.points.size() >= 4) {
                geometry = engine.createCurve(doc.id(), curveOf(params));
            }
            break;
    }
    if (geometry.fail()) {
        doc.abandonAction();
        return false;
    }

    // A curve is a line too, drawn a pixel at a time along its path: the
    // stroke that keeps a one-pixel curve one pixel wide.
    if (kind == ShapeKind::Curve) {
        ls::StrokePixelPathOp stroke;
        stroke.path = geometry.value;
        stroke.fallbackColor = colour;
        stroke.paletteRole = role;
        auto op = engine.addOperation(layer, stroke);
        if (op.fail()) {
            doc.abandonAction();
            return false;
        }
        keepEffectsLast(doc, layer);
        doc.endAction();
        out->paint.layer = layer;
        out->paint.fill = op.value;
        out->paint.region = ls::RegionId{};
        out->geometry = geometry.value;
        out->kind = kind;
        return true;
    }

    // A line is stroked rather than filled: it encloses no area, so a fill would
    // resolve to nothing. Everything else fills the region its geometry makes.
    if (kind == ShapeKind::Line) {
        ls::StrokePolylineOp stroke;
        stroke.polyline = geometry.value;
        stroke.width = params.thickness;
        stroke.fallbackColor = colour;
        stroke.paletteRole = role;
        stroke.snap = ls::SnapPolicy::HalfGrid;   // keeps a 1px line on one row

        auto op = engine.addOperation(layer, stroke);
        if (op.fail()) {
            doc.abandonAction();
            return false;
        }
        out->paint.layer = layer;
        out->paint.fill = op.value;
        out->paint.region = ls::RegionId{};    // a stroke names no region
    } else {
        auto region = engine.createRegionFromGeometry(geometry.value);
        if (region.fail()) {
            doc.abandonAction();
            return false;
        }
        // Its area, or -- outlined -- its edge, thickened inward.
        ls::Result<ls::OperationId> op = ls::Result<ls::OperationId>::err(
            ls::LSError::InvalidParameter);
        if (params.outline) {
            ls::StrokeRegionBoundaryOp edge;
            edge.targetRegion = region.value;
            edge.width = std::max(1.f, params.thickness);
            edge.fallbackColor = colour;
            edge.paletteRole = role;
            op = engine.addOperation(layer, edge);
        } else {
            ls::FillSolidOp fill;
            fill.targetRegion = region.value;
            fill.fallbackColor = colour;
            fill.paletteRole = role;
            op = engine.addOperation(layer, fill);
        }
        if (op.fail()) {
            doc.abandonAction();
            return false;
        }
        out->paint.layer = layer;
        out->paint.region = region.value;
        out->paint.fill = op.value;
    }

    // A new shape is drawn before any outline or shadow, so they see it.
    keepEffectsLast(doc, layer);
    doc.endAction();

    out->geometry = geometry.value;
    out->kind = kind;
    return true;
}

bool shapeIsOutlined(Document& doc, ls::OperationId op, float* width) {
    auto info = doc.engine().getOperationInfo(op);
    if (info.fail() || info.value.type != "StrokeRegionBoundaryOp") {
        return false;
    }
    if (width != nullptr) {
        auto value = doc.engine().getOperationParameter(op, "width");
        const float* found = value.ok() ? std::get_if<float>(&value.value) : nullptr;
        *width = found != nullptr ? *found : 1.f;
    }
    return true;
}

bool setShapeOutlined(Document& doc, ls::LayerId layer, ls::OperationId* op, bool outlined,
                      float width) {
    if (op == nullptr) {
        return false;
    }
    ls::LSContext& engine = doc.engine();
    float was = 1.f;
    const bool isOutlined = shapeIsOutlined(doc, *op, &was);
    if (isOutlined == outlined) {
        // Already so: only the width, if it is an outline.
        return !outlined ||
               engine.setOperationParameter(*op, "width",
                                            ls::ParameterValue{ std::max(1.f, width) }).ok();
    }
    auto operation = engine.getOperation(*op);
    auto operations = engine.getLayerOperations(layer);
    if (operation.fail() || operations.fail()) {
        return false;
    }
    int32_t at = -1;
    for (size_t i = 0; i < operations.value.size(); ++i) {
        if (operations.value[i].id == *op) { at = static_cast<int32_t>(i); }
    }
    if (at < 0) {
        return false;
    }
    // The same region, colour and slot, drawn the other way.
    ls::Operation swapped;
    if (outlined) {
        const auto* fill = std::get_if<ls::FillSolidOp>(&operation.value);
        if (fill == nullptr) {
            return false;
        }
        ls::StrokeRegionBoundaryOp edge;
        edge.targetRegion = fill->targetRegion;
        edge.width = std::max(1.f, width);
        edge.fallbackColor = fill->fallbackColor;
        edge.paletteRole = fill->paletteRole;
        edge.blend = fill->blend;
        edge.opacity = fill->opacity;
        swapped = edge;
    } else {
        const auto* edge = std::get_if<ls::StrokeRegionBoundaryOp>(&operation.value);
        if (edge == nullptr) {
            return false;
        }
        ls::FillSolidOp fill;
        fill.targetRegion = edge->targetRegion;
        fill.fallbackColor = edge->fallbackColor;
        fill.paletteRole = edge->paletteRole;
        fill.blend = edge->blend;
        fill.opacity = edge->opacity;
        swapped = fill;
    }
    auto added = engine.addOperation(layer, swapped, at);
    if (added.fail()) {
        return false;
    }
    engine.removeOperation(layer, *op);
    *op = added.value;
    return true;
}

bool createShapeLayer(Document& doc, ls::SpriteId sprite, ShapeKind kind,
                      const ShapeParams& params, ls::Color colour, ShapeLayer* out) {
    if (out == nullptr) {
        return false;
    }
    doc.beginAction(std::string("Draw ") + shapeKindName(kind));
    auto layer = doc.engine().createLayer(sprite, {shapeKindName(kind)});
    if (layer.fail() ||
        !addShapeTo(doc, layer.value, kind, params, colour, ls::kColorRoleNone, out)) {
        doc.abandonAction();
        return false;
    }
    doc.endAction();
    return true;
}

bool updateShape(Document& doc, const ShapeLayer& shape, const ShapeParams& params) {
    if (!shape.valid()) {
        return false;
    }
    // Only the geometry is touched. The region tracks it, the fill is on the
    // region, and an outline is generated from whatever the layer draws -- so
    // all of them follow without being told.
    return writeGeometry(doc, shape.geometry, shape.kind, params);
}

bool readShapeParams(Document& doc, const ShapeLayer& shape, ShapeParams* out) {
    if (out == nullptr || !shape.valid()) {
        return false;
    }
    ls::LSContext& engine = doc.engine();

    if (shape.kind == ShapeKind::Polygon) {
        auto polygon = engine.getPolygon(shape.geometry);
        if (polygon.ok()) {
            out->points = polygon.value.vertices;
            out->closed = true;
            boundsOf(out->points, out);
            return true;
        }
    }
    if (shape.kind == ShapeKind::Curve) {
        auto curve = engine.getCurve(shape.geometry);
        if (curve.ok()) {
            out->points = pointsOf(curve.value);
            out->closed = curve.value.closed;
            boundsOf(out->points, out);
            return true;
        }
    }

    if (shape.kind == ShapeKind::Line) {
        auto path = engine.getGeometryPath(shape.geometry);
        if (path.fail() || path.value.size() < 2) {
            return false;
        }
        out->from = path.value.front();
        out->to = path.value.back();
        return true;
    }

    // The description itself, not its rasterised bounds: written back
    // unchanged it must be the same shape, corner radius included -- a
    // rounded rectangle carried along by a selection or turned with the
    // canvas used to come back square.
    if (shape.kind == ShapeKind::Rectangle) {
        auto rect = engine.getRect(shape.geometry);
        if (rect.ok()) {
            out->from = rect.value.origin;
            out->to = { rect.value.origin.x + rect.value.width,
                        rect.value.origin.y + rect.value.height };
            out->cornerRadius = rect.value.cornerRadius;
            return true;
        }
    } else if (shape.kind == ShapeKind::Ellipse) {
        auto oval = engine.getEllipse(shape.geometry);
        if (oval.ok()) {
            out->from = { oval.value.center.x - oval.value.radiusX,
                          oval.value.center.y - oval.value.radiusY };
            out->to = { oval.value.center.x + oval.value.radiusX,
                        oval.value.center.y + oval.value.radiusY };
            return true;
        }
    }

    // A geometry of some other kind than the element claims: its bounds are
    // the best description there is.
    auto bounds = engine.getGeometryBounds(shape.geometry);
    if (bounds.fail()) {
        return false;
    }
    out->from = { bounds.value.floatBounds.min.x, bounds.value.floatBounds.min.y };
    out->to = { bounds.value.floatBounds.max.x, bounds.value.floatBounds.max.y };
    return true;
}

bool shapeOfLayer(Document& doc, const PaintLayer& layer, ShapeLayer* out) {
    if (out == nullptr || !layer.valid()) {
        return false;
    }
    ls::LSContext& engine = doc.engine();

    // A curve's stroke names its curve directly, as a line's names its
    // polyline.
    auto info = engine.getOperationInfo(layer.fill);
    if (info.ok() && info.value.type == "StrokePixelPathOp") {
        auto path = engine.getOperationParameter(layer.fill, "path");
        const uint64_t* handle = path.ok() ? std::get_if<uint64_t>(&path.value) : nullptr;
        if (handle == nullptr || *handle == 0) {
            return false;
        }
        ls::GeometryId geometry;
        geometry.value = *handle;
        if (engine.getCurve(geometry).fail()) {
            return false;
        }
        out->paint = layer;
        out->geometry = geometry;
        out->kind = ShapeKind::Curve;
        return true;
    }
    if (info.ok() && info.value.type == "StrokePolylineOp") {
        auto polyline = engine.getOperationParameter(layer.fill, "polyline");
        if (polyline.ok()) {
            if (const uint64_t* handle = std::get_if<uint64_t>(&polyline.value)) {
                if (*handle != 0) {
                    out->paint = layer;
                    out->geometry.value = *handle;
                    out->kind = ShapeKind::Line;
                    return true;
                }
            }
        }
        return false;
    }

    if (!layer.region.valid()) {
        return false;
    }
    auto source = engine.getRegionSourceGeometry(layer.region);
    if (source.fail() || !source.value.valid()) {
        return false;       // authored pixels, or a shape edited by hand
    }

    // Which kind it is comes from the geometry's own path: an ellipse is a many
    // sided closed loop, a rectangle is four or eight corners. Asking the
    // geometry beats recording the kind separately, which could disagree.
    out->paint = layer;
    out->geometry = source.value;
    out->kind = shapeKindOf(doc, source.value);
    return true;
}

// ------------------------------------------------------------------ effects --

void keepEffectsLast(Document& doc, ls::LayerId layer) {
    auto operations = doc.engine().getLayerOperations(layer);
    if (operations.fail()) {
        return;
    }
    std::vector<ls::OperationId> rest;
    std::vector<ls::OperationId> transforms;
    std::vector<ls::OperationId> outlines;
    std::vector<ls::OperationId> shadows;
    for (const ls::OperationInfo& op : operations.value) {
        ls::Operation made;
        if (op.type == "GenerateSilhouetteOutlineOp") {
            outlines.push_back(op.id);
        } else if (op.type == "GenerateDropShadowOp") {
            shadows.push_back(op.id);
        } else if (ls::makeOperationOfType(op.type, made) && ls::operationIsTransform(made)) {
            transforms.push_back(op.id);
        } else {
            rest.push_back(op.id);
        }
    }
    std::vector<ls::OperationId> order = rest;
    order.insert(order.end(), transforms.begin(), transforms.end());
    order.insert(order.end(), outlines.begin(), outlines.end());
    order.insert(order.end(), shadows.begin(), shadows.end());
    bool same = order.size() == operations.value.size();
    for (size_t i = 0; same && i < order.size(); ++i) {
        same = order[i] == operations.value[i].id;
    }
    if (!same) {
        doc.engine().reorderOperations(layer, order);
    }
}

namespace {

ls::OperationId shadowOperation(Document& doc, const PaintLayer& layer) {
    auto operations = doc.engine().getLayerOperations(layer.layer);
    if (operations.fail()) {
        return ls::OperationId{};
    }
    for (const ls::OperationInfo& op : operations.value) {
        if (op.type == "GenerateDropShadowOp") {
            return op.id;
        }
    }
    return ls::OperationId{};
}

} // namespace

bool hasShadow(Document& doc, const PaintLayer& layer) {
    return shadowOperation(doc, layer).valid();
}

bool setShadow(Document& doc, const PaintLayer& layer, const ShadowSettings& settings) {
    if (!layer.valid()) {
        return false;
    }
    ls::LSContext& engine = doc.engine();
    ls::SpriteId sprite;
    if (settings.scope == OutlineScope::Sprite) {
        auto info = engine.getLayerInfo(layer.layer);
        if (info.fail() || !info.value.sprite.valid()) {
            return false;
        }
        sprite = info.value.sprite;
    }
    const float dx = static_cast<float>(std::clamp(settings.dx, -kMaxShadowOffset, kMaxShadowOffset));
    const float dy = static_cast<float>(std::clamp(settings.dy, -kMaxShadowOffset, kMaxShadowOffset));
    const float opacity = std::clamp(settings.opacity, 0.f, 1.f);
    const ls::OperationId existing = shadowOperation(doc, layer);
    if (existing.valid()) {
        bool ok = true;
        ok = engine.setOperationParameter(existing, "targetSprite",
                 ls::ParameterValue{ static_cast<uint64_t>(sprite.value) }).ok() && ok;
        ok = engine.setOperationParameter(existing, "offset",
                 ls::ParameterValue{ ls::Vec2f{ dx, dy } }).ok() && ok;
        ok = engine.setOperationParameter(existing, "fallbackColor",
                 ls::ParameterValue{ settings.colour }).ok() && ok;
        ok = engine.setOperationParameter(existing, "paletteRole",
                 ls::ParameterValue{ static_cast<int64_t>(settings.role) }).ok() && ok;
        ok = engine.setOperationParameter(existing, "opacity",
                 ls::ParameterValue{ opacity }).ok() && ok;
        return ok;
    }
    ls::GenerateDropShadowOp shadow;
    shadow.targetSprite = sprite;
    shadow.offset = { dx, dy };
    shadow.fallbackColor = settings.colour;
    shadow.paletteRole = settings.role;
    shadow.opacity = opacity;
    const bool added = engine.addOperation(layer.layer, shadow).ok();
    keepEffectsLast(doc, layer.layer);
    return added;
}

bool removeShadow(Document& doc, const PaintLayer& layer) {
    const ls::OperationId op = shadowOperation(doc, layer);
    return op.valid() && doc.engine().removeOperation(layer.layer, op).ok();
}

ShadowSettings shadowOf(Document& doc, const PaintLayer& layer) {
    ShadowSettings settings;
    const ls::OperationId op = shadowOperation(doc, layer);
    if (!op.valid()) {
        return settings;
    }
    auto operation = doc.engine().getOperation(op);
    const auto* shadow = operation.ok() ? std::get_if<ls::GenerateDropShadowOp>(&operation.value)
                                        : nullptr;
    if (shadow == nullptr) {
        return settings;
    }
    settings.scope = shadow->targetSprite.valid() ? OutlineScope::Sprite : OutlineScope::Layer;
    settings.dx = static_cast<int>(std::lround(shadow->offset.x));
    settings.dy = static_cast<int>(std::lround(shadow->offset.y));
    settings.colour = shadow->fallbackColor;
    settings.role = shadow->paletteRole;
    settings.opacity = shadow->opacity;
    return settings;
}

// ------------------------------------------------------------------ outline --

bool hasOutline(Document& doc, const PaintLayer& layer) {
    return outlineIndex(doc, layer) >= 0;
}

bool setOutline(Document& doc, const PaintLayer& layer, const OutlineSettings& settings) {
    if (!layer.valid()) {
        return false;
    }
    ls::LSContext& engine = doc.engine();

    // Naming the sprite is what makes the engine trace the whole figure rather
    // than this layer. The layer knows which sprite it is in, so the caller
    // does not have to carry it about.
    ls::SpriteId sprite;
    if (settings.scope == OutlineScope::Sprite) {
        auto info = engine.getLayerInfo(layer.layer);
        if (info.fail() || !info.value.sprite.valid()) {
            return false;
        }
        sprite = info.value.sprite;
    }

    const float thickness = static_cast<float>(
        std::min(std::max(settings.thickness, 1), kMaxOutlineThickness));

    const ls::OperationId existing = outlineOperation(doc, layer);
    if (existing.valid()) {
        // Driven rather than rebuilt: the operation stays the same one, so a
        // slider drag leaves one entry in the layer and one in the history.
        bool ok = true;
        ok = engine.setOperationParameter(existing, "targetSprite",
                 ls::ParameterValue{ static_cast<uint64_t>(sprite.value) }).ok() && ok;
        ok = engine.setOperationParameter(existing, "thickness",
                 ls::ParameterValue{ thickness }).ok() && ok;
        ok = engine.setOperationParameter(existing, "side",
                 ls::ParameterValue{ static_cast<int64_t>(settings.side) }).ok() && ok;
        ok = engine.setOperationParameter(existing, "fallbackColor",
                 ls::ParameterValue{ settings.colour }).ok() && ok;
        ok = engine.setOperationParameter(existing, "paletteRole",
                 ls::ParameterValue{ static_cast<int64_t>(settings.role) }).ok() && ok;
        return ok;
    }

    // A silhouette outline rather than a region outline: it follows whatever has
    // been drawn by the time it resolves, so it works on a shape and on freehand
    // pixels alike and keeps following as either changes. A region outline would
    // be pinned to one region and could not do the second.
    ls::GenerateSilhouetteOutlineOp outline;
    outline.targetSprite = sprite;
    outline.thickness = thickness;
    outline.side = settings.side;
    outline.fallbackColor = settings.colour;
    outline.paletteRole = settings.role;
    const bool added = engine.addOperation(layer.layer, outline).ok();
    keepEffectsLast(doc, layer.layer);
    return added;
}

bool removeOutline(Document& doc, const PaintLayer& layer) {
    const ls::OperationId op = outlineOperation(doc, layer);
    if (!op.valid()) {
        return false;
    }
    return doc.engine().removeOperation(layer.layer, op).ok();
}

OutlineSettings outlineOf(Document& doc, const PaintLayer& layer) {
    OutlineSettings settings;
    const ls::OperationId op = outlineOperation(doc, layer);
    if (!op.valid()) {
        return settings;
    }
    ls::LSContext& engine = doc.engine();

    auto sprite = engine.getOperationParameter(op, "targetSprite");
    if (sprite.ok()) {
        if (const uint64_t* handle = std::get_if<uint64_t>(&sprite.value)) {
            settings.scope = *handle != 0 ? OutlineScope::Sprite : OutlineScope::Layer;
        }
    }
    auto thickness = engine.getOperationParameter(op, "thickness");
    if (thickness.ok()) {
        if (const float* found = std::get_if<float>(&thickness.value)) {
            settings.thickness = static_cast<int>(*found + 0.5f);
        }
    }
    auto side = engine.getOperationParameter(op, "side");
    if (side.ok()) {
        if (const int64_t* found = std::get_if<int64_t>(&side.value)) {
            settings.side = static_cast<ls::OutlineSide>(*found);
        }
    }
    auto colour = engine.getOperationParameter(op, "fallbackColor");
    if (colour.ok()) {
        if (const ls::Color* found = std::get_if<ls::Color>(&colour.value)) {
            settings.colour = *found;
        }
    }
    auto role = engine.getOperationParameter(op, "paletteRole");
    if (role.ok()) {
        if (const int64_t* found = std::get_if<int64_t>(&role.value)) {
            settings.role = static_cast<ls::ColorRole>(*found);
        }
    }
    // The colour reported is the one drawn: the slot's, when the outline names
    // a slot the palette has. The literal stays on the operation as the
    // fallback, but a panel showing it would disagree with the canvas after
    // every palette change.
    resolvePaletteRole(doc, paletteOfLayer(doc, layer.layer), settings.role,
                       &settings.colour);
    return settings;
}

} // namespace fast
