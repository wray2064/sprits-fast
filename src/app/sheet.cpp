// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

#include "app/sheet.h"
#include "app/file_io.h"

#include <algorithm>
#include <cstdint>
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
    return { static_cast<int32_t>(border + static_cast<uint32_t>(column) * (cellWidth + spacing)),
             static_cast<int32_t>(border + static_cast<uint32_t>(row) * (cellHeight + spacing)) };
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

    if (settings.border > 1024 || settings.spacing > 1024) {
        return fail("a border or spacing that wide is not padding any more");
    }
    SheetPlan plan;
    plan.cells = frameCount;
    plan.cellWidth = cellWidth * settings.scale;
    plan.cellHeight = cellHeight * settings.scale;
    plan.border = settings.border;
    plan.spacing = settings.spacing;

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
    const uint64_t width  = static_cast<uint64_t>(plan.columns) * plan.cellWidth +
                            static_cast<uint64_t>(plan.columns - 1) * plan.spacing +
                            2ull * plan.border;
    const uint64_t height = static_cast<uint64_t>(plan.rows) * plan.cellHeight +
                            static_cast<uint64_t>(plan.rows - 1) * plan.spacing +
                            2ull * plan.border;
    if (width > kMaxCanvasDimension || height > kMaxCanvasDimension) {
        return fail("that sheet is longer than 16384 pixels on a side");
    }
    if (width * height > kMaxCanvasPixels) {
        return fail("that sheet is more pixels than Fast will write; "
                    "try fewer columns, a smaller scale, or one cycle at a time");
    }

    plan.width = static_cast<uint32_t>(width);
    plan.height = static_cast<uint32_t>(height);
    plan.sourceWidth = plan.cellWidth;
    plan.sourceHeight = plan.cellHeight;
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
    const uint32_t canvasWidth  = static_cast<uint32_t>(size.value.x);
    const uint32_t canvasHeight = static_cast<uint32_t>(size.value.y);

    // Every frame at the origin first: exactly what exporting it alone makes.
    std::vector<ls::RasterBuffer> cells;
    cells.reserve(frames.size());
    for (ls::SpriteId frame : frames) {
        auto compiled = doc.engine().compileSprite(
            frame, compileProfile(ls::CompileProfileType::Export, canvasWidth, canvasHeight));
        if (compiled.fail()) {
            return fail("a frame could not be compiled");
        }
        cells.push_back(std::move(compiled.value.raster));
    }

    // The part of the canvas the cells show: all of it, or -- trimmed -- the
    // smallest rectangle holding every frame's drawn pixels. A sheet of
    // nothing keeps the whole canvas rather than becoming zero pixels wide.
    ls::Rect2i keep { { 0, 0 }, { static_cast<int32_t>(canvasWidth),
                                  static_cast<int32_t>(canvasHeight) } };
    if (settings.trim) {
        ls::Rect2i found { { INT32_MAX, INT32_MAX }, { INT32_MIN, INT32_MIN } };
        bool any = false;
        for (const ls::RasterBuffer& cell : cells) {
            for (uint32_t y = 0; y < cell.height; ++y) {
                const uint8_t* row = cell.row(y);
                for (uint32_t x = 0; x < cell.width; ++x) {
                    if (row[x * 4 + 3] != 0) {
                        any = true;
                        found.min.x = std::min(found.min.x, static_cast<int32_t>(x));
                        found.min.y = std::min(found.min.y, static_cast<int32_t>(y));
                        found.max.x = std::max(found.max.x, static_cast<int32_t>(x) + 1);
                        found.max.y = std::max(found.max.y, static_cast<int32_t>(y) + 1);
                    }
                }
            }
        }
        if (any) {
            keep = found;
        }
    }
    const uint32_t cellWidth = static_cast<uint32_t>(keep.width());
    const uint32_t cellHeight = static_cast<uint32_t>(keep.height());

    if (!planSheet(static_cast<int>(frames.size()), cellWidth, cellHeight,
                   settings, plan, error)) {
        return false;
    }
    if (settings.trim) {
        plan->trimmed = true;
        plan->trimX = keep.min.x * static_cast<int32_t>(settings.scale);
        plan->trimY = keep.min.y * static_cast<int32_t>(settings.scale);
        plan->sourceWidth = canvasWidth * settings.scale;
        plan->sourceHeight = canvasHeight * settings.scale;
    }

    ls::RasterBuffer sheet = ls::makeRaster(plan->width, plan->height);
    if (sheet.empty()) {
        return fail("there was not enough memory for a sheet that size");
    }

    for (size_t i = 0; i < frames.size(); ++i) {
        // Asked for one lattice across the whole sheet: compiled again with
        // its export origin where its first kept pixel lands, which is the
        // thing this option is. Otherwise the origin compile stands, so a
        // cell is exactly a single-frame export.
        if (settings.patternAcrossSheet) {
            const int column = static_cast<int>(i) % plan->columns;
            const int row = static_cast<int>(i) / plan->columns;
            ls::CompileProfile profile =
                compileProfile(ls::CompileProfileType::Export, canvasWidth, canvasHeight);
            profile.exportOrigin = {
                static_cast<int32_t>(static_cast<uint32_t>(column) * cellWidth) - keep.min.x,
                static_cast<int32_t>(static_cast<uint32_t>(row) * cellHeight) - keep.min.y };
            auto compiled = doc.engine().compileSprite(frames[i], profile);
            if (compiled.fail()) {
                return fail("a frame could not be compiled");
            }
            cells[i] = std::move(compiled.value.raster);
        }
        const ls::RasterBuffer& cell = cells[i];

        const ls::Vec2i at = plan->positionOf(static_cast<int>(i));
        for (uint32_t y = 0; y < cellHeight; ++y) {
            for (uint32_t x = 0; x < cellWidth; ++x) {
                const ls::Color pixel = ls::readPixel(
                    cell, keep.min.x + static_cast<int32_t>(x), keep.min.y + static_cast<int32_t>(y));
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

namespace {

// A rectangle in the manifest's words, scaled.
std::string rectJson(ls::Rect2i r, uint32_t scale, const char* w, const char* h) {
    const int32_t k = static_cast<int32_t>(scale);
    return "{ \"x\": " + std::to_string(r.min.x * k) + ", \"y\": " + std::to_string(r.min.y * k) +
           ", \"" + w + "\": " + std::to_string(r.width() * k) + ", \"" + h + "\": " +
           std::to_string(r.height() * k) + " }";
}

std::string pointJson(ls::Vec2i p, uint32_t scale) {
    const int32_t k = static_cast<int32_t>(scale);
    return "{ \"x\": " + std::to_string(p.x * k) + ", \"y\": " + std::to_string(p.y * k) + " }";
}

std::string hexColour(ls::Color c) {
    char text[16];
    std::snprintf(text, sizeof(text), "#%02x%02x%02xff", c.r, c.g, c.b);
    return text;
}

} // namespace

std::string sheetManifest(const SheetPlan& plan, const std::vector<Frame>& frames,
                          const std::vector<int>& steps,
                          const std::vector<Cycle>& cycles,
                          const std::string& imageName, uint32_t scale,
                          const std::vector<Slice>& slices) {
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
    if (plan.trimmed) {
        out += "  \"trim\": { \"x\": " + std::to_string(plan.trimX) +
               ", \"y\": " + std::to_string(plan.trimY) + " },\n";
        out += "  \"source\": { \"width\": " + std::to_string(plan.sourceWidth) +
               ", \"height\": " + std::to_string(plan.sourceHeight) + " },\n";
    }
    if (plan.border > 0 || plan.spacing > 0) {
        out += "  \"border\": " + std::to_string(plan.border) + ",\n";
        out += "  \"spacing\": " + std::to_string(plan.spacing) + ",\n";
    }

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

    // Slices, in the canvas's pixels at the sheet's scale.
    if (!slices.empty()) {
        out += ",\n  \"slices\": [\n";
        for (size_t i = 0; i < slices.size(); ++i) {
            const Slice& s = slices[i];
            out += "    { \"name\": " + quoted(s.name) + ", \"bounds\": " +
                   rectJson(s.bounds, scale, "width", "height");
            if (s.nine) {
                out += ", \"center\": " + rectJson(s.centre, scale, "width", "height");
            }
            if (s.hasPivot) {
                out += ", \"pivot\": " + pointJson(s.pivot, scale);
            }
            out += " }";
            out += (i + 1 < slices.size()) ? ",\n" : "\n";
        }
        out += "  ]";
    }

    out += "\n}\n";
    return out;
}

std::string asepriteManifest(const SheetPlan& plan, const std::vector<Frame>& frames,
                             const std::vector<int>& steps, const std::vector<Cycle>& cycles,
                             const std::string& imageName, uint32_t scale, bool hash,
                             const std::vector<Slice>& slices) {
    // Frames are named "<image stem> <cell>", as Aseprite names them after
    // the file.
    std::string stem = imageName;
    const size_t dot = stem.find_last_of('.');
    if (dot != std::string::npos && dot > 0) {
        stem.erase(dot);
    }
    std::string out = "{ \"frames\": ";
    out += hash ? "{\n" : "[\n";
    for (size_t i = 0; i < steps.size(); ++i) {
        const int index = steps[i];
        const bool known = index >= 0 && static_cast<size_t>(index) < frames.size();
        const ls::Vec2i at = plan.positionOf(static_cast<int>(i));
        const std::string w = std::to_string(plan.cellWidth);
        const std::string h = std::to_string(plan.cellHeight);
        const std::string name = quoted(stem + " " + std::to_string(i));
        out += hash ? "  " + name + ": {\n" : "  {\n   \"filename\": " + name + ",\n";
        out += "   \"frame\": { \"x\": " + std::to_string(at.x) + ", \"y\": " +
               std::to_string(at.y) + ", \"w\": " + w + ", \"h\": " + h + " },\n";
        out += std::string("   \"rotated\": false,\n   \"trimmed\": ") +
               (plan.trimmed ? "true" : "false") + ",\n";
        out += "   \"spriteSourceSize\": { \"x\": " + std::to_string(plan.trimX) +
               ", \"y\": " + std::to_string(plan.trimY) + ", \"w\": " + w + ", \"h\": " + h +
               " },\n";
        out += "   \"sourceSize\": { \"w\": " + std::to_string(plan.sourceWidth) +
               ", \"h\": " + std::to_string(plan.sourceHeight) + " },\n";
        out += "   \"duration\": " +
               std::to_string(known ? frames[static_cast<size_t>(index)].durationMs
                                    : kDefaultFrameMs) + "\n  }";
        out += (i + 1 < steps.size()) ? ",\n" : "\n";
    }
    out += hash ? " },\n" : " ],\n";

    // A tag is a run of cells, so a cycle becomes one where its steps appear
    // as a run in the cell order -- which is every cycle when the sheet was
    // made from it, and every forward run of frames when it was made from all.
    std::string tags;
    for (const Cycle& cycle : cycles) {
        const size_t length = cycle.frames.size();
        if (length == 0 || length > steps.size()) {
            continue;
        }
        for (size_t start = 0; start + length <= steps.size(); ++start) {
            if (!std::equal(cycle.frames.begin(), cycle.frames.end(), steps.begin() +
                            static_cast<long long>(start))) {
                continue;
            }
            if (!tags.empty()) {
                tags += ",\n";
            }
            tags += "   { \"name\": " + quoted(cycle.name) + ", \"from\": " +
                    std::to_string(start) + ", \"to\": " + std::to_string(start + length - 1) +
                    ", \"direction\": " +
                    quoted(cycle.loop == LoopMode::PingPong ? "pingpong" : "forward");
            if (cycle.loop == LoopMode::Once) {
                tags += ", \"repeat\": \"1\"";
            }
            tags += ", \"color\": \"#000000ff\" }";
            break;
        }
    }
    out += " \"meta\": {\n";
    out += "  \"app\": \"Sprit's'fast\",\n  \"version\": \"sprits-sheet/1\",\n";
    out += "  \"image\": " + quoted(imageName) + ",\n";
    out += "  \"format\": \"RGBA8888\",\n";
    out += "  \"size\": { \"w\": " + std::to_string(plan.width) + ", \"h\": " +
           std::to_string(plan.height) + " },\n";
    out += "  \"scale\": \"" + std::to_string(scale) + "\",\n";
    out += "  \"frameTags\": [" + (tags.empty() ? std::string() : "\n" + tags + "\n  ") + "],\n";
    // Aseprite's slices: one key, from the first frame, as a slice that does
    // not move through the animation.
    std::string sliceText;
    for (size_t i = 0; i < slices.size(); ++i) {
        const Slice& s = slices[i];
        sliceText += "   { \"name\": " + quoted(s.name) + ", \"color\": \"" + hexColour(s.colour) +
                     "\", \"keys\": [{ \"frame\": 0, \"bounds\": " + rectJson(s.bounds, scale, "w", "h");
        if (s.nine) {
            sliceText += ", \"center\": " + rectJson(s.centre, scale, "w", "h");
        }
        if (s.hasPivot) {
            sliceText += ", \"pivot\": " + pointJson(s.pivot, scale);
        }
        sliceText += " }] }";
        sliceText += (i + 1 < slices.size()) ? ",\n" : "\n";
    }
    out += "  \"layers\": [],\n  \"slices\": [" +
           (sliceText.empty() ? std::string() : "\n" + sliceText + "  ") + "]\n }\n}\n";
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
            settings.manifestFormat == SheetManifestFormat::Fast
                ? sheetManifest(plan, frames, steps, cycles, name, settings.scale, readSlices(doc))
                : asepriteManifest(plan, frames, steps, cycles, name, settings.scale,
                                   settings.manifestFormat == SheetManifestFormat::AsepriteHash,
                                   readSlices(doc));
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
