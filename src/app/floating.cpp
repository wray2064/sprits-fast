// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

#include "app/floating.h"

#include "app/element.h"
#include "app/selection.h"
#include "app/transform.h"

#include <algorithm>

namespace fast {

namespace {

ls::IntervalSet regionPixels(Document& doc, ls::RegionId region) {
    auto intervals = doc.engine().getRegionIntervals(region);
    return intervals.ok() ? intervals.value : ls::IntervalSet{};
}

ls::IntervalSet canvasMask(Document& doc) {
    auto size = doc.engine().getCanvasSize(doc.id());
    if (size.fail()) {
        return {};
    }
    return rectangleMask({ 0, 0 }, { size.value.x - 1, size.value.y - 1 });
}

// A piece's element: a solid fill in `ink`, or a copy of the dithered element
// `dither` names, over a region of its own holding `pixels`.
bool makePiece(Document& doc, ls::LayerId layer, const Ink& ink, ls::OperationId dither,
               const ls::IntervalSet& pixels, Floating::Piece* out) {
    ls::LSContext& engine = doc.engine();
    auto region = engine.createRegionFromIntervals(doc.id(), pixels);
    if (region.fail()) {
        return false;
    }
    ls::Result<ls::OperationId> op = ls::Result<ls::OperationId>::err(ls::LSError::InvalidId);
    if (dither.valid()) {
        auto source = engine.getOperation(dither);
        if (source.ok()) {
            op = engine.addOperation(layer, source.value);
            if (op.ok()) {
                engine.setOperationParameter(op.value, "targetRegion",
                    ls::ParameterValue{ static_cast<uint64_t>(region.value.value) });
            }
        }
    }
    if (!dither.valid() || op.fail()) {
        ls::FillSolidOp fill;
        fill.targetRegion = region.value;
        fill.fallbackColor = ink.colour;
        fill.paletteRole = ink.role;
        op = engine.addOperation(layer, fill);
    }
    if (op.fail()) {
        engine.deleteRegion(region.value);
        return false;
    }
    out->fill = op.value;
    out->region = region.value;
    out->original = pixels;
    return true;
}

bool isPiece(const Floating& floating, ls::OperationId fill) {
    for (const Floating::Piece& piece : floating.pieces) {
        if (piece.fill == fill) {
            return true;
        }
    }
    return false;
}

} // namespace

bool layerTakesSelections(Document& doc, ls::LayerId layer) {
    return layer.valid() && listTransforms(doc, layer).empty();
}

bool copyPixels(Document& doc, ls::LayerId layer, const ls::IntervalSet& mask, PixelClip* out) {
    if (out == nullptr || mask.empty() || !layerTakesSelections(doc, layer)) {
        return false;
    }
    PixelClip clip;
    clip.from = doc.id();
    for (const Element& element : elementsOf(doc, layer)) {
        if (element.kind != ElementKind::Paint) {
            continue;
        }
        const ls::IntervalSet inside =
            ls::geom::intersectSets(regionPixels(doc, element.region), mask);
        if (inside.empty()) {
            continue;
        }
        PixelClip::Piece piece;
        piece.pixels = inside;
        if (!inkOfElement(doc, element.fill, &piece.ink)) {
            // A dither: remembered by the element, and by a colour for a
            // document that does not have it.
            piece.dither = element.fill;
            PaintLayer as;
            as.layer = layer;
            as.fill = element.fill;
            as.region = element.region;
            piece.ink.colour = paintColor(doc, as);
        }
        clip.mask = ls::geom::unionSets(clip.mask, inside);
        clip.pieces.push_back(std::move(piece));
    }
    if (clip.empty()) {
        return false;
    }
    *out = std::move(clip);
    return true;
}

bool clearPixels(Document& doc, ls::LayerId layer, const ls::IntervalSet& mask) {
    if (mask.empty() || !layerTakesSelections(doc, layer)) {
        return false;
    }
    const std::vector<ls::Vec2i> pixels = pixelsOf(mask);
    bool any = false;
    for (const Element& element : elementsOf(doc, layer)) {
        if (element.kind != ElementKind::Paint) {
            continue;
        }
        if (ls::geom::intersectSets(regionPixels(doc, element.region), mask).empty()) {
            continue;
        }
        any = doc.engine().erasePixelsFromRegion(element.region, pixels).ok() || any;
    }
    if (any) {
        pruneEmptyInks(doc, layer);
    }
    return any;
}

bool liftPixels(Document& doc, ls::LayerId layer, const ls::IntervalSet& mask, Floating* out) {
    if (out == nullptr || mask.empty() || !layerTakesSelections(doc, layer)) {
        return false;
    }
    PixelClip clip;
    const bool anyPixels = copyPixels(doc, layer, mask, &clip);

    // The shapes the mask holds entirely. One that crosses its edge stays:
    // half a rectangle is not a thing a shape can be.
    std::vector<Floating::Shape> shapes;
    for (const Element& element : elementsOf(doc, layer)) {
        if (!element.isShape()) {
            continue;
        }
        ls::IntervalSet covers;
        if (element.region.valid()) {
            covers = regionPixels(doc, element.region);
        } else if (element.geometry.valid()) {
            auto bounds = doc.engine().getGeometryBounds(element.geometry);
            if (bounds.ok() && !bounds.value.pixelBounds.empty()) {
                covers = rectangleMask(bounds.value.pixelBounds.min,
                                       { bounds.value.pixelBounds.max.x - 1,
                                         bounds.value.pixelBounds.max.y - 1 });
            }
        }
        if (covers.empty() || !ls::geom::subtractSets(covers, mask).empty()) {
            continue;
        }
        Floating::Shape taken;
        taken.shape = shapeOfElement(layer, element);
        if (readShapeParams(doc, taken.shape, &taken.original)) {
            shapes.push_back(taken);
        }
    }
    if (!anyPixels && shapes.empty()) {
        return false;
    }

    Floating floating;
    floating.layer = layer;
    floating.originalMask = mask;
    floating.shapes = std::move(shapes);
    // Out of the layer first, then into pieces at the top: the pieces are
    // added after, so they draw over everything the layer holds.
    for (const Element& element : elementsOf(doc, layer)) {
        if (element.kind != ElementKind::Paint) {
            continue;
        }
        const ls::IntervalSet inside =
            ls::geom::intersectSets(regionPixels(doc, element.region), mask);
        if (!inside.empty()) {
            doc.engine().erasePixelsFromRegion(element.region, pixelsOf(inside));
        }
    }
    for (const PixelClip::Piece& taken : clip.pieces) {
        Floating::Piece piece;
        if (makePiece(doc, layer, taken.ink, taken.dither, taken.pixels, &piece)) {
            floating.pieces.push_back(std::move(piece));
        }
    }
    *out = std::move(floating);
    return !out->pieces.empty() || !out->shapes.empty();
}

bool floatClip(Document& doc, ls::LayerId layer, const PixelClip& clip, Floating* out) {
    if (out == nullptr || clip.empty() || !layerTakesSelections(doc, layer)) {
        return false;
    }
    Floating floating;
    floating.layer = layer;
    floating.originalMask = clip.mask;
    const bool sameDocument = clip.from == doc.id();
    for (const PixelClip::Piece& taken : clip.pieces) {
        Floating::Piece piece;
        const ls::OperationId dither = sameDocument ? taken.dither : ls::OperationId{};
        // A dither whose element was deleted since the copy pastes as its
        // colour rather than as nothing.
        const bool ditherLives = dither.valid() && doc.engine().getOperation(dither).ok();
        if (makePiece(doc, layer, taken.ink, ditherLives ? dither : ls::OperationId{},
                      taken.pixels, &piece)) {
            floating.pieces.push_back(std::move(piece));
        }
    }
    *out = std::move(floating);
    return !out->pieces.empty();
}

bool moveFloating(Document& doc, Floating& floating, ls::Vec2i offset) {
    if (!floating.active()) {
        return false;
    }
    floating.offset = offset;
    bool ok = true;
    for (const Floating::Piece& piece : floating.pieces) {
        ok = doc.engine().setRegionIntervals(piece.region,
                                             translated(piece.original, offset)).ok() && ok;
    }
    const ls::Vec2f by { static_cast<float>(offset.x), static_cast<float>(offset.y) };
    for (const Floating::Shape& taken : floating.shapes) {
        ShapeParams moved = taken.original;
        moved.from = { moved.from.x + by.x, moved.from.y + by.y };
        moved.to = { moved.to.x + by.x, moved.to.y + by.y };
        ok = updateShape(doc, taken.shape, moved) && ok;
    }
    return ok;
}

bool turnFloating(Document& doc, Floating& floating, FloatTurn turn) {
    if (!floating.active()) {
        return false;
    }
    // About the middle of what floats, as it was lifted; the offset carries
    // on applying afterwards, so a turned selection stays where it was put.
    const ls::Rect2i within = ls::geom::bounds(floating.originalMask);
    const auto apply = [&](const ls::IntervalSet& set) {
        switch (turn) {
            case FloatTurn::FlipHorizontal: return flippedHorizontally(set, within);
            case FloatTurn::FlipVertical:   return flippedVertically(set, within);
            case FloatTurn::Clockwise:      return rotatedQuarter(set, within, true);
            case FloatTurn::Anticlockwise:  return rotatedQuarter(set, within, false);
            case FloatTurn::HalfTurn:
                return flippedVertically(flippedHorizontally(set, within), within);
        }
        return set;
    };
    for (Floating::Piece& piece : floating.pieces) {
        piece.original = apply(piece.original);
    }
    // A shape's corners, turned the same way. Corners sit between pixels, so
    // the mirror of a corner at x is at min + max - x, where a pixel's is one
    // less: a rectangle over pixels 2..4 lands over the pixels the pieces do.
    const auto corner = [&](ls::Vec2f p) -> ls::Vec2f {
        const float minX = static_cast<float>(within.min.x);
        const float minY = static_cast<float>(within.min.y);
        const float w = static_cast<float>(within.width());
        const float h = static_cast<float>(within.height());
        const float ox = minX + static_cast<float>((within.width() - within.height()) / 2);
        const float oy = minY + static_cast<float>((within.height() - within.width()) / 2);
        const float u = p.x - minX;
        const float v = p.y - minY;
        switch (turn) {
            case FloatTurn::FlipHorizontal: return { minX + w - u, p.y };
            case FloatTurn::FlipVertical:   return { p.x, minY + h - v };
            case FloatTurn::Clockwise:      return { ox + (h - v), oy + u };
            case FloatTurn::Anticlockwise:  return { ox + v, oy + (w - u) };
            case FloatTurn::HalfTurn:       return { minX + w - u, minY + h - v };
        }
        return p;
    };
    for (Floating::Shape& taken : floating.shapes) {
        taken.original.from = corner(taken.original.from);
        taken.original.to = corner(taken.original.to);
    }
    floating.originalMask = apply(floating.originalMask);
    return moveFloating(doc, floating, floating.offset);
}

ls::IntervalSet floatingMask(const Floating& floating) {
    return translated(floating.originalMask, floating.offset);
}

bool dropFloating(Document& doc, Floating& floating) {
    if (!floating.active()) {
        return false;
    }
    ls::LSContext& engine = doc.engine();
    const ls::LayerId layer = floating.layer;
    const ls::IntervalSet canvas = canvasMask(doc);

    // Everything that lands, clipped to the canvas as every editor clips a
    // paste: pixels dragged off the edge are gone, not hiding out there.
    ls::IntervalSet landed;
    for (Floating::Piece& piece : floating.pieces) {
        const ls::IntervalSet kept = ls::geom::intersectSets(regionPixels(doc, piece.region), canvas);
        engine.setRegionIntervals(piece.region, kept);
        landed = ls::geom::unionSets(landed, kept);
    }

    // Where it lands, the layer's own colours give way -- a pixel has one
    // colour on a layer, and the one that was dropped is the one that shows.
    const std::vector<ls::Vec2i> landedPixels = pixelsOf(landed);
    const std::vector<Element> elements = elementsOf(doc, layer);
    long long lastShape = -1;
    for (size_t i = 0; i < elements.size(); ++i) {
        if (elements[i].isShape()) {
            lastShape = static_cast<long long>(i);
        }
    }
    for (const Element& element : elements) {
        if (element.kind != ElementKind::Paint || isPiece(floating, element.fill)) {
            continue;
        }
        if (!ls::geom::intersectSets(regionPixels(doc, element.region), landed).empty()) {
            engine.erasePixelsFromRegion(element.region, landedPixels);
        }
    }

    // A solid piece whose colour the layer already has, above every shape,
    // joins that element rather than staying a second one of the same colour.
    for (const Floating::Piece& piece : floating.pieces) {
        Ink ink;
        if (!inkOfElement(doc, piece.fill, &ink)) {
            continue;
        }
        ls::OperationId into;
        ls::RegionId intoRegion;
        for (size_t i = elements.size(); i-- > 0;) {
            const Element& element = elements[i];
            if (static_cast<long long>(i) < lastShape) {
                break;
            }
            Ink other;
            if (element.kind == ElementKind::Paint && !isPiece(floating, element.fill) &&
                inkOfElement(doc, element.fill, &other) && other == ink) {
                into = element.fill;
                intoRegion = element.region;
                break;
            }
        }
        if (!into.valid()) {
            continue;
        }
        const ls::IntervalSet joined =
            ls::geom::unionSets(regionPixels(doc, intoRegion), regionPixels(doc, piece.region));
        engine.setRegionIntervals(intoRegion, joined);
        engine.removeOperation(layer, piece.fill);
        engine.deleteRegion(piece.region);
    }

    pruneEmptyInks(doc, layer);
    floating = Floating{};
    return true;
}

} // namespace fast
