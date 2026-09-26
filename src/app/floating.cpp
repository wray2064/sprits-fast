// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

#include "app/floating.h"

#include "app/element.h"
#include "app/ink.h"
#include "app/selection.h"
#include "app/transform.h"

#include <algorithm>
#include <functional>
#include <iterator>

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

// Every point of every mark, moved by `to`: a path's points, an area's
// corners. Marks are shapes, so a move or a turn of them is exact.
ls::StrokesDesc mappedMarks(const ls::StrokesDesc& marks, const std::function<ls::Vec2f(ls::Vec2f)>& to) {
    ls::StrokesDesc out = marks;
    for (ls::PenStroke& mark : out.strokes) {
        for (ls::Vec2f& p : mark.points) {
            p = to(p);
        }
        for (auto& contour : mark.area.contours) {
            for (ls::Vec2f& p : contour) {
                p = to(p);
            }
        }
    }
    return out;
}

ls::StrokesDesc translatedMarks(const ls::StrokesDesc& marks, ls::Vec2i by) {
    const ls::Vec2f step { static_cast<float>(by.x), static_cast<float>(by.y) };
    return mappedMarks(marks, [step](ls::Vec2f p) { return ls::Vec2f{ p.x + step.x, p.y + step.y }; });
}

// The part of some marks inside `mask`, still marks: thin lines cut at the
// mask's edge, areas trimmed to it, and what cannot be cut -- a wide stroke
// -- trimmed by an erase of everything outside.
ls::StrokesDesc keptInside(const ls::StrokesDesc& marks, const ls::IntervalSet& mask) {
    ls::StrokesDesc kept = marks;
    const ls::Rect2i box = ls::geom::bounds(ls::geom::rasterizeStrokes(marks));
    if (box.empty()) {
        return kept;
    }
    const ls::IntervalSet outside = ls::geom::subtractSets(
        rectangleMask(box.min, { box.max.x - 1, box.max.y - 1 }), mask);
    if (ls::geom::cutStrokes(kept, outside)) {
        ls::PenStroke trim = areaMark(outside);
        trim.erase = true;
        kept.strokes.push_back(std::move(trim));
    }
    return kept;
}

// A piece's element: a run on top of the layer holding `marks`, coloured by a
// solid `ink` or by a copy of the dithered element `dither` names.
bool makePiece(Document& doc, ls::LayerId layer, const Ink& ink, ls::OperationId dither,
               const ls::StrokesDesc& marks, const ls::IntervalSet& pixels, Floating::Piece* out) {
    ls::LSContext& engine = doc.engine();
    const ls::RegionId region = createFreehandRegion(doc);
    ls::GeometryId strokes;
    if (!region.valid() || !readStrokes(doc, region, &strokes, nullptr) ||
        engine.updateStrokes(strokes, marks).fail()) {
        if (region.valid()) {
            deleteRegionAndShapes(doc, region);
        }
        return false;
    }
    ls::Result<ls::OperationId> op = ls::Result<ls::OperationId>::err(ls::LSError::InvalidId);
    if (dither.valid()) {
        auto source = engine.getOperation(dither);
        if (source.ok()) {
            if (auto* rule = std::get_if<ls::FillDitherOp>(&source.value)) {
                rule->targetRegion = region;
                op = engine.addOperation(layer, source.value);
            }
        }
    }
    if (op.fail()) {
        ls::FillSolidOp fill;
        fill.targetRegion = region;
        fill.fallbackColor = ink.colour;
        fill.paletteRole = ink.role;
        op = engine.addOperation(layer, fill);
    }
    if (op.fail()) {
        deleteRegionAndShapes(doc, region);
        return false;
    }
    keepEffectsLast(doc, layer);
    out->fill = op.value;
    out->region = region;
    out->original = pixels;
    out->marks = marks;
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

bool writePieceMarks(Document& doc, const Floating::Piece& piece, const ls::StrokesDesc& marks) {
    ls::GeometryId strokes;
    return readStrokes(doc, piece.region, &strokes, nullptr) &&
           doc.engine().updateStrokes(strokes, marks).ok();
}

// What a selection lifts and copies: freehand runs and what the bucket filled.
bool carriesPixels(const Element& element) {
    return (element.kind == ElementKind::Paint || element.kind == ElementKind::Fill) &&
           element.region.valid();
}

} // namespace

