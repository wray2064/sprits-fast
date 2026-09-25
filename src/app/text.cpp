// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

#include "app/text.h"

#include "app/pixel_font.h"

#include <cstdlib>

namespace fast {

namespace {

constexpr const char* kTextKey = "fast.text";
constexpr const char* kTextAtKey = "fast.text.at";
constexpr const char* kTextScaleKey = "fast.text.scale";

ls::IntervalSet setOf(const std::vector<ls::Vec2i>& pixels) {
    ls::IntervalSet set;
    set.intervals.reserve(pixels.size());
    for (ls::Vec2i p : pixels) {
        set.intervals.push_back({ p.y, p.x, p.x + 1 });
    }
    return ls::geom::normalize(std::move(set));
}

bool remember(Document& doc, ls::RegionId region, const TextSpec& spec) {
    ls::LSContext& engine = doc.engine();
    const std::string at = std::to_string(spec.at.x) + "," + std::to_string(spec.at.y);
    return engine.setMetadata(region.value, kTextKey, spec.text).ok() &&
           engine.setMetadata(region.value, kTextAtKey, at).ok() &&
           engine.setMetadata(region.value, kTextScaleKey, std::to_string(spec.scale)).ok();
}

bool usable(const TextSpec& spec) {
    return !spec.text.empty() && spec.text.size() <= kMaxTextLength &&
           spec.scale >= 1 && spec.scale <= 16;
}

} // namespace

bool addTextElement(Document& doc, ls::LayerId layer, const TextSpec& spec, const Ink& ink,
                    PaintLayer* out) {
    if (out == nullptr || !usable(spec)) {
        return false;
    }
    ls::LSContext& engine = doc.engine();
    auto region = engine.createRegionFromIntervals(doc.id(),
                                                   setOf(layOutText(spec.text, spec.at, spec.scale)));
    if (region.fail()) {
        return false;
    }
    ls::FillSolidOp fill;
    fill.targetRegion = region.value;
    fill.fallbackColor = ink.colour;
    fill.paletteRole = ink.role;
    auto op = engine.addOperation(layer, fill);
    if (op.fail() || !remember(doc, region.value, spec)) {
        return false;
    }
    out->layer = layer;
    out->fill = op.value;
    out->region = region.value;
    return true;
}

bool readTextElement(Document& doc, ls::RegionId region, TextSpec* out) {
    if (out == nullptr || !region.valid()) {
        return false;
    }
    ls::LSContext& engine = doc.engine();
    auto text = engine.getMetadata(region.value, kTextKey);
    if (text.fail()) {
        return false;
    }
    TextSpec spec;
    spec.text = text.value;
    auto at = engine.getMetadata(region.value, kTextAtKey);
    if (at.ok()) {
        const char* p = at.value.c_str();
        char* end = nullptr;
        spec.at.x = static_cast<int32_t>(std::strtol(p, &end, 10));
        if (end != nullptr && *end == ',') {
            spec.at.y = static_cast<int32_t>(std::strtol(end + 1, nullptr, 10));
        }
    }
    auto scale = engine.getMetadata(region.value, kTextScaleKey);
    if (scale.ok()) {
        spec.scale = std::max(1, std::min(16, static_cast<int>(std::strtol(scale.value.c_str(),
                                                                           nullptr, 10))));
    }
    *out = spec;
    return true;
}

bool updateTextElement(Document& doc, ls::RegionId region, const TextSpec& spec) {
    if (!usable(spec)) {
        return false;
    }
    return doc.engine().setRegionIntervals(region,
                                           setOf(layOutText(spec.text, spec.at, spec.scale))).ok() &&
           remember(doc, region, spec);
}

} // namespace fast
