// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

#include "app/ink.h"
#include "app/shape.h"

#include "app/element.h"
#include "app/transform.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace fast {

bool operator==(const Ink& a, const Ink& b) {
    // Two inks through the same slot are the same ink whatever colour each
    // remembers as its fallback: the slot is what they paint.
    if (a.role != b.role) {
        return false;
    }
    if (a.usesSlot()) {
        return true;
    }
    return a.colour.r == b.colour.r && a.colour.g == b.colour.g &&
           a.colour.b == b.colour.b && a.colour.a == b.colour.a;
}

namespace {

bool isSolid(Document& doc, ls::OperationId op) {
    auto info = doc.engine().getOperationInfo(op);
    return info.ok() && info.value.type == "FillSolidOp";
}

// A run: a freehand element whose region is made of strokes.
bool isRun(Document& doc, const Element& element) {
    return element.kind == ElementKind::Paint && element.region.valid() &&
           readStrokes(doc, element.region, nullptr, nullptr);
}

// A new run on top of the layer, coloured by `fill` -- a solid colour or a
// copy of another element's rule -- over strokes with nothing in them yet.
bool addRun(Document& doc, ls::LayerId layer, ls::Operation fill, PaintLayer* out) {
    ls::LSContext& engine = doc.engine();
    const ls::RegionId region = createFreehandRegion(doc);
    if (!region.valid()) {
        return false;
    }
    if (auto* solid = std::get_if<ls::FillSolidOp>(&fill)) {
        solid->targetRegion = region;
    } else if (auto* dither = std::get_if<ls::FillDitherOp>(&fill)) {
        dither->targetRegion = region;
    } else {
        deleteRegionAndShapes(doc, region);
        return false;
    }
    auto op = engine.addOperation(layer, fill);
    if (op.fail()) {
        deleteRegionAndShapes(doc, region);
        return false;
    }
    // New paint is drawn before any transform, outline or shadow, so they see it.
    keepEffectsLast(doc, layer);
    out->layer = layer;
    out->region = region;
    out->fill = op.value;
    return true;
}

// What a path of centres draws, the brush stamped along it.
ls::IntervalSet stamped(const std::vector<ls::Vec2i>& centres, const PenBrush& brush) {
    return markPixels(pathMark(centres, brush));
}

std::vector<ls::Vec2i> listOf(const ls::IntervalSet& set) {
    std::vector<ls::Vec2i> out;
    for (const ls::Interval& run : set.intervals) {
        for (int32_t x = run.x0; x < run.x1; ++x) {
            out.push_back({ x, run.y });
        }
    }
    return out;
}

bool sameBrush(const PenBrush& a, const PenBrush& b) {
    return a.size == b.size && a.round == b.round && a.pixelPerfect == b.pixelPerfect;
}

// A line or a curve drawn as a path has no region to keep an erase: what the
// eraser takes from those goes into an erase of the layer's own, a clear laid
// over what is drawn before it -- kept as strokes, so it moves with them.
ls::RegionId pathShapesEraseOf(Document& doc, ls::LayerId layer) {
    const std::vector<Element> all = elementsOf(doc, layer);
    // The topmost erase, unless something was drawn after it: that one would
    // then be under the erase, and moving the erase above it would cut into
    // it whatever was erased before.
    if (!all.empty() && all.back().kind == ElementKind::Erase &&
        readStrokes(doc, all.back().region, nullptr, nullptr)) {
        return all.back().region;
    }
    ls::LSContext& engine = doc.engine();
    const ls::RegionId region = createFreehandRegion(doc);
    if (!region.valid()) {
        return ls::RegionId{};
    }
    ls::ClearRegionOp clear;
    clear.targetRegion = region;
    if (engine.addOperation(layer, clear).fail()) {
        deleteRegionAndShapes(doc, region);
        return ls::RegionId{};
    }
    keepEffectsLast(doc, layer);
    return region;
}

} // namespace