std::vector<std::vector<ls::Vec2i>> stampOf(const PixelClip& clip, ls::Vec2i centre) {
    std::vector<std::vector<ls::Vec2i>> out;
    const ls::Rect2i box = ls::geom::bounds(clip.mask);
    const ls::Vec2i middle { box.min.x + box.width() / 2, box.min.y + box.height() / 2 };
    const ls::Vec2i by { centre.x - middle.x, centre.y - middle.y };
    out.reserve(clip.pieces.size());
    for (const PixelClip::Piece& piece : clip.pieces) {
        out.push_back(pixelsOf(translated(piece.pixels, by)));
    }
    return out;
}

bool layerTakesSelections(Document& doc, ls::LayerId layer) {
    return layer.valid() && listTransforms(doc, layer).empty();
}

bool copyPixels(Document& doc, ls::LayerId layer, const ls::IntervalSet& mask, PixelClip* out) {
    if (out == nullptr || mask.empty() || !layerTakesSelections(doc, layer)) {
        return false;
    }
    PixelClip clip;
    clip.from = doc.id();
    // What shows: topmost first, each element only where nothing above it
    // draws -- a colour painted over is covered, not copied.
    const std::vector<Element> elements = elementsOf(doc, layer);
    ls::IntervalSet covered;
    std::vector<PixelClip::Piece> topFirst;
    for (size_t n = elements.size(); n-- > 0;) {
        const Element& element = elements[n];
        const ls::IntervalSet drawn = elementCoverage(doc, element);
        const ls::IntervalSet inside = ls::geom::subtractSets(
            ls::geom::intersectSets(drawn, mask), covered);
        covered = ls::geom::unionSets(covered, drawn);
        if (!carriesPixels(element) || inside.empty()) {
            continue;
        }
        PixelClip::Piece piece;
        piece.pixels = inside;
        ls::StrokesDesc marks;
        if (readStrokes(doc, element.region, nullptr, &marks)) {
            piece.marks = keptInside(marks, inside);
        } else {
            piece.marks.strokes.push_back(areaMark(inside));
        }
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
        topFirst.push_back(std::move(piece));
    }
    // In draw order again.
    clip.pieces.assign(std::make_move_iterator(topFirst.rbegin()),
                       std::make_move_iterator(topFirst.rend()));
    if (clip.empty()) {
        return false;
    }
    *out = std::move(clip);
    return true;
}

