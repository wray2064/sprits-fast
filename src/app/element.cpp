// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

#include "app/element.h"

#include "app/palette.h"

namespace fast {
namespace {

uint64_t handleParam(Document& doc, ls::OperationId op, const char* name) {
    auto value = doc.engine().getOperationParameter(op, name);
    if (value.fail()) {
        return 0;
    }
    if (const uint64_t* found = std::get_if<uint64_t>(&value.value)) {
        return *found;
    }
    return 0;
}

// Whether an operation draws something a person made: a fill over a region,
// or a stroke along a polyline. Outlines, transforms and the rest are rules
// about the elements, not elements.
bool isElementOp(const ls::OperationInfo& op) {
    return op.type == "FillSolidOp" || op.type == "FillDitherOp" ||
           op.type == "StrokePolylineOp";
}

} // namespace

const char* elementKindName(ElementKind kind) {
    switch (kind) {
        case ElementKind::Paint:     return "Pixels";
        case ElementKind::Rectangle: return "Rectangle";
        case ElementKind::Ellipse:   return "Ellipse";
        case ElementKind::Line:      return "Line";
    }
    return "Element";
}

std::vector<Element> elementsOf(Document& doc, ls::LayerId layer) {
    std::vector<Element> out;
    ls::LSContext& engine = doc.engine();
    auto operations = engine.getLayerOperations(layer);
    if (operations.fail()) {
        return out;
    }
    for (const ls::OperationInfo& op : operations.value) {
        if (!isElementOp(op)) {
            continue;
        }
        Element element;
        element.fill = op.id;
        if (op.type == "StrokePolylineOp") {
            element.geometry.value = handleParam(doc, op.id, "polyline");
            element.kind = ElementKind::Line;
            out.push_back(element);
            continue;
        }
        element.region.value = handleParam(doc, op.id, "targetRegion");
        if (!element.region.valid()) {
            continue;
        }
        // A region still tied to the geometry it was built from is a shape;
        // one that is not -- authored, or a shape edited by hand -- is pixels.
        auto source = engine.getRegionSourceGeometry(element.region);
        if (source.ok() && source.value.valid()) {
            element.geometry = source.value;
            auto path = engine.getGeometryPath(source.value);
            element.kind = (path.ok() && path.value.size() > 12) ? ElementKind::Ellipse
                                                                  : ElementKind::Rectangle;
        } else {
            element.kind = ElementKind::Paint;
        }
        out.push_back(element);
    }
    return out;
}

ShapeLayer shapeOfElement(ls::LayerId layer, const Element& element) {
    ShapeLayer shape;
    shape.paint.layer = layer;
    shape.paint.region = element.region;
    shape.paint.fill = element.fill;
    shape.geometry = element.geometry;
    shape.kind = element.kind == ElementKind::Ellipse ? ShapeKind::Ellipse
               : element.kind == ElementKind::Line    ? ShapeKind::Line
                                                      : ShapeKind::Rectangle;
    return shape;
}

namespace {

// What a new element of this layer should be coloured: the first element's
// colour and role, so the layer stays one colour to the palette.
void colourOf(Document& doc, ls::LayerId layer, ls::Color* colour, ls::ColorRole* role) {
    *colour = ls::Color{ 200, 200, 200, 255 };
    *role = ls::kColorRoleNone;
    const std::vector<Element> elements = elementsOf(doc, layer);
    if (elements.empty()) {
        return;
    }
    PaintLayer first;
    first.layer = layer;
    first.fill = elements.front().fill;
    first.region = elements.front().region;
    *colour = paintColor(doc, first);
    *role = layerRole(doc, first);
}

} // namespace

bool addShapeElement(Document& doc, ls::LayerId layer, ShapeKind kind,
                     const ShapeParams& params, ShapeLayer* out) {
    ls::Color colour;
    ls::ColorRole role;
    colourOf(doc, layer, &colour, &role);
    return addShapeTo(doc, layer, kind, params, colour, role, out);
}

bool addShapeElement(Document& doc, ls::LayerId layer, ShapeKind kind,
                     const ShapeParams& params, const Ink& ink, ShapeLayer* out) {
    return addShapeTo(doc, layer, kind, params, ink.colour, ink.role, out);
}

bool ensurePaintElement(Document& doc, PaintLayer& layer) {
    if (!layer.layer.valid()) {
        return false;
    }
    for (const Element& element : elementsOf(doc, layer.layer)) {
        if (element.kind == ElementKind::Paint) {
            layer.region = element.region;
            layer.fill = element.fill;
            return true;
        }
    }
    ls::Color colour;
    ls::ColorRole role;
    colourOf(doc, layer.layer, &colour, &role);

    doc.beginAction("Pixels on a shape layer");
    auto region = doc.engine().createRegionFromIntervals(doc.id(), ls::IntervalSet{});
    if (region.fail()) {
        doc.abandonAction();
        return false;
    }
    ls::FillSolidOp fill;
    fill.targetRegion = region.value;
    fill.fallbackColor = colour;
    fill.paletteRole = role;
    auto op = doc.engine().addOperation(layer.layer, fill);
    if (op.fail()) {
        doc.abandonAction();
        return false;
    }
    doc.endAction();
    layer.region = region.value;
    layer.fill = op.value;
    return true;
}

bool removeElement(Document& doc, ls::LayerId layer, const Element& element) {
    if (!element.valid() || elementsOf(doc, layer).size() <= 1) {
        return false;
    }
    doc.beginAction(std::string("Remove ") + elementKindName(element.kind));
    if (doc.engine().removeOperation(layer, element.fill).fail()) {
        doc.abandonAction();
        return false;
    }
    // The drawing it named goes with it. Nothing else can be pointing at a
    // region or geometry an element owned: the tools make one per element.
    if (element.region.valid()) {
        doc.engine().deleteRegion(element.region);
    }
    if (element.geometry.valid()) {
        doc.engine().deleteGeometry(element.geometry);
    }
    doc.endAction();
    return true;
}

bool setElementsColor(Document& doc, ls::LayerId layer, ls::Color colour) {
    bool ok = true;
    for (const Element& element : elementsOf(doc, layer)) {
        // A dithered element's colours live on its ramp, not the operation.
        auto info = doc.engine().getOperationInfo(element.fill);
        if (info.ok() && info.value.type == "FillDitherOp") {
            continue;
        }
        ok = doc.engine().setOperationParameter(element.fill, "fallbackColor",
                                                ls::ParameterValue{colour}).ok() && ok;
    }
    return ok;
}

bool setElementsRole(Document& doc, ls::LayerId layer, ls::ColorRole role) {
    bool ok = true;
    for (const Element& element : elementsOf(doc, layer)) {
        ok = doc.engine().setOperationParameter(element.fill, "paletteRole",
                 ls::ParameterValue{static_cast<int64_t>(role)}).ok() && ok;
    }
    return ok;
}

} // namespace fast
