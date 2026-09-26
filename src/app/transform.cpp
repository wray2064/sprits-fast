// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

#include "app/transform.h"

#include "app/element.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace fast {
namespace {

// Reads a parameter by the same name the save file uses, since both walk the
// engine's one field table.
float floatParam(Document& doc, ls::OperationId op, const char* name, float fallback) {
    auto value = doc.engine().getOperationParameter(op, name);
    if (value.fail()) {
        return fallback;
    }
    if (const float* found = std::get_if<float>(&value.value)) {
        return *found;
    }
    return fallback;
}

ls::Vec2f vec2Param(Document& doc, ls::OperationId op, const char* name, ls::Vec2f fallback) {
    auto value = doc.engine().getOperationParameter(op, name);
    if (value.fail()) {
        return fallback;
    }
    if (const ls::Vec2f* found = std::get_if<ls::Vec2f>(&value.value)) {
        return *found;
    }
    return fallback;
}

int64_t intParam(Document& doc, ls::OperationId op, const char* name, int64_t fallback) {
    auto value = doc.engine().getOperationParameter(op, name);
    if (value.fail()) {
        return fallback;
    }
    if (const int64_t* found = std::get_if<int64_t>(&value.value)) {
        return *found;
    }
    return fallback;
}

} // namespace

std::string TransformEntry::label() const {
    char buffer[64];
    switch (kind) {
        case TransformKind::Rotate:
            std::snprintf(buffer, sizeof(buffer), "Rotate  %.1f deg",
                          static_cast<double>(angleDegrees));
            break;
        case TransformKind::Scale:
            std::snprintf(buffer, sizeof(buffer), "Scale  %.2f x %.2f",
                          static_cast<double>(factor.x), static_cast<double>(factor.y));
            break;
        case TransformKind::Mirror:
            std::snprintf(buffer, sizeof(buffer), "Mirror  %s",
                          axis == ls::MirrorAxis::X ? "horizontal"
                        : axis == ls::MirrorAxis::Y ? "vertical" : "both");
            break;
        case TransformKind::Offset:
            std::snprintf(buffer, sizeof(buffer), "Offset  %.0f, %.0f",
                          static_cast<double>(delta.x), static_cast<double>(delta.y));
            break;
    }
    return std::string(buffer);
}

// ------------------------------------------------------------------ adding --

ls::OperationId addRotate(Document& doc, ls::LayerId layer, float degrees, ls::Vec2f pivot,
                          ls::SamplingPolicy sampling) {
    ls::RotateOp op;
    op.targetLayer = layer;
    op.angleDegrees = degrees;
    op.pivotFallback = pivot;
    op.sampling = sampling;
    auto added = doc.engine().addOperation(layer, op);
    return added.ok() ? added.value : ls::OperationId{};
}

ls::OperationId addScale(Document& doc, ls::LayerId layer, ls::Vec2f factor, ls::Vec2f pivot,
                         ls::SamplingPolicy sampling) {
    ls::ScaleOp op;
    op.targetLayer = layer;
    op.factor = factor;
    op.pivotFallback = pivot;
    op.sampling = sampling;
    auto added = doc.engine().addOperation(layer, op);
    return added.ok() ? added.value : ls::OperationId{};
}

namespace {

// What the layer draws, in its own pixels: every element but an erase.
ls::Rect2f drawnBounds(Document& doc, ls::LayerId layer) {
    ls::IntervalSet all;
    for (const Element& element : elementsOf(doc, layer)) {
        if (element.kind != ElementKind::Erase) {
            all = ls::geom::unionSets(all, ls::geom::normalize(elementCoverage(doc, element)));
        }
    }
    if (all.empty()) {
        return {};
    }
    const ls::Rect2i box = ls::geom::bounds(all);
    return { { static_cast<float>(box.min.x), static_cast<float>(box.min.y) },
             { static_cast<float>(box.max.x), static_cast<float>(box.max.y) } };
}

} // namespace

bool readFreeScale(Document& doc, ls::LayerId layer, FreeScale* out) {
    const std::vector<TransformEntry> transforms = listTransforms(doc, layer);
    if (transforms.size() != 2 || transforms[0].kind != TransformKind::Scale ||
        transforms[1].kind != TransformKind::Offset) {
        return false;
    }
    const ls::Rect2f drawn = drawnBounds(doc, layer);
    if (drawn.empty()) {
        return false;
    }
    const TransformEntry& scale = transforms[0];
    const ls::Vec2f d = transforms[1].delta;
    const auto place = [&](ls::Vec2f p) {
        return ls::Vec2f{ scale.pivot.x + scale.factor.x * (p.x - scale.pivot.x) + d.x,
                          scale.pivot.y + scale.factor.y * (p.y - scale.pivot.y) + d.y };
    };
    out->scale = scale.id;
    out->offset = transforms[1].id;
    out->drawn = drawn;
    out->box = { place(drawn.min), place(drawn.max) };
    return true;
}