bool inkOfElement(Document& doc, ls::OperationId fill, Ink* out) {
    if (out == nullptr || !isSolid(doc, fill)) {
        return false;
    }
    ls::LSContext& engine = doc.engine();
    auto colour = engine.getOperationParameter(fill, "fallbackColor");
    auto role = engine.getOperationParameter(fill, "paletteRole");
    if (colour.fail() || role.fail()) {
        return false;
    }
    const ls::Color* c = std::get_if<ls::Color>(&colour.value);
    const int64_t* r = std::get_if<int64_t>(&role.value);
    if (c == nullptr || r == nullptr) {
        return false;
    }
    out->colour = *c;
    out->role = static_cast<ls::ColorRole>(*r);
    return true;
}

std::vector<ls::OperationId> elementsWithInk(Document& doc, ls::LayerId layer, const Ink& ink) {
    std::vector<ls::OperationId> out;
    for (const Element& element : elementsOf(doc, layer)) {
        Ink found;
        if (element.kind == ElementKind::Paint && inkOfElement(doc, element.fill, &found) &&
            found == ink) {
            out.push_back(element.fill);
        }
    }
    return out;
}

bool beginInkStroke(Document& doc, ls::LayerId layer, const Ink& ink, InkStroke* out) {
    if (out == nullptr || !layer.valid()) {
        return false;
    }
    *out = InkStroke{};
    out->layer = layer;
    const std::vector<Element> all = elementsOf(doc, layer);
    if (!all.empty()) {
        const Element& top = all.back();
        Ink found;
        if (isRun(doc, top) && inkOfElement(doc, top.fill, &found) && found == ink) {
            out->target.layer = layer;
            out->target.region = top.region;
            out->target.fill = top.fill;
            return true;
        }
    }
    ls::FillSolidOp fill;
    fill.fallbackColor = ink.colour;
    fill.paletteRole = ink.role;
    return addRun(doc, layer, fill, &out->target);
}

bool beginElementStroke(Document& doc, const PaintLayer& element, InkStroke* out) {
    if (out == nullptr || !element.valid()) {
        return false;
    }
    *out = InkStroke{};
    out->layer = element.layer;
    if (element.region.valid()) {
        const RegionMade made = regionMadeOf(doc, element.region);
        if (made == RegionMade::Strokes || made == RegionMade::Pixels) {
            out->target = element;
            return true;
        }
    }
    auto rule = doc.engine().getOperation(element.fill);
    return rule.ok() && addRun(doc, element.layer, rule.value, &out->target);
}

bool beginEraseStroke(Document& doc, ls::LayerId layer, InkStroke* out) {
    (void)doc;
    if (out == nullptr || !layer.valid()) {
        return false;
    }
    *out = InkStroke{};
    out->layer = layer;
    out->eraser = true;
    return true;
}

bool eraseFromLayer(Document& doc, ls::LayerId layer, const ls::PenStroke& eraser,
                    const ls::IntervalSet& pixels) {
    if (pixels.empty()) {
        return false;
    }
    bool any = false;
    bool pathShapeUnder = false;
    for (const Element& element : elementsOf(doc, layer)) {
        if (element.kind == ElementKind::Erase) {
            continue;
        }
        if (!element.region.valid()) {
            // A line or a curve: its pixels, to see whether the eraser is on it.
            pathShapeUnder = pathShapeUnder ||
                !ls::geom::intersectSets(elementCoverage(doc, element), pixels).empty();
            continue;
        }
        any = eraseFromRegion(doc, element.region, eraser, pixels) || any;
    }
    if (pathShapeUnder) {
        const ls::RegionId erase = pathShapesEraseOf(doc, layer);
        ls::GeometryId geometry;
        ls::StrokesDesc desc;
        if (erase.valid() && readStrokes(doc, erase, &geometry, &desc)) {
            ls::PenStroke rub = eraser;
            rub.erase = false;
            desc.strokes.push_back(std::move(rub));
            any = doc.engine().updateStrokes(geometry, desc).ok() || any;
        }
    }
    return any;
}

