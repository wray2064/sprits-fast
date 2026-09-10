// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

#include "app/sheet.h"
#include "app/file_io.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace fast {
namespace {

// JSON string escaping. A frame name is whatever somebody typed, and a manifest
// that a quotation mark can break is a manifest no consumer can trust.
std::string quoted(const std::string& text) {
    std::string out = "\"";
    for (unsigned char c : text) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (c < 0x20) {
                    char buffer[8];
                    std::snprintf(buffer, sizeof(buffer), "\\u%04x", c);
                    out += buffer;
                } else {
                    // Everything else, including UTF-8, passes through: JSON is
                    // UTF-8, and escaping it would only mangle names that are
                    // not English.
                    out.push_back(static_cast<char>(c));
                }
                break;
        }
    }
    out += '"';
    return out;
}

// "art/hero.png" -> "art/hero.json".
//
// Not file_io's withExtension, which appends and would give "hero.png.json".
// A sheet's description sits beside it under the same name, which is what every
// consumer of one expects to find. Only the part after the last separator is
// considered, so a dot in a folder name is not mistaken for an extension.
std::string besideAsJson(const std::string& path) {
    const size_t slash = path.find_last_of("/\\");
    const size_t start = slash == std::string::npos ? 0 : slash + 1;
    const size_t dot = path.find_last_of('.');
    if (dot == std::string::npos || dot < start || dot == start) {
        return path + ".json";      // no extension, or a name that is all suffix
    }
    return path.substr(0, dot) + ".json";
}

const char* loopWord(LoopMode mode) {
    switch (mode) {
        case LoopMode::Loop:     return "loop";
        case LoopMode::Once:     return "once";
        case LoopMode::PingPong: return "pingpong";
    }
    return "loop";
}

// The arrangement closest to square, which keeps a forty-frame animation from
// becoming a 40-cell-wide image that no viewer opens at a useful size.
int nearSquareColumns(int count) {
    if (count <= 1) {
        return 1;
    }
    const int guess = static_cast<int>(std::ceil(std::sqrt(static_cast<double>(count))));
    return std::max(1, guess);
}

} // namespace

ls::Vec2i SheetPlan::positionOf(int index) const {
    if (columns <= 0 || index < 0) {
        return { 0, 0 };
    }
    const int column = index % columns;
    const int row = index / columns;
    return { static_cast<int32_t>(static_cast<uint32_t>(column) * cellWidth),
             static_cast<int32_t>(static_cast<uint32_t>(row) * cellHeight) };
}

bool planSheet(int frameCount, uint32_t cellWidth, uint32_t cellHeight,
               const SheetSettings& settings, SheetPlan* out, std::string* error) {
    const auto fail = [&](const char* why) {
        if (error != nullptr) { *error = why; }
        return false;
    };
    if (out == nullptr) {
        return false;
    }
    if (frameCount <= 0) {
        return fail("there are no frames to lay out");
    }
    if (cellWidth == 0 || cellHeight == 0) {
        return fail("the canvas has no size");
    }
    if (settings.scale == 0 || settings.scale > ExportSettings::kMaxScale) {
        return fail("that scale is outside 1x to 64x");
    }

    SheetPlan plan;
    plan.cells = frameCount;
    plan.cellWidth = cellWidth * settings.scale;
    plan.cellHeight = cellHeight * settings.scale;

    switch (settings.layout) {
        case SheetLayout::Row:    plan.columns = frameCount; break;
        case SheetLayout::Column: plan.columns = 1; break;
        case SheetLayout::Grid:
        default:
            plan.columns = settings.columns > 0
                ? std::min(settings.columns, frameCount)
                : nearSquareColumns(frameCount);
            break;
    }
    plan.rows = (frameCount + plan.columns - 1) / plan.columns;

    // In 64-bit, so the check happens before the multiplication that would
    // overflow. An image is not a canvas, but it is still something somebody
    // has to open, and the same policy bounds are the right ones.
    const uint64_t width  = static_cast<uint64_t>(plan.columns) * plan.cellWidth;
    const uint64_t height = static_cast<uint64_t>(plan.rows) * plan.cellHeight;
    if (width > kMaxCanvasDimension || height > kMaxCanvasDimension) {
        return fail("that sheet is longer than 16384 pixels on a side");
    }
    if (width * height > kMaxCanvasPixels) {
        return fail("that sheet is more pixels than Fast will write; "
                    "try fewer columns, a smaller scale, or one cycle at a time");
    }

    plan.width = static_cast<uint32_t>(width);
    plan.height = static_cast<uint32_t>(height);
    *out = plan;
    return true;
}