bool clearPixels(Document& doc, ls::LayerId layer, const ls::IntervalSet& mask,
                 bool shapesToo) {
    if (mask.empty() || !layerTakesSelections(doc, layer)) {
        return false;
    }
    const ls::PenStroke eraser = areaMark(mask);
    bool any = false;
    if (shapesToo) {
        any = eraseFromLayer(doc, layer, eraser, mask);
    } else {
        for (const Element& element : elementsOf(doc, layer)) {
            if (carriesPixels(element)) {
                any = eraseFromRegion(doc, element.region, eraser, mask) || any;
            }
        }
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
    std::vector<Floating::ShapeErase> erases;
    for (const Element& element : elementsOf(doc, layer)) {
        if (!element.isGeometry()) {
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
            // Its erase is its own, and goes with it.
            if (element.region.valid()) {
                auto erase = doc.engine().getRegionErase(element.region);
                if (erase.ok() && erase.value.valid()) {
                    auto marks = doc.engine().getStrokes(erase.value);
                    if (marks.ok()) {
                        erases.push_back({ erase.value, marks.value });
                    }
                }
            }
        }
    }
    if (!anyPixels && shapes.empty()) {
        return false;
    }

    Floating floating;
    floating.layer = layer;
    floating.originalMask = mask;
    floating.shapes = std::move(shapes);
    floating.shapeErases = std::move(erases);
    // Out of the layer first, then into pieces at the top: the pieces are
    // added after, so they draw over everything the layer holds.
    const ls::PenStroke eraser = areaMark(mask);
    for (const Element& element : elementsOf(doc, layer)) {
        if (carriesPixels(element)) {
            eraseFromRegion(doc, element.region, eraser, mask);
        }
    }
    for (const PixelClip::Piece& taken : clip.pieces) {
        Floating::Piece piece;
        if (makePiece(doc, layer, taken.ink, taken.dither, taken.marks, taken.pixels, &piece)) {
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
        ls::StrokesDesc marks = taken.marks;
        if (marks.strokes.empty()) {
            marks.strokes.push_back(areaMark(taken.pixels));
        }
        if (makePiece(doc, layer, taken.ink, ditherLives ? dither : ls::OperationId{},
                      marks, taken.pixels, &piece)) {
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
        ok = writePieceMarks(doc, piece, translatedMarks(piece.marks, offset)) && ok;
    }
    for (const Floating::ShapeErase& erase : floating.shapeErases) {
        ok = doc.engine().updateStrokes(erase.strokes, translatedMarks(erase.original, offset)).ok() && ok;
    }
    const ls::Vec2f by { static_cast<float>(offset.x), static_cast<float>(offset.y) };
    for (const Floating::Shape& taken : floating.shapes) {
        ShapeParams moved = taken.original;
        mapShapePoints(moved, [by](ls::Vec2f p) { return ls::Vec2f{ p.x + by.x, p.y + by.y }; });
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
    // A point, turned the same way. Corners and pixel centres alike: the
    // mirror of x is at min + max - x, which puts a pixel's centre where the
    // pixels themselves land and a rectangle's corners round the same pixels.
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
    for (Floating::Piece& piece : floating.pieces) {
        piece.original = apply(piece.original);
        piece.marks = mappedMarks(piece.marks, corner);
    }
    for (Floating::ShapeErase& erase : floating.shapeErases) {
        erase.original = mappedMarks(erase.original, corner);
    }
    for (Floating::Shape& taken : floating.shapes) {
        mapShapePoints(taken.original, corner);
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

    // Clipped to the canvas as every editor clips a paste: what was dragged
    // off the edge is gone, not hiding out there.
    for (const Floating::Piece& piece : floating.pieces) {
        ls::GeometryId strokes;
        ls::StrokesDesc marks;
        if (readStrokes(doc, piece.region, &strokes, &marks)) {
            engine.updateStrokes(strokes, keptInside(marks, canvas));
        }
    }

    // A solid piece straight above a run of its own colour joins it, rather
    // than staying a second run of the same colour one above the other.
    for (const Floating::Piece& piece : floating.pieces) {
        Ink ink;
        if (!inkOfElement(doc, piece.fill, &ink)) {
            continue;
        }
        const std::vector<Element> elements = elementsOf(doc, layer);
        for (size_t i = 1; i < elements.size(); ++i) {
            if (elements[i].fill != piece.fill) {
                continue;
            }
            const Element& below = elements[i - 1];
            Ink other;
            ls::GeometryId belowStrokes;
            ls::StrokesDesc belowMarks;
            ls::StrokesDesc pieceMarks;
            if (below.kind == ElementKind::Paint && !isPiece(floating, below.fill) &&
                inkOfElement(doc, below.fill, &other) && other == ink &&
                readStrokes(doc, below.region, &belowStrokes, &belowMarks) &&
                readStrokes(doc, piece.region, nullptr, &pieceMarks)) {
                belowMarks.strokes.insert(belowMarks.strokes.end(), pieceMarks.strokes.begin(),
                                          pieceMarks.strokes.end());
                engine.updateStrokes(belowStrokes, belowMarks);
                engine.removeOperation(layer, piece.fill);
                deleteRegionAndShapes(doc, piece.region);
            }
            break;
        }
    }

    pruneEmptyInks(doc, layer);
    floating = Floating{};
    return true;
}

} // namespace fast
