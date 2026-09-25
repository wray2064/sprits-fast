// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

// batch_tests.cpp — exporting from the command line.
//
// The promises: --export is what makes a run a batch run, and without --to it
// is refused with a sentence; a single PNG, a GIF, a sheet, a sequence and a
// conversion to .lsprite are each written from their extension and flags; a
// cycle is found by name, and a missing one is named in the refusal along
// with the cycles that do exist; and an input Fast cannot read fails with a
// non-zero exit.

#include "app/animation.h"
#include "app/batch.h"
#include "app/document.h"
#include "app/file_io.h"
#include "app/image_io.h"
#include "app/ink.h"
#include "app/paint.h"

#include <cstdio>
#include <string>
#include <vector>

static int failures = 0;

#define CHECK(...)                                                            \
    do {                                                                      \
        if (!(__VA_ARGS__)) {                                                 \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #__VA_ARGS__);\
            ++failures;                                                       \
        }                                                                     \
    } while (0)

#define REQUIRE(...) do { if (!(__VA_ARGS__)) { CHECK(__VA_ARGS__); return; } } while (0)

namespace {

using namespace fast;

const std::string kInput = "batch_input.lsprite";

// Two frames, a pixel each, and a cycle called "walk".
bool writeInput() {
    Document doc;
    if (!doc.create("batch", 6, 4)) { return false; }
    PaintLayer layer;
    if (!createPaintLayer(doc, doc.sprite(), "Layer 1", { 200, 0, 0, 255 }, &layer)) {
        return false;
    }
    Ink red;
    red.colour = { 200, 0, 0, 255 };
    InkStroke stroke;
    doc.beginAction("paint");
    if (!beginInkStroke(doc, layer.layer, red, &stroke) || !strokeInk(doc, stroke, {{ 1, 1 }})) {
        return false;
    }
    doc.endAction();
    if (duplicateFrame(doc, 0) != 1) { return false; }
    Cycle walk;
    walk.name = "walk";
    walk.frames = { 1, 0 };
    setCycles(doc, { walk }, 2);
    std::string error;
    return doc.save(kInput, &error);
}

void testArguments() {
    BatchJob job;
    std::string error;
    CHECK(!parseBatch({ "hero.lsprite" }, &job, &error) && error.empty());   // not a batch run
    CHECK(!parseBatch({ "--export", "a.lsprite" }, &job, &error) && !error.empty());
    CHECK(!parseBatch({ "--export", "a", "--to", "b.png", "--scale", "0" }, &job, &error));
    CHECK(!parseBatch({ "--export", "a", "--to", "b.png", "--sheet", "--sequence" }, &job, &error));
    REQUIRE(parseBatch({ "--export", "a.lsprite", "--to", "b.gif", "--scale", "3",
                         "--cycle", "walk" }, &job, &error));
    CHECK(job.input == "a.lsprite" && job.output == "b.gif" && job.scale == 3 &&
          job.cycle == "walk");
}

void testEachKindOfOutput() {
    REQUIRE(writeInput());
    std::string message;
    BatchJob job;
    job.input = kInput;

    job.output = "batch_frame.png";
    job.scale = 2;
    REQUIRE(runBatch(job, &message) == 0);
    std::vector<uint8_t> bytes;
    ls::RasterBuffer image;
    std::string error;
    REQUIRE(readFile(job.output, bytes, &error) && decodeImage(bytes, &image, &error));
    CHECK(image.width == 12 && image.height == 8);
    deleteFile(job.output);

    job.output = "batch_indexed.png";
    job.indexed = true;
    REQUIRE(runBatch(job, &message) == 0);
    REQUIRE(readFile(job.output, bytes, &error));
    CHECK(bytes.size() > 25 && bytes[25] == 3);          // colour type: indexed
    deleteFile(job.output);
    job.indexed = false;

    job.output = "batch_walk.gif";
    job.cycle = "walk";
    REQUIRE(runBatch(job, &message) == 0);
    REQUIRE(readFile(job.output, bytes, &error));
    CHECK(countGifFrames(bytes) == 2);
    deleteFile(job.output);

    job.output = "batch_sheet.png";
    job.sheet = true;
    job.json = "hash";
    job.spacing = 1;
    REQUIRE(runBatch(job, &message) == 0);
    CHECK(fileExists("batch_sheet.png") && fileExists("batch_sheet.json"));
    std::vector<uint8_t> manifest;
    REQUIRE(readFile("batch_sheet.json", manifest, &error));
    const std::string text(manifest.begin(), manifest.end());
    CHECK(text.find("\"frameTags\"") != std::string::npos &&
          text.find("\"walk\"") != std::string::npos);
    job.json.clear();
    job.spacing = 0;
    deleteFile("batch_sheet.png");
    deleteFile("batch_sheet.json");
    job.sheet = false;

    job.output = "batch_seq.png";
    job.sequence = true;
    REQUIRE(runBatch(job, &message) == 0);
    CHECK(fileExists("batch_seq-01.png") && fileExists("batch_seq-02.png"));
    deleteFile("batch_seq-01.png");
    deleteFile("batch_seq-02.png");
    job.sequence = false;

    job.output = "batch_copy.lsprite";
    job.cycle.clear();
    REQUIRE(runBatch(job, &message) == 0);
    Document reread;
    CHECK(reread.open("batch_copy.lsprite", &error));
    CHECK(readFrames(reread).size() == 2);
    deleteFile("batch_copy.lsprite");

    // Refusals, each saying why.
    job.output = "batch.png";
    job.cycle = "run";
    CHECK(runBatch(job, &message) != 0 && message.find("walk") != std::string::npos);
    job.cycle.clear();
    job.frame = 9;
    CHECK(runBatch(job, &message) != 0 && message.find("frame 9") != std::string::npos);
    job.frame = 1;
    job.output = "batch.bmp";
    CHECK(runBatch(job, &message) != 0);
    job.input = "no_such_file.lsprite";
    job.output = "batch.png";
    CHECK(runBatch(job, &message) != 0);
    deleteFile(kInput);
}

} // namespace

int main() {
    testArguments();
    testEachKindOfOutput();
    if (failures == 0) {
        std::printf("batch: all passed\n");
        return 0;
    }
    std::printf("batch: %d failure(s)\n", failures);
    return 1;
}
