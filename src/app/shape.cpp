// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

#include "app/shape.h"

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

const char* shapeKindName(ShapeKind kind) {
    switch (kind) {
        case ShapeKind::Rectangle: return "Rectangle";
        case ShapeKind::Ellipse:   return "Ellipse";
        case ShapeKind::Line:      return "Line";
    }
    return "Shape";
}

bool createShapeLayer(Document& doc, ls::SpriteId sprite, ShapeKind kind,
                      const ShapeParams& params, ls::Color colour, ShapeLayer* out) {
    if (out == nullptr) {
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
    }
    if (geometry.fail()) {
        doc.abandonAction();
        return false;
    }

    auto layer = engine.createLayer(sprite, {shapeKindName(kind)});
    if (layer.fail()) {
        doc.abandonAction();
        return false;
    }

    // A line is stroked rather than filled: it encloses no area, so a fill would
    // resolve to nothing. Everything else fills the region its geometry makes.
    if (kind == ShapeKind::Line) {
        ls::StrokePolylineOp stroke;
        stroke.polyline = geometry.value;
        stroke.width = params.thickness;
        stroke.fallbackColor = colour;
        stroke.snap = ls::SnapPolicy::HalfGrid;   // keeps a 1px line on one row

        auto op = engine.addOperation(layer.value, stroke);
        if (op.fail()) {
            doc.abandonAction();
            return false;
        }
        out->paint.layer = layer.value;
        out->paint.fill = op.value;
        out->paint.region = ls::RegionId{};    // a stroke names no region
    } else {
        auto region = engine.createRegionFromGeometry(geometry.value);
        if (region.fail()) {
            doc.abandonAction();
            return false;
        }
        ls::FillSolidOp fill;
        fill.targetRegion = region.value;
        fill.fallbackColor = colour;

        auto op = engine.addOperation(layer.value, fill);
        if (op.fail()) {
            doc.abandonAction();
            return false;
        }
        out->paint.layer = layer.value;
        out->paint.region = region.value;
        out->paint.fill = op.value;
    }

    doc.endAction();

    out->geometry = geometry.value;
    out->kind = kind;
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

    if (shape.kind == ShapeKind::Line) {
        auto path = engine.getGeometryPath(shape.geometry);
        if (path.fail() || path.value.size() < 2) {
            return false;
        }
        out->from = path.value.front();
        out->to = path.value.back();
        return true;
    }

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

    // A stroked line names a polyline directly rather than a region.
    auto info = engine.getOperationInfo(layer.fill);
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
    auto path = engine.getGeometryPath(source.value);
    out->paint = layer;
    out->geometry = source.value;
    out->kind = (path.ok() && path.value.size() > 12) ? ShapeKind::Ellipse
                                                      : ShapeKind::Rectangle;
    return true;
}

// ------------------------------------------------------------------ outline --

bool hasOutline(Document& doc, const PaintLayer& layer) {
    return outlineIndex(doc, layer) >= 0;
}

bool addOutline(Document& doc, const PaintLayer& layer, ls::Color colour,
                int thickness) {
    if (!layer.valid() || hasOutline(doc, layer)) {
        return false;
    }
    // A silhouette outline rather than a region outline: it follows whatever the
    // layer has drawn by the time it resolves, so it works on a shape and on
    // freehand pixels alike, and keeps following as either changes. A region
    // outline would be pinned to one region and could not do the second.
    ls::GenerateSilhouetteOutlineOp outline;
    outline.thickness = static_cast<float>(std::max(1, thickness));
    outline.side = ls::OutlineSide::Outside;
    outline.fallbackColor = colour;
    return doc.engine().addOperation(layer.layer, outline).ok();
}

bool removeOutline(Document& doc, const PaintLayer& layer) {
    const ls::OperationId op = outlineOperation(doc, layer);
    if (!op.valid()) {
        return false;
    }
    return doc.engine().removeOperation(layer.layer, op).ok();
}

bool setOutlineColor(Document& doc, const PaintLayer& layer, ls::Color colour) {
    const ls::OperationId op = outlineOperation(doc, layer);
    if (!op.valid()) {
        return false;
    }
    return doc.engine()
        .setOperationParameter(op, "fallbackColor", ls::ParameterValue{colour}).ok();
}

bool setOutlineThickness(Document& doc, const PaintLayer& layer, int thickness) {
    const ls::OperationId op = outlineOperation(doc, layer);
    if (!op.valid()) {
        return false;
    }
    return doc.engine()
        .setOperationParameter(op, "thickness",
            ls::ParameterValue{static_cast<float>(std::max(1, thickness))}).ok();
}

ls::Color outlineColor(Document& doc, const PaintLayer& layer) {
    const ls::OperationId op = outlineOperation(doc, layer);
    if (!op.valid()) {
        return ls::Color{0, 0, 0, 0};
    }
    auto value = doc.engine().getOperationParameter(op, "fallbackColor");
    if (value.ok()) {
        if (const ls::Color* found = std::get_if<ls::Color>(&value.value)) {
            return *found;
        }
    }
    return ls::Color{0, 0, 0, 0};
}

int outlineThickness(Document& doc, const PaintLayer& layer) {
    const ls::OperationId op = outlineOperation(doc, layer);
    if (!op.valid()) {
        return 0;
    }
    auto value = doc.engine().getOperationParameter(op, "thickness");
    if (value.ok()) {
        if (const float* found = std::get_if<float>(&value.value)) {
            return static_cast<int>(*found + 0.5f);
        }
    }
    return 1;
}

} // namespace fast