bool composeSheet(Document& doc, const std::vector<ls::SpriteId>& frames,
                  const SheetSettings& settings, ls::RasterBuffer* out,
                  SheetPlan* plan, std::string* error) {
    const auto fail = [&](const char* why) {
        if (error != nullptr) { *error = why; }
        return false;
    };
    if (out == nullptr || plan == nullptr) {
        return false;
    }

    auto size = doc.engine().getCanvasSize(doc.id());
    if (size.fail() || size.value.x <= 0 || size.value.y <= 0) {
        return fail("the document has no canvas");
    }
    const uint32_t cellWidth  = static_cast<uint32_t>(size.value.x);
    const uint32_t cellHeight = static_cast<uint32_t>(size.value.y);

    if (!planSheet(static_cast<int>(frames.size()), cellWidth, cellHeight,
                   settings, plan, error)) {
        return false;
    }

    ls::RasterBuffer sheet = ls::makeRaster(plan->width, plan->height);
    if (sheet.empty()) {
        return fail("there was not enough memory for a sheet that size");
    }

    for (size_t i = 0; i < frames.size(); ++i) {
        ls::CompileProfile profile;
        profile.type = ls::CompileProfileType::Export;   // never what is on screen
        profile.outputWidth = cellWidth;
        profile.outputHeight = cellHeight;
        profile.palette = ls::PalettePolicy::Unconstrained;

        // The default is the origin, so a cell is exactly what exporting this
        // frame on its own would have produced. Asking for the other thing --
        // one lattice across the whole sheet -- is what this option is.
        if (settings.patternAcrossSheet) {
            const int column = static_cast<int>(i) % plan->columns;
            const int row = static_cast<int>(i) / plan->columns;
            profile.exportOrigin = {
                static_cast<int32_t>(static_cast<uint32_t>(column) * cellWidth),
                static_cast<int32_t>(static_cast<uint32_t>(row) * cellHeight) };
        }

        auto compiled = doc.engine().compileSprite(frames[i], profile);
        if (compiled.fail()) {
            return fail("a frame could not be compiled");
        }
        const ls::RasterBuffer& cell = compiled.value.raster;

        const ls::Vec2i at = plan->positionOf(static_cast<int>(i));
        for (uint32_t y = 0; y < cellHeight; ++y) {
            for (uint32_t x = 0; x < cellWidth; ++x) {
                const ls::Color pixel = ls::readPixel(cell, static_cast<int32_t>(x),
                                                      static_cast<int32_t>(y));
                // Scaling is pixel duplication, never interpolation: a sprite
                // at 4x is four identical pixels a side or it is not pixel art
                // any more.
                for (uint32_t sy = 0; sy < settings.scale; ++sy) {
                    for (uint32_t sx = 0; sx < settings.scale; ++sx) {
                        ls::writePixel(sheet,
                            at.x + static_cast<int32_t>(x * settings.scale + sx),
                            at.y + static_cast<int32_t>(y * settings.scale + sy),
                            pixel);
                    }
                }
            }
        }
    }

    *out = std::move(sheet);
    return true;
}

