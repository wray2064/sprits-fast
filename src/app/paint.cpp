// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

#include "app/paint.h"

#include <cstdlib>

namespace fast {

bool createPaintLayer(Document& doc, ls::SpriteId sprite, const std::string& name,
                      ls::Color color, PaintLayer* out) {
    if (out == nullptr) {
        return false;
    }
    ls::LSContext& engine = doc.engine();

    doc.beginAction("Add layer");

    auto layer = engine.createLayer(sprite, {name});
    if (layer.fail()) {
        doc.abandonAction();
        return false;
    }

    // An empty region to accumulate into. Starting from intervals rather than
    // from pixels means the shape begins genuinely empty, with nothing drawn.
    auto region = engine.createRegionFromIntervals(doc.id(), ls::IntervalSet{});
    if (region.fail()) {
        doc.abandonAction();
        return false;
    }

    ls::FillSolidOp fill;
    fill.targetRegion = region.value;
    fill.fallbackColor = color;

    auto op = engine.addOperation(layer.value, fill);
    if (op.fail()) {
        doc.abandonAction();
        return false;
    }

    doc.endAction();

    out->layer = layer.value;
    out->region = region.value;
    out->fill = op.value;
    return true;
}

bool paintPixels(Document& doc, const PaintLayer& target,
                 const std::vector<ls::Vec2i>& pixels) {
    if (!target.drawable() || pixels.empty()) {
        return false;
    }

    // The colour is carried by the fill operation, not by the pixels. What is
    // stored here is only the shape; the colour on each PixelInput is ignored
    // for a region used this way, and the region is deliberately not asked to
    // close boundaries -- a pencil stroke is a mark, not an outline that should
    // seal and flood its interior.
    ls::PixelRegionDesc desc;
    desc.closeSameColorBoundaries = false;
    desc.pixels.reserve(pixels.size());
    for (ls::Vec2i pixel : pixels) {
        ls::PixelInput input;
        input.position = pixel;
        input.color = ls::Color{0, 0, 0, 255};
        desc.pixels.push_back(input);
    }

    return doc.engine().addPixelsToRegion(target.region, desc).ok();
}

bool erasePixels(Document& doc, const PaintLayer& target,
                 const std::vector<ls::Vec2i>& pixels) {
    if (!target.drawable() || pixels.empty()) {
        return false;
    }
    return doc.engine().erasePixelsFromRegion(target.region, pixels).ok();
}

bool setPaintColor(Document& doc, const PaintLayer& target, ls::Color color) {
    if (!target.valid()) {
        return false;
    }
    // One parameter on one operation. The drawing is not touched, which is the
    // point: recolouring is not a repaint.
    return doc.engine()
        .setOperationParameter(target.fill, "fallbackColor", ls::ParameterValue{color})
        .ok();
}

ls::Color paintColor(Document& doc, const PaintLayer& target) {
    if (!target.valid()) {
        return ls::Color{0, 0, 0, 0};
    }
    auto value = doc.engine().getOperationParameter(target.fill, "fallbackColor");
    if (value.fail()) {
        return ls::Color{0, 0, 0, 0};
    }
    if (const ls::Color* found = std::get_if<ls::Color>(&value.value)) {
        return *found;
    }
    return ls::Color{0, 0, 0, 0};
}

bool adoptPaintLayers(Document& doc, ls::SpriteId* outSprite,
                      std::vector<PaintLayer>* outLayers) {
    if (outSprite == nullptr || outLayers == nullptr) {
        return false;
    }
    outLayers->clear();

    ls::LSContext& engine = doc.engine();

    auto document = engine.getDocumentInfo(doc.id());
    if (document.fail() || document.value.sprites.empty()) {
        return false;
    }
    *outSprite = document.value.sprites.front();

    auto sprite = engine.getSpriteInfo(*outSprite);
    if (sprite.fail()) {
        return false;
    }

    for (ls::LayerId layer : sprite.value.layers) {
        auto operations = engine.getLayerOperations(layer);
        if (operations.fail()) {
            continue;
        }

        // The first fill that names a region is the one a pencil writes into.
        // Both kinds Fast makes are recognised: a solid colour and a dither are
        // different *rules for colouring* the same drawing, so a layer switched
        // to dithered must still be drawable after a reload.
        //
        // A layer built by another tool -- a gradient along an axis, a stroke
        // following a path -- has no such operation and is left alone rather
        // than guessed at.
        for (const ls::OperationInfo& op : operations.value) {
            // A stroked line has no region -- it names a polyline and encloses
            // no area. It is still a layer the panel must list, or it would go
            // on drawing while vanishing from the interface.
            if (op.type == "StrokePolylineOp") {
                PaintLayer found;
                found.layer = layer;
                found.fill = op.id;
                outLayers->push_back(found);
                break;
            }

            if (op.type != "FillSolidOp" && op.type != "FillDitherOp") {
                continue;
            }
            auto region = engine.getOperationParameter(op.id, "targetRegion");
            if (region.fail()) {
                continue;
            }
            const uint64_t* handle = std::get_if<uint64_t>(&region.value);
            if (handle == nullptr || *handle == 0) {
                continue;
            }

            PaintLayer found;
            found.layer = layer;
            found.fill = op.id;
            found.region.value = *handle;
            outLayers->push_back(found);
            break;
        }
    }
    return true;
}

std::vector<ls::Vec2i> linePixels(ls::Vec2i from, ls::Vec2i to) {
    // Bresenham. A drag reports positions per frame, not per pixel, so without
    // this a quick stroke is a row of dots.
    std::vector<ls::Vec2i> out;

    int32_t x = from.x;
    int32_t y = from.y;
    const int32_t dx = std::abs(to.x - from.x);
    const int32_t dy = -std::abs(to.y - from.y);
    const int32_t stepX = from.x < to.x ? 1 : -1;
    const int32_t stepY = from.y < to.y ? 1 : -1;
    int32_t error = dx + dy;

    for (;;) {
        out.push_back({x, y});
        if (x == to.x && y == to.y) {
            break;
        }
        const int32_t doubled = 2 * error;
        if (doubled >= dy) {
            error += dy;
            x += stepX;
        }
        if (doubled <= dx) {
            error += dx;
            y += stepY;
        }
    }
    return out;
}

} // namespace fast
