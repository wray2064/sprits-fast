// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

#include "app/batch.h"

#include "app/animation.h"
#include "app/document.h"
#include "app/export_anim.h"
#include "app/export_png.h"
#include "app/file_io.h"
#include "app/image_io.h"
#include "app/import_aseprite.h"
#include "app/import_image.h"
#include "app/palette_io.h"
#include "app/sheet.h"

#include <cstdlib>

namespace fast {

namespace {

bool number(const std::string& text, long long low, long long high, long long* out) {
    if (text.empty()) {
        return false;
    }
    char* end = nullptr;
    const long long value = std::strtoll(text.c_str(), &end, 10);
    if (end == nullptr || *end != '\0' || value < low || value > high) {
        return false;
    }
    *out = value;
    return true;
}

// Anything Open takes.
bool openAnything(Document& doc, const std::string& path, std::string* error) {
    if (looksLikeAsepriteName(path)) {
        return openAsepriteAsDocument(doc, path, nullptr, error);
    }
    if (looksLikeImageName(path)) {
        return openImageAsDocument(doc, path, nullptr, error);
    }
    return doc.open(path, error);
}

} // namespace

bool parseBatch(const std::vector<std::string>& args, BatchJob* out, std::string* error) {
    if (out == nullptr) {
        return false;
    }
    bool batch = false;
    BatchJob job;
    const auto fail = [&](const std::string& why) {
        if (error != nullptr) { *error = why; }
        return false;
    };
    for (size_t i = 0; i < args.size(); ++i) {
        const std::string& arg = args[i];
        const bool hasValue = i + 1 < args.size();
        if (arg == "--export") {
            batch = true;
            if (!hasValue) { return fail("--export needs the file to read"); }
            job.input = args[++i];
        } else if (arg == "--to") {
            if (!hasValue) { return fail("--to needs the file to write"); }
            job.output = args[++i];
        } else if (arg == "--scale") {
            long long value = 0;
            if (!hasValue || !number(args[++i], 1, ExportSettings::kMaxScale, &value)) {
                return fail("--scale takes a whole number from 1 to " +
                            std::to_string(ExportSettings::kMaxScale));
            }
            job.scale = static_cast<uint32_t>(value);
        } else if (arg == "--frame") {
            long long value = 0;
            if (!hasValue || !number(args[++i], 1, static_cast<long long>(kMaxFrames), &value)) {
                return fail("--frame takes a frame number, counted from 1");
            }
            job.frame = static_cast<int>(value);
        } else if (arg == "--cycle") {
            if (!hasValue) { return fail("--cycle needs a cycle's name"); }
            job.cycle = args[++i];
        } else if (arg == "--border" || arg == "--spacing") {
            long long value = 0;
            if (!hasValue || !number(args[++i], 0, 1024, &value)) {
                return fail(arg + " takes a number of pixels");
            }
            (arg == "--border" ? job.border : job.spacing) = static_cast<uint32_t>(value);
        } else if (arg == "--json") {
            if (!hasValue || (args[i + 1] != "hash" && args[i + 1] != "array")) {
                return fail("--json takes hash or array, for Aseprite's layouts");
            }
            job.json = args[++i];
        } else if (arg == "--sheet") {
            job.sheet = true;
        } else if (arg == "--sequence") {
            job.sequence = true;
        } else if (arg == "--animated") {
            job.animated = true;
        }
    }
    if (!batch) {
        return false;
    }
    if (job.output.empty()) {
        return fail("say where to write with --to");
    }
    if (static_cast<int>(job.sheet) + static_cast<int>(job.sequence) +
            static_cast<int>(job.animated) > 1) {
        return fail("--sheet, --sequence and --animated each make a different file; choose one");
    }
    *out = job;
    return true;
}

int runBatch(const BatchJob& job, std::string* message) {
    const auto fail = [&](const std::string& why) {
        if (message != nullptr) { *message = why; }
        return 1;
    };
    Document doc;
    std::string error;
    if (!openAnything(doc, job.input, &error)) {
        return fail("could not open " + job.input + ": " + error);
    }
    const std::vector<Frame> frames = readFrames(doc);
    const std::vector<Cycle> cycles = readCycles(doc, static_cast<int>(frames.size()));
    Cycle cycle = everyFrame(static_cast<int>(frames.size()));
    if (!job.cycle.empty()) {
        bool found = false;
        for (const Cycle& candidate : cycles) {
            if (candidate.name == job.cycle) {
                cycle = candidate;
                found = true;
                break;
            }
        }
        if (!found) {
            std::string names;
            for (const Cycle& candidate : cycles) {
                names += (names.empty() ? "" : ", ") + candidate.name;
            }
            return fail("there is no cycle called " + job.cycle +
                        (names.empty() ? std::string(" -- the document has none")
                                       : " -- it has " + names));
        }
    }

    const std::string& out = job.output;
    const bool png = hasExtension(out, ".png");
    bool written = false;
    std::string what;

    if (hasExtension(out, ".lsprite")) {
        written = doc.save(out, &error);
        what = "the document";
    } else if (hasExtension(out, ".gpl") || hasExtension(out, ".hex") ||
               hasExtension(out, ".pal") || hasExtension(out, ".act")) {
        written = exportPaletteFile(doc, doc.sprite(), out, &error);
        what = "the palette";
    } else if (hasExtension(out, ".gif") || (png && (job.animated || job.sequence))) {
        AnimationSettings settings;
        settings.scale = job.scale;
        settings.format = hasExtension(out, ".gif") ? AnimationFormat::Gif
                        : job.sequence ? AnimationFormat::PngSequence
                                       : AnimationFormat::Apng;
        AnimationReport report;
        written = exportAnimation(doc, frames, cycle, out, settings, &report, &error);
        what = std::to_string(report.frames) + " frame(s)";
    } else if (png && job.sheet) {
        SheetSettings settings;
        settings.scale = job.scale;
        settings.border = job.border;
        settings.spacing = job.spacing;
        settings.manifestFormat = job.json == "hash"  ? SheetManifestFormat::AsepriteHash
                                : job.json == "array" ? SheetManifestFormat::AsepriteArray
                                                      : SheetManifestFormat::Fast;
        written = exportSheetToPng(doc, frames, stepsToPlay(cycle), cycles, out, settings, &error);
        what = "a sheet of " + std::to_string(stepsToPlay(cycle).size()) + " cell(s)";
    } else if (png) {
        if (job.frame < 1 || job.frame > static_cast<int>(frames.size())) {
            return fail("there is no frame " + std::to_string(job.frame) + "; the document has " +
                        std::to_string(frames.size()));
        }
        ExportSettings settings;
        settings.scale = job.scale;
        written = exportSpriteToPng(doc, frames[static_cast<size_t>(job.frame - 1)].sprite, out,
                                    settings, &error);
        what = "frame " + std::to_string(job.frame);
    } else {
        return fail("the output's extension says nothing Fast writes: use .png, .gif, "
                    ".lsprite, or a palette's .gpl, .hex, .pal or .act");
    }
    if (!written) {
        return fail("could not write " + out + ": " + error);
    }
    if (message != nullptr) {
        *message = "wrote " + what + " to " + out;
    }
    return 0;
}

} // namespace fast
