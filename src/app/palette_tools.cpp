// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

#include "app/palette_tools.h"

#include "app/element.h"
#include "app/ink.h"

#include <algorithm>
#include <cmath>

namespace fast {

namespace {

// The next role no palette in the document uses. Roles mean the same thing
// in every palette -- slot 5 of "day" is slot 5 of "night" -- so a new one
// must not collide with a slot another palette already has.
ls::ColorRole nextFreeRole(Document& doc) {
    ls::ColorRole next = 0;
    for (const PaletteInfo& info : listPalettes(doc)) {
        for (const PaletteEntry& entry : paletteEntries(doc, info.id)) {
            if (entry.role != ls::kColorRoleNone && entry.role >= next) {
                next = entry.role + 1;
            }
        }
    }
    return next;
}

std::vector<ls::ColorRole> orderOf(const std::vector<PaletteEntry>& entries) {
    std::vector<ls::ColorRole> order;
    order.reserve(entries.size());
    for (const PaletteEntry& entry : entries) {
        order.push_back(entry.role);
    }
    return order;
}

float wrapHue(float h) {
    h = std::fmod(h, 360.f);
    return h < 0.f ? h + 360.f : h;
}

} // namespace

void rgbToHsl(ls::Color c, float* h, float* s, float* l) {
    const float r = static_cast<float>(c.r) / 255.f;
    const float g = static_cast<float>(c.g) / 255.f;
    const float b = static_cast<float>(c.b) / 255.f;
    const float max = std::max({ r, g, b });
    const float min = std::min({ r, g, b });
    const float delta = max - min;
    *l = (max + min) * 0.5f;
    if (delta <= 0.f) {
        *h = 0.f;
        *s = 0.f;
        return;
    }
    *s = *l > 0.5f ? delta / (2.f - max - min) : delta / (max + min);
    float hue = 0.f;
    if (max == r) {
        hue = (g - b) / delta + (g < b ? 6.f : 0.f);
    } else if (max == g) {
        hue = (b - r) / delta + 2.f;
    } else {
        hue = (r - g) / delta + 4.f;
    }
    *h = hue * 60.f;
}

ls::Color hslToRgb(float h, float s, float l, uint8_t alpha) {
    s = std::clamp(s, 0.f, 1.f);
    l = std::clamp(l, 0.f, 1.f);
    h = wrapHue(h) / 360.f;
    const auto channel = [](float p, float q, float t) {
        if (t < 0.f) { t += 1.f; }
        if (t > 1.f) { t -= 1.f; }
        if (t < 1.f / 6.f) { return p + (q - p) * 6.f * t; }
        if (t < 0.5f) { return q; }
        if (t < 2.f / 3.f) { return p + (q - p) * (2.f / 3.f - t) * 6.f; }
        return p;
    };
    float r = l, g = l, b = l;
    if (s > 0.f) {
        const float q = l < 0.5f ? l * (1.f + s) : l + s - l * s;
        const float p = 2.f * l - q;
        r = channel(p, q, h + 1.f / 3.f);
        g = channel(p, q, h);
        b = channel(p, q, h - 1.f / 3.f);
    }
    const auto byte = [](float v) {
        return static_cast<uint8_t>(std::clamp(std::lround(v * 255.f), 0L, 255L));
    };
    return ls::Color{ byte(r), byte(g), byte(b), alpha };
}

bool setSlotOrder(Document& doc, ls::PaletteId palette, const std::vector<ls::ColorRole>& order) {
    return palette.valid() && doc.engine().setPaletteOrder(palette, order).ok();
}

bool sortPalette(Document& doc, ls::PaletteId palette, PaletteSort by) {
    std::vector<PaletteEntry> entries = paletteEntries(doc, palette);
    if (entries.size() < 2) {
        return false;
    }
    if (by == PaletteSort::Reverse) {
        std::reverse(entries.begin(), entries.end());
    } else {
        const auto key = [by](const PaletteEntry& entry) {
            float h, s, l;
            rgbToHsl(entry.color, &h, &s, &l);
            // Greys have no hue worth sorting by; they go first, dark to light,
            // rather than scattering among the reds where hue 0 would put them.
            if (by == PaletteSort::Hue) {
                return s < 0.05f ? -1.f + l : h;
            }
            return by == PaletteSort::Saturation ? s : l;
        };
        std::stable_sort(entries.begin(), entries.end(),
                         [&](const PaletteEntry& a, const PaletteEntry& b) {
                             return key(a) < key(b);
                         });
    }
    return setSlotOrder(doc, palette, orderOf(entries));
}

bool moveSlot(Document& doc, ls::PaletteId palette, ls::ColorRole role, int index) {
    std::vector<ls::ColorRole> order = orderOf(paletteEntries(doc, palette));
    const auto at = std::find(order.begin(), order.end(), role);
    if (at == order.end()) {
        return false;
    }
    order.erase(at);
    index = std::clamp(index, 0, static_cast<int>(order.size()));
    order.insert(order.begin() + index, role);
    return setSlotOrder(doc, palette, order);
}

std::vector<ls::ColorRole> addRampBetween(Document& doc, ls::PaletteId palette,
                                          ls::ColorRole from, ls::ColorRole to, int steps) {
    std::vector<ls::ColorRole> made;
    if (steps < 1 || from == to) {
        return made;
    }
    const std::vector<PaletteEntry> entries = paletteEntries(doc, palette);
    const PaletteEntry* a = nullptr;
    const PaletteEntry* b = nullptr;
    for (const PaletteEntry& entry : entries) {
        if (entry.role == from) { a = &entry; }
        if (entry.role == to) { b = &entry; }
    }
    if (a == nullptr || b == nullptr) {
        return made;
    }
    ls::ColorRole next = nextFreeRole(doc);
    for (int i = 1; i <= steps; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(steps + 1);
        const auto mix = [t](uint8_t x, uint8_t y) {
            return static_cast<uint8_t>(std::lround(static_cast<float>(x) +
                                                   (static_cast<float>(y) - static_cast<float>(x)) * t));
        };
        const ls::Color colour { mix(a->color.r, b->color.r), mix(a->color.g, b->color.g),
                                 mix(a->color.b, b->color.b), mix(a->color.a, b->color.a) };
        if (!setPaletteEntry(doc, palette, next, colour)) {
            break;
        }
        made.push_back(next++);
    }
    // Between the two ends in the display order, where a ramp belongs.
    std::vector<ls::ColorRole> order = orderOf(paletteEntries(doc, palette));
    for (ls::ColorRole role : made) {
        order.erase(std::find(order.begin(), order.end(), role));
    }
    auto at = std::find(order.begin(), order.end(), from);
    const auto end = std::find(order.begin(), order.end(), to);
    // After `from` when `to` follows it, before it when `to` comes first, so
    // the ramp reads from one end to the other whichever way round they sit.
    std::vector<ls::ColorRole> ramp = made;
    if (end < at) {
        std::reverse(ramp.begin(), ramp.end());
    } else {
        ++at;
    }
    order.insert(at, ramp.begin(), ramp.end());
    setSlotOrder(doc, palette, order);
    return made;
}

bool adjustPalette(Document& doc, ls::PaletteId palette, const std::vector<PaletteEntry>& base,
                   float hueDegrees, float saturation, float lightness) {
    bool ok = true;
    for (const PaletteEntry& entry : base) {
        float h, s, l;
        rgbToHsl(entry.color, &h, &s, &l);
        // Saturation and lightness move toward their ends in proportion to
        // the room left, so a +0.5 lightness halves the distance to white
        // for every slot rather than pushing the light ones off the top.
        s = saturation >= 0.f ? s + (1.f - s) * saturation : s * (1.f + saturation);
        l = lightness >= 0.f ? l + (1.f - l) * lightness : l * (1.f + lightness);
        const ls::Color colour = hslToRgb(h + hueDegrees, s, l, entry.color.a);
        ok = setPaletteEntry(doc, palette, entry.role, colour) && ok;
    }
    return ok;
}

int slotsFromColours(Document& doc, ls::PaletteId palette) {
    if (!palette.valid()) {
        return 0;
    }
    ls::LSContext& engine = doc.engine();
    auto info = engine.getDocumentInfo(doc.id());
    if (info.fail()) {
        return 0;
    }
    int converted = 0;
    ls::ColorRole next = nextFreeRole(doc);
    for (ls::SpriteId sprite : info.value.sprites) {
        auto spriteInfo = engine.getSpriteInfo(sprite);
        if (spriteInfo.fail()) {
            continue;
        }
        for (ls::LayerId layer : spriteInfo.value.layers) {
            for (const Element& element : elementsOf(doc, layer)) {
                Ink ink;
                if (!inkOfElement(doc, element.fill, &ink) || ink.usesSlot()) {
                    continue;
                }
                // An element that draws nothing is not a colour anyone used,
                // and a slot made for it would be a swatch nobody can explain.
                if (element.region.valid()) {
                    auto pixels = engine.getRegionIntervals(element.region);
                    if (pixels.ok() && pixels.value.empty()) {
                        continue;
                    }
                }
                // An existing slot of exactly this colour, or a new one.
                ls::ColorRole role = ls::kColorRoleNone;
                for (const PaletteEntry& entry : paletteEntries(doc, palette)) {
                    if (entry.color.r == ink.colour.r && entry.color.g == ink.colour.g &&
                        entry.color.b == ink.colour.b && entry.color.a == ink.colour.a) {
                        role = entry.role;
                        break;
                    }
                }
                if (role == ls::kColorRoleNone) {
                    if (!setPaletteEntry(doc, palette, next, ink.colour)) {
                        continue;
                    }
                    role = next++;
                }
                ink.role = role;
                if (setElementInk(doc, element.fill, ink)) {
                    ++converted;
                }
            }
        }
    }
    return converted;
}

const std::vector<PalettePreset>& palettePresets() {
    static const std::vector<PalettePreset> presets = [] {
        std::vector<PalettePreset> out;

        // Greys, evenly spaced: the palette every value study starts from.
        for (int count : { 4, 8, 16 }) {
            PalettePreset greys;
            greys.name = "Greys " + std::to_string(count);
            for (int i = 0; i < count; ++i) {
                const uint8_t v = static_cast<uint8_t>(std::lround(255.f * static_cast<float>(i) /
                                                                   static_cast<float>(count - 1)));
                greys.colours.push_back({ v, v, v, 255 });
            }
            out.push_back(greys);
        }

        // One bit: ink and paper.
        out.push_back({ "One bit", { { 16, 14, 20, 255 }, { 236, 232, 222, 255 } } });

        // Four greens in the manner of an early handheld's screen, drawn up
        // for Fast rather than measured off one.
        out.push_back({ "Handheld green", { { 22, 40, 26, 255 }, { 58, 94, 54, 255 },
                                            { 128, 164, 76, 255 }, { 200, 214, 138, 255 } } });

        // Twelve hues, each in three values -- shadow, base, light -- with the
        // shadow turned toward blue and the light toward yellow, the way pixel
        // artists shade, plus a black and a white. Generated, so it is
        // nobody's but this program's.
        PalettePreset ramps;
        ramps.name = "Hue ramps 38";
        ramps.colours.push_back({ 14, 12, 20, 255 });
        ramps.colours.push_back({ 244, 240, 232, 255 });
        for (int i = 0; i < 12; ++i) {
            const float hue = static_cast<float>(i) * 30.f;
            // Toward 240 (blue) for shadows, toward 60 (yellow) for lights,
            // by the short way round the wheel.
            const auto toward = [](float from, float target, float amount) {
                float d = std::fmod(target - from + 540.f, 360.f) - 180.f;
                return from + d * amount;
            };
            ramps.colours.push_back(hslToRgb(toward(hue, 240.f, 0.15f), 0.55f, 0.28f, 255));
            ramps.colours.push_back(hslToRgb(hue, 0.65f, 0.50f, 255));
            ramps.colours.push_back(hslToRgb(toward(hue, 60.f, 0.15f), 0.75f, 0.74f, 255));
        }
        out.push_back(ramps);

        // A small working palette of warm and cool neutrals and a few accents,
        // chosen for Fast: skin, hair, foliage, metal, water, fire.
        out.push_back({ "Fast 16", {
            { 20, 16, 28, 255 },   { 58, 50, 72, 255 },   { 104, 96, 112, 255 }, { 168, 160, 164, 255 },
            { 240, 234, 220, 255 }, { 120, 64, 48, 255 },  { 196, 120, 84, 255 },  { 240, 184, 140, 255 },
            { 60, 92, 52, 255 },   { 112, 160, 72, 255 },  { 44, 72, 128, 255 },   { 84, 148, 208, 255 },
            { 168, 48, 56, 255 },  { 232, 112, 48, 255 },  { 248, 208, 88, 255 },  { 132, 76, 148, 255 } } });
        return out;
    }();
    return presets;
}

} // namespace fast
