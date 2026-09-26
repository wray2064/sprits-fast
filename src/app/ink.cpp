// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

#include "app/ink.h"
#include "app/shape.h"

#include "app/element.h"
#include "app/transform.h"

#include <algorithm>
#include <cmath>

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

// The freehand elements of a layer, and where the last shape sits among all of
// its elements -- an ink below that index would paint under the shape.
struct Survey {
    std::vector<Element> freehand;
    std::vector<size_t>  freehandIndex;    // position in the full element list
    long long            lastShape = -1;
};

Survey survey(Document& doc, ls::LayerId layer) {
    Survey out;
    const std::vector<Element> all = elementsOf(doc, layer);
    for (size_t i = 0; i < all.size(); ++i) {
        if (all[i].kind == ElementKind::Paint) {
            out.freehand.push_back(all[i]);
            out.freehandIndex.push_back(i);
        } else {
            out.lastShape = static_cast<long long>(i);
        }
    }
    return out;
}

bool fillOthers(const Survey& layout, ls::OperationId except, InkStroke* out) {
    out->others.clear();
    for (const Element& element : layout.freehand) {
        if (element.fill != except) {
            out->others.push_back(element.region);
        }
    }
    return true;
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

    const Survey layout = survey(doc, layer);

    // The topmost element of this ink, if nothing but pixels sits above it.
    for (size_t i = layout.freehand.size(); i-- > 0;) {
        if (static_cast<long long>(layout.freehandIndex[i]) < layout.lastShape) {
            break;
        }
        Ink found;
        if (inkOfElement(doc, layout.freehand[i].fill, &found) && found == ink) {
            out->target.layer = layer;
            out->target.fill = layout.freehand[i].fill;
            out->target.region = layout.freehand[i].region;
            return fillOthers(layout, out->target.fill, out);
        }
    }

    // None: a new element, at the top so the paint lands over everything.
    ls::LSContext& engine = doc.engine();
    auto region = engine.createRegionFromIntervals(doc.id(), ls::IntervalSet{});
    if (region.fail()) {
        return false;
    }
    ls::FillSolidOp fill;
    fill.targetRegion = region.value;
    fill.fallbackColor = ink.colour;
    fill.paletteRole = ink.role;
    auto op = engine.addOperation(layer, fill);
    if (op.fail()) {
        engine.deleteRegion(region.value);
        return false;
    }
    // New paint is drawn before any outline or shadow, so they see it.
    keepEffectsLast(doc, layer);
    out->target.layer = layer;
    out->target.fill = op.value;
    out->target.region = region.value;
    return fillOthers(layout, out->target.fill, out);
}

bool beginElementStroke(Document& doc, const PaintLayer& element, InkStroke* out) {
    if (out == nullptr || !element.drawable()) {
        return false;
    }
    *out = InkStroke{};
    out->layer = element.layer;
    out->target = element;
    return fillOthers(survey(doc, element.layer), element.fill, out);
}

bool beginEraseStroke(Document& doc, ls::LayerId layer, InkStroke* out) {
    if (out == nullptr || !layer.valid()) {
        return false;
    }
    *out = InkStroke{};
    out->layer = layer;
    return fillOthers(survey(doc, layer), ls::OperationId{}, out);
}

bool strokeInk(Document& doc, const InkStroke& stroke, const std::vector<ls::Vec2i>& pixels) {
    if (pixels.empty()) {
        return false;
    }
    ls::LSContext& engine = doc.engine();
    bool ok = true;
    if (!stroke.erasing()) {
        ok = paintPixels(doc, stroke.target, pixels);
    }
    // A pixel has one colour on a layer. Taking it out of the others is what
    // makes painting red over blue replace the blue rather than stack on it --
    // and keeps a stroke from being drawn twice where two inks would overlap.
    for (ls::RegionId region : stroke.others) {
        ok = engine.erasePixelsFromRegion(region, pixels).ok() && ok;
    }
    return ok;
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
        if (element.kind != ElementKind::Paint || element.fill == keep ||
            !isSolid(doc, element.fill)) {
            continue;
        }
        auto intervals = engine.getRegionIntervals(element.region);
        if (intervals.fail() || !intervals.value.empty()) {
            continue;
        }
        if (elementsOf(doc, layer).size() <= 1) {
            break;
        }
        if (engine.removeOperation(layer, element.fill).ok()) {
            engine.deleteRegion(element.region);
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
    ls::LSContext& engine = doc.engine();
    for (const Element& element : elementsOf(doc, layer)) {
        if (!element.region.valid()) {
            continue;
        }
        auto pixels = engine.getRegionIntervals(element.region);
        if (pixels.fail() || pixels.value.empty()) {
            continue;
        }
        Ink ink;
        const bool solid = element.kind == ElementKind::Paint &&
                           inkOfElement(doc, element.fill, &ink);
        switch (mode) {
            case InkMode::LockAlpha:
                // Where anything on the layer draws -- shapes too, since a
                // shape's pixels are the layer's pixels to the eye.
                out->allowed = ls::geom::unionSets(out->allowed, pixels.value);
                break;
            case InkMode::Replace:
                if (solid && ink == second) {
                    out->allowed = ls::geom::unionSets(out->allowed, pixels.value);
                }
                break;
            case InkMode::Shading:
                if (solid) {
                    out->held.push_back({ pixels.value, ink });
                }
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
            // Whatever covers the pixel answers, even a shape or a dither: a
            // pixel under a rectangle is the rectangle's colour, not the ink
            // hidden beneath it. Only a solid fill has an ink to hand back.
            return inkOfElement(doc, element.fill, out);
        }
    }
    return false;
}

} // namespace fast