std::string sheetManifest(const SheetPlan& plan, const std::vector<Frame>& frames,
                          const std::vector<int>& steps,
                          const std::vector<Cycle>& cycles,
                          const std::string& imageName, uint32_t scale) {
    std::string out;
    out.reserve(512 + steps.size() * 96);

    // Versioned from the first line, because a consumer that cannot tell which
    // shape it is reading has to guess, and this format is young enough to
    // change.
    out += "{\n  \"format\": \"sprits-sheet/1\",\n";
    out += "  \"image\": " + quoted(imageName) + ",\n";
    out += "  \"scale\": " + std::to_string(scale) + ",\n";
    out += "  \"columns\": " + std::to_string(plan.columns) + ",\n";
    out += "  \"rows\": " + std::to_string(plan.rows) + ",\n";
    out += "  \"cell\": { \"width\": " + std::to_string(plan.cellWidth) +
           ", \"height\": " + std::to_string(plan.cellHeight) + " },\n";
    out += "  \"size\": { \"width\": " + std::to_string(plan.width) +
           ", \"height\": " + std::to_string(plan.height) + " },\n";

    // One entry per cell, in the order they are laid out -- so playing the
    // animation is stepping through this array, which is all most consumers
    // want to do.
    out += "  \"cells\": [\n";
    for (size_t i = 0; i < steps.size(); ++i) {
        const int index = steps[i];
        const bool known = index >= 0 && static_cast<size_t>(index) < frames.size();
        const ls::Vec2i at = plan.positionOf(static_cast<int>(i));

        out += "    { \"frame\": " + std::to_string(index);
        out += ", \"x\": " + std::to_string(at.x);
        out += ", \"y\": " + std::to_string(at.y);
        out += ", \"durationMs\": " +
               std::to_string(known ? frames[static_cast<size_t>(index)].durationMs
                                    : kDefaultFrameMs);
        if (known && !frames[static_cast<size_t>(index)].name.empty()) {
            out += ", \"name\": " + quoted(frames[static_cast<size_t>(index)].name);
        }
        out += " }";
        out += (i + 1 < steps.size()) ? ",\n" : "\n";
    }
    out += "  ]";

    if (!cycles.empty()) {
        out += ",\n  \"cycles\": [\n";
        for (size_t i = 0; i < cycles.size(); ++i) {
            const Cycle& cycle = cycles[i];
            out += "    { \"name\": " + quoted(cycle.name);
            out += ", \"loop\": " + quoted(loopWord(cycle.loop));
            out += ", \"frames\": [";
            for (size_t s = 0; s < cycle.frames.size(); ++s) {
                out += std::to_string(cycle.frames[s]);
                if (s + 1 < cycle.frames.size()) {
                    out += ", ";
                }
            }
            out += "] }";
            out += (i + 1 < cycles.size()) ? ",\n" : "\n";
        }
        out += "  ]";
    }

    out += "\n}\n";
    return out;
}

bool exportSheetToPng(Document& doc, const std::vector<Frame>& frames,
                      const std::vector<int>& steps,
                      const std::vector<Cycle>& cycles,
                      const std::string& path, const SheetSettings& settings,
                      std::string* error) {
    if (steps.empty()) {
        if (error != nullptr) { *error = "there are no frames to write"; }
        return false;
    }

    // The sprites, in the order the cells go. A step naming a frame that does
    // not exist is refused rather than written as a hole: a sheet with a gap in
    // it is worse than a sheet that was not written.
    std::vector<ls::SpriteId> sprites;
    sprites.reserve(steps.size());
    for (int index : steps) {
        if (index < 0 || static_cast<size_t>(index) >= frames.size()) {
            if (error != nullptr) { *error = "a step names a frame that is not there"; }
            return false;
        }
        sprites.push_back(frames[static_cast<size_t>(index)].sprite);
    }

    ls::RasterBuffer sheet;
    SheetPlan plan;
    if (!composeSheet(doc, sprites, settings, &sheet, &plan, error)) {
        return false;
    }

    // The raster is already scaled, so the encoder must not scale it again.
    ExportSettings encode;
    encode.scale = 1;
    std::vector<uint8_t> png;
    if (!encodeRasterToPng(sheet, encode, &png, error)) {
        return false;
    }
    if (!writeFileAtomic(path, png, error)) {
        return false;
    }

    if (settings.writeManifest) {
        const std::string name = fileName(path);
        const std::string json =
            sheetManifest(plan, frames, steps, cycles, name, settings.scale);
        const std::vector<uint8_t> bytes(json.begin(), json.end());

        // Beside the image and named after it. A failure here is reported, but
        // the image stays: the picture is the thing that was asked for, and
        // unwriting it to punish a missing sidecar helps nobody.
        std::string manifestError;
        if (!writeFileAtomic(besideAsJson(path), bytes, &manifestError)) {
            if (error != nullptr) {
                *error = "the sheet was written, but its description was not: " +
                         manifestError;
            }
            return false;
        }
    }
    return true;
}

} // namespace fast