bool addFreeScale(Document& doc, ls::LayerId layer) {
    if (!listTransforms(doc, layer).empty()) {
        return false;
    }
    const ls::Rect2f drawn = drawnBounds(doc, layer);
    if (drawn.empty()) {
        return false;
    }
    return addScale(doc, layer, { 1.f, 1.f }, drawn.min, ls::SamplingPolicy::Center).valid() &&
           addOffset(doc, layer, { 0.f, 0.f }).valid();
}

bool setFreeScaleBox(Document& doc, const FreeScale& scale, ls::Rect2f box) {
    const float w = scale.drawn.width();
    const float h = scale.drawn.height();
    if (w <= 0.f || h <= 0.f || box.width() <= 0.f || box.height() <= 0.f) {
        return false;
    }
    auto pivotValue = doc.engine().getOperationParameter(scale.scale, "pivotFallback");
    const ls::Vec2f* pivot = pivotValue.ok() ? std::get_if<ls::Vec2f>(&pivotValue.value) : nullptr;
    const ls::Vec2f p = pivot != nullptr ? *pivot : scale.drawn.min;
    const ls::Vec2f factor { box.width() / w, box.height() / h };
    // Where the scale alone puts the drawing's corner; the offset takes it
    // the rest of the way.
    const ls::Vec2f corner { p.x + factor.x * (scale.drawn.min.x - p.x),
                             p.y + factor.y * (scale.drawn.min.y - p.y) };
    return setScaleFactor(doc, scale.scale, factor) &&
           setOffsetDelta(doc, scale.offset, { box.min.x - corner.x, box.min.y - corner.y });
}

ls::OperationId addOffset(Document& doc, ls::LayerId layer, ls::Vec2f delta) {
    ls::TranslateOp op;
    op.targetLayer = layer;
    op.delta = delta;
    auto added = doc.engine().addOperation(layer, op);
    return added.ok() ? added.value : ls::OperationId{};
}

ls::OperationId addMirror(Document& doc, ls::LayerId layer, ls::MirrorAxis axis,
                          ls::Vec2f pivot) {
    ls::MirrorOp op;
    op.targetLayer = layer;
    op.axis = axis;
    op.pivotFallback = pivot;
    auto added = doc.engine().addOperation(layer, op);
    return added.ok() ? added.value : ls::OperationId{};
}

// ----------------------------------------------------------------- reading --

std::vector<TransformEntry> listTransforms(Document& doc, ls::LayerId layer) {
    std::vector<TransformEntry> out;

    auto operations = doc.engine().getLayerOperations(layer);
    if (operations.fail()) {
        return out;
    }

    for (const ls::OperationInfo& info : operations.value) {
        TransformEntry entry;
        entry.id = info.id;

        if (info.type == "RotateOp") {
            entry.kind = TransformKind::Rotate;
            entry.angleDegrees = floatParam(doc, info.id, "angleDegrees", 0.f);
        } else if (info.type == "ScaleOp") {
            entry.kind = TransformKind::Scale;
            entry.factor = vec2Param(doc, info.id, "factor", {1.f, 1.f});
        } else if (info.type == "MirrorOp") {
            entry.kind = TransformKind::Mirror;
            entry.axis = static_cast<ls::MirrorAxis>(intParam(doc, info.id, "axis", 0));
        } else if (info.type == "TranslateOp") {
            entry.kind = TransformKind::Offset;
            entry.delta = vec2Param(doc, info.id, "delta", {0.f, 0.f});
        } else {
            continue;       // a fill, an outline, something else: not ours
        }

        entry.pivot = vec2Param(doc, info.id, "pivotFallback", {0.f, 0.f});
        if (entry.kind == TransformKind::Rotate || entry.kind == TransformKind::Scale) {
            entry.sampling = static_cast<ls::SamplingPolicy>(
                intParam(doc, info.id, "sampling", static_cast<int>(ls::SamplingPolicy::Coverage)));
        }
        out.push_back(entry);
    }
    return out;
}

// ----------------------------------------------------------------- editing --
//
// Every one of these drives a parameter on an operation that already exists.
// That is what makes dragging a slider free: a hundred angles leave one
// operation, and each compile resolves from the same untouched source.

bool setRotateAngle(Document& doc, ls::OperationId op, float degrees) {
    return doc.engine()
        .setOperationParameter(op, "angleDegrees", ls::ParameterValue{degrees})
        .ok();
}

bool setScaleFactor(Document& doc, ls::OperationId op, ls::Vec2f factor) {
    return doc.engine()
        .setOperationParameter(op, "factor", ls::ParameterValue{factor})
        .ok();
}

bool setTransformPivot(Document& doc, ls::OperationId op, ls::Vec2f pivot) {
    return doc.engine()
        .setOperationParameter(op, "pivotFallback", ls::ParameterValue{pivot})
        .ok();
}

bool setOffsetDelta(Document& doc, ls::OperationId op, ls::Vec2f delta) {
    return doc.engine().setOperationParameter(op, "delta", ls::ParameterValue{ delta }).ok();
}

bool setTransformSampling(Document& doc, ls::OperationId op, ls::SamplingPolicy sampling) {
    return doc.engine().setOperationParameter(
        op, "sampling", ls::ParameterValue{ static_cast<int64_t>(sampling) }).ok();
}