bool strokeAlong(Document& doc, InkStroke& stroke, int copy, const std::vector<ls::Vec2i>& centres,
                 const PenBrush& brush) {
    if (centres.empty()) {
        return false;
    }
    if (stroke.erasing()) {
        return eraseFromLayer(doc, stroke.layer, pathMark(centres, brush), stamped(centres, brush));
    }
    ls::GeometryId geometry;
    ls::StrokesDesc desc;
    if (!readStrokes(doc, stroke.target.region, &geometry, &desc)) {
        // A document's old pixels are still painted as pixels.
        return paintPixels(doc, stroke.target, listOf(stamped(centres, brush)));
    }
    InkStroke::Open& open = stroke.open[static_cast<size_t>(copy) & 3u];
    const ls::Vec2i first = centres.front();
    const bool carries = open.index >= 0 && open.index < static_cast<int>(desc.strokes.size()) &&
                         sameBrush(open.brush, brush) &&
                         std::max(std::abs(first.x - open.last.x), std::abs(first.y - open.last.y)) <= 1;
    if (carries) {
        std::vector<ls::Vec2f>& points = desc.strokes[static_cast<size_t>(open.index)].points;
        for (size_t i = 0; i < centres.size(); ++i) {
            if (i == 0 && centres[0].x == open.last.x && centres[0].y == open.last.y) {
                continue;                 // the pixel it stopped on
            }
            points.push_back({ static_cast<float>(centres[i].x) + 0.5f,
                               static_cast<float>(centres[i].y) + 0.5f });
        }
    } else {
        desc.strokes.push_back(pathMark(centres, brush));
        open.index = static_cast<int>(desc.strokes.size()) - 1;
        open.brush = brush;
    }
    open.last = centres.back();
    return doc.engine().updateStrokes(geometry, desc).ok();
}

bool sprayDots(Document& doc, const InkStroke& stroke, const std::vector<ls::Vec2i>& dots) {
    if (dots.empty()) {
        return false;
    }
    if (stroke.erasing()) {
        ls::IntervalSet set;
        for (ls::Vec2i p : dots) {
            set.intervals.push_back({ p.y, p.x, p.x + 1 });
        }
        set = ls::geom::normalize(set);
        return eraseFromLayer(doc, stroke.layer, areaMark(set), set);
    }
    ls::GeometryId geometry;
    ls::StrokesDesc desc;
    if (!readStrokes(doc, stroke.target.region, &geometry, &desc)) {
        return paintPixels(doc, stroke.target, dots);
    }
    ls::PenStroke mark = pathMark(dots, PenBrush{});
    mark.kind = ls::PenKind::Dots;
    desc.strokes.push_back(std::move(mark));
    return doc.engine().updateStrokes(geometry, desc).ok();
}

void closePaths(InkStroke& stroke) {
    for (InkStroke::Open& open : stroke.open) {
        open.index = -1;
    }
}

bool strokeInk(Document& doc, const InkStroke& stroke, const std::vector<ls::Vec2i>& pixels) {
    if (pixels.empty()) {
        return false;
    }
    ls::IntervalSet set;
    for (ls::Vec2i p : pixels) {
        set.intervals.push_back({ p.y, p.x, p.x + 1 });
    }
    set = ls::geom::normalize(set);
    if (stroke.erasing()) {
        return eraseFromLayer(doc, stroke.layer, areaMark(set), set);
    }
    ls::GeometryId geometry;
    ls::StrokesDesc desc;
    if (!readStrokes(doc, stroke.target.region, &geometry, &desc)) {
        return paintPixels(doc, stroke.target, pixels);
    }
    // An area laid right after another grows it, rather than stacking up one
    // mark for every stamp of a drag.
    if (!desc.strokes.empty() && desc.strokes.back().kind == ls::PenKind::Area &&
        !desc.strokes.back().erase) {
        const ls::IntervalSet grown = ls::geom::unionSets(
            ls::geom::rasterizeAreaDesc(desc.strokes.back().area), set);
        desc.strokes.back().area = ls::geom::traceArea(grown);
    } else {
        desc.strokes.push_back(areaMark(set));
    }
    return doc.engine().updateStrokes(geometry, desc).ok();
}