bool removeTransform(Document& doc, ls::LayerId layer, ls::OperationId op) {
    return doc.engine().removeOperation(layer, op).ok();
}

bool clearTransforms(Document& doc, ls::LayerId layer) {
    bool removedAny = false;
    for (const TransformEntry& entry : listTransforms(doc, layer)) {
        removedAny = doc.engine().removeOperation(layer, entry.id).ok() || removedAny;
    }
    return removedAny;
}

// ----------------------------------------------------------------- mapping --

ls::Mat3f layerTransform(Document& doc, ls::LayerId layer) {
    ls::Mat3f composed = ls::Mat3f::identity();

    // Composed in resolution order. Each operation acts on what the ones before
    // it produced, so a later transform multiplies on the left.
    for (const TransformEntry& entry : listTransforms(doc, layer)) {
        ls::Mat3f step = ls::Mat3f::identity();
        switch (entry.kind) {
            case TransformKind::Rotate:
                step = ls::Mat3f::rotation(entry.angleDegrees);
                break;
            case TransformKind::Scale:
                step = ls::Mat3f::scaling(entry.factor);
                break;
            case TransformKind::Mirror:
                step = ls::Mat3f::scaling({
                    entry.axis == ls::MirrorAxis::Y ? 1.f : -1.f,
                    entry.axis == ls::MirrorAxis::X ? 1.f : -1.f });
                break;
            case TransformKind::Offset:
                step = ls::Mat3f::translation(entry.delta);
                break;
        }
        composed = ls::Mat3f::aroundPivot(step, entry.pivot).mul(composed);
    }
    return composed;
}

ls::IntervalSet canvasAreaOnLayer(Document& doc, ls::LayerId layer, const ls::IntervalSet& area) {
    if (listTransforms(doc, layer).empty() || area.empty()) {
        return area;
    }
    const ls::Mat3f toCanvas = layerTransform(doc, layer);
    auto back = toCanvas.inverse();
    if (back.fail()) {
        return {};
    }
    ls::IntervalSet out;
    const auto add = [&out](ls::Vec2f p) {
        const int32_t x = static_cast<int32_t>(std::floor(p.x));
        out.intervals.push_back({ static_cast<int32_t>(std::floor(p.y)), x, x + 1 });
    };
    // Every canvas pixel of the area, carried back.
    for (const ls::Interval& run : area.intervals) {
        for (int32_t x = run.x0; x < run.x1; ++x) {
            add(back.value.transformPoint({ static_cast<float>(x) + 0.5f,
                                            static_cast<float>(run.y) + 0.5f }));
        }
    }
    // And every layer pixel near it whose centre lands inside.
    const ls::Rect2i box = ls::geom::bounds(area);
    float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;
    for (ls::Vec2f corner : { ls::Vec2f{ static_cast<float>(box.min.x), static_cast<float>(box.min.y) },
                              ls::Vec2f{ static_cast<float>(box.max.x), static_cast<float>(box.min.y) },
                              ls::Vec2f{ static_cast<float>(box.min.x), static_cast<float>(box.max.y) },
                              ls::Vec2f{ static_cast<float>(box.max.x), static_cast<float>(box.max.y) } }) {
        const ls::Vec2f at = back.value.transformPoint(corner);
        minX = std::min(minX, at.x);
        minY = std::min(minY, at.y);
        maxX = std::max(maxX, at.x);
        maxY = std::max(maxY, at.y);
    }
    for (int32_t y = static_cast<int32_t>(std::floor(minY)) - 1;
         y <= static_cast<int32_t>(std::ceil(maxY)); ++y) {
        for (int32_t x = static_cast<int32_t>(std::floor(minX)) - 1;
             x <= static_cast<int32_t>(std::ceil(maxX)); ++x) {
            const ls::Vec2f at = toCanvas.transformPoint({ static_cast<float>(x) + 0.5f,
                                                           static_cast<float>(y) + 0.5f });
            if (ls::geom::contains(area, { static_cast<int32_t>(std::floor(at.x)),
                                           static_cast<int32_t>(std::floor(at.y)) })) {
                out.intervals.push_back({ y, x, x + 1 });
            }
        }
    }
    return ls::geom::normalize(std::move(out));
}

bool mapCanvasPointToLayer(Document& doc, ls::LayerId layer, ls::Vec2f canvasPoint,
                           ls::Vec2f* out) {
    if (out == nullptr) {
        return false;
    }
    const std::vector<TransformEntry> transforms = listTransforms(doc, layer);
    if (transforms.empty()) {
        *out = canvasPoint;             // the common case, and it costs nothing
        return true;
    }

    auto inverse = layerTransform(doc, layer).inverse();
    if (inverse.fail()) {
        return false;                   // a scale of zero has no way back
    }

    // Sampled at the centre of the pixel rather than its corner, so a point
    // maps to the pixel a person would say they clicked on.
    const ls::Vec2f centre { canvasPoint.x + 0.5f, canvasPoint.y + 0.5f };
    const ls::Vec2f mapped = inverse.value.transformPoint(centre);
    *out = { mapped.x - 0.5f, mapped.y - 0.5f };
    return true;
}

} // namespace fast