bool setElementInk(Document& doc, ls::OperationId fill, const Ink& ink) {
    if (!isSolid(doc, fill)) {
        return false;
    }
    ls::LSContext& engine = doc.engine();
    const bool colour = engine.setOperationParameter(fill, "fallbackColor",
                                                     ls::ParameterValue{ink.colour}).ok();
    const bool role = engine.setOperationParameter(
        fill, "paletteRole", ls::ParameterValue{static_cast<int64_t>(ink.role)}).ok();
    return colour && role;
}

int pruneEmptyInks(Document& doc, ls::LayerId layer, ls::OperationId keep) {
    ls::LSContext& engine = doc.engine();
    int removed = 0;
    for (const Element& element : elementsOf(doc, layer)) {
        if (element.fill == keep || !element.region.valid()) {
            continue;
        }
        const bool erase = element.kind == ElementKind::Erase;
        const bool freehand = element.kind == ElementKind::Paint || element.kind == ElementKind::Fill;
        if (!erase && !(freehand && isSolid(doc, element.fill))) {
            continue;
        }
        auto intervals = engine.getRegionIntervals(element.region);
        if (intervals.fail() || !intervals.value.empty()) {
            continue;
        }
        if (!erase && elementsOf(doc, layer).size() <= 1) {
            break;
        }
        if (engine.removeOperation(layer, element.fill).ok()) {
            deleteRegionAndShapes(doc, element.region);
            ++removed;
        }
    }
    return removed;
}

bool beginInkMode(Document& doc, ls::LayerId layer, InkMode mode, const Ink& second,
                  const std::vector<std::pair<ls::ColorRole, ls::Color>>& palette,
                  InkModeState* out) {
    if (out == nullptr) {
        return false;
    }
    *out = InkModeState{};
    out->mode = mode;
    out->layer = layer;
    if (mode == InkMode::Simple) {
        return true;
    }
    // In draw order, so what is on top of a pixel is what answers for it.
    for (const Element& element : elementsOf(doc, layer)) {
        const ls::IntervalSet pixels = elementCoverage(doc, element);
        if (pixels.empty()) {
            continue;
        }
        // An erase takes its pixels out of what is drawn before it.
        if (element.kind == ElementKind::Erase) {
            out->allowed = ls::geom::subtractSets(out->allowed, pixels);
            for (InkModeState::Held& held : out->held) {
                held.pixels = ls::geom::subtractSets(held.pixels, pixels);
            }
            continue;
        }
        Ink ink;
        const bool solid = (element.kind == ElementKind::Paint ||
                            element.kind == ElementKind::Fill) &&
                           inkOfElement(doc, element.fill, &ink);
        switch (mode) {
            case InkMode::LockAlpha:
                // Where anything on the layer draws -- shapes too, since a
                // shape's pixels are the layer's pixels to the eye.
                out->allowed = ls::geom::unionSets(out->allowed, pixels);
                break;
            case InkMode::Replace:
                // Only where the second colour is what shows.
                out->allowed = ls::geom::subtractSets(out->allowed, pixels);
                if (solid && ink == second) {
                    out->allowed = ls::geom::unionSets(out->allowed, pixels);
                }
                break;
            case InkMode::Shading:
                // Everything is held, so a pixel under a shape answers as the
                // shape -- which has no slot, and is left alone.
                out->held.push_back({ pixels, solid ? ink : Ink{ {0, 0, 0, 0}, ls::kColorRoleNone } });
                break;
            case InkMode::Simple:
                break;
        }
    }
    for (const auto& [role, colour] : palette) {
        out->ramp.push_back(role);
        out->rampColours.push_back(colour);
    }
    return true;
}

bool strokeInkMode(Document& doc, InkModeState& state, const InkStroke& stroke,
                   const std::vector<ls::Vec2i>& pixels, bool forward) {
    if (state.mode == InkMode::Simple) {
        return strokeInk(doc, stroke, pixels);
    }
    if (state.mode == InkMode::LockAlpha || state.mode == InkMode::Replace) {
        std::vector<ls::Vec2i> kept;
        kept.reserve(pixels.size());
        for (ls::Vec2i p : pixels) {
            if (ls::geom::contains(state.allowed, p)) {
                kept.push_back(p);
            }
        }
        return kept.empty() || strokeInk(doc, stroke, kept);
    }

    // Shading: each pixel once, to the slot beside its own.
    std::map<ls::ColorRole, std::vector<ls::Vec2i>> byTarget;
    for (ls::Vec2i p : pixels) {
        if (ls::geom::contains(state.shaded, p)) {
            continue;
        }
        const Ink* was = nullptr;
        for (const InkModeState::Held& held : state.held) {
            if (ls::geom::contains(held.pixels, p)) {
                was = &held.ink;
            }
        }
        if (was == nullptr || !was->usesSlot()) {
            continue;                   // empty, or a colour with no slot to step from
        }
        const auto at = std::find(state.ramp.begin(), state.ramp.end(), was->role);
        if (at == state.ramp.end()) {
            continue;
        }
        const long long index = at - state.ramp.begin();
        const long long next = forward ? index + 1 : index - 1;
        if (next < 0 || next >= static_cast<long long>(state.ramp.size())) {
            continue;                   // already at the end of the ramp
        }
        byTarget[state.ramp[static_cast<size_t>(next)]].push_back(p);
        state.shaded.intervals.push_back({ p.y, p.x, p.x + 1 });
    }
    state.shaded = ls::geom::normalize(std::move(state.shaded));
    bool ok = true;
    for (const auto& [role, targets] : byTarget) {
        auto found = state.strokes.find(role);
        if (found == state.strokes.end()) {
            Ink ink;
            ink.role = role;
            const auto at = std::find(state.ramp.begin(), state.ramp.end(), role);
            ink.colour = state.rampColours[static_cast<size_t>(at - state.ramp.begin())];
            InkStroke made;
            if (!beginInkStroke(doc, state.layer, ink, &made)) {
                ok = false;
                continue;
            }
            found = state.strokes.emplace(role, made).first;
        }
        ok = strokeInk(doc, found->second, targets) && ok;
    }
    return ok;
}

bool inkAt(Document& doc, ls::SpriteId sprite, ls::Vec2i pixel, Ink* out) {
    if (out == nullptr) {
        return false;
    }
    ls::LSContext& engine = doc.engine();
    auto info = engine.getSpriteInfo(sprite);
    if (info.fail()) {
        return false;
    }
    // Topmost first: the last layer in the list draws over the rest.
    for (size_t i = info.value.layers.size(); i-- > 0;) {
        const ls::LayerId layer = info.value.layers[i];
        auto layerInfo = engine.getLayerInfo(layer);
        if (layerInfo.fail() || !layerInfo.value.visible) {
            continue;
        }
        if (layerInfo.value.parentId.valid()) {
            auto group = engine.getGroupInfo(layerInfo.value.parentId);
            if (group.ok() && !group.value.visible) {
                continue;
            }
        }
        ls::Vec2f mapped;
        if (!mapCanvasPointToLayer(doc, layer,
                                   { static_cast<float>(pixel.x), static_cast<float>(pixel.y) },
                                   &mapped)) {
            continue;
        }
        const ls::Vec2i local { static_cast<int32_t>(std::floor(mapped.x + 0.5f)),
                                static_cast<int32_t>(std::floor(mapped.y + 0.5f)) };

        // Topmost element first, for the same reason.
        const std::vector<Element> elements = elementsOf(doc, layer);
        for (size_t e = elements.size(); e-- > 0;) {
            const Element& element = elements[e];
            if (!element.region.valid()) {
                continue;
            }
            auto inside = engine.regionContainsPoint(element.region, local);
            if (inside.fail() || !inside.value) {
                continue;
            }
            if (element.kind == ElementKind::Erase) {
                return false;              // rubbed out: nothing there to pick
            }
            // Whatever covers the pixel answers, even a shape or a dither: a
            // pixel under a rectangle is the rectangle's colour, not the ink
            // hidden beneath it. Only a solid fill has an ink to hand back.
            return inkOfElement(doc, element.fill, out);
        }
    }
    return false;
}

} // namespace fast
