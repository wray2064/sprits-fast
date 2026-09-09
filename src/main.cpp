// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

// main.cpp — Fast without a window.
//
// There is no user interface yet. This drives a document through the motions an
// editor performs -- make a canvas, draw something, undo it, redo it, save,
// reopen -- and prints the result as text.
//
// It exists so that "Fast links the engine and can drive a document" is a thing
// the build proves rather than a thing we assume, and so the first day of user
// interface work starts from something known to be working underneath.

#include "app/document.h"

#include <cstdio>
#include <string>

using namespace ls;

namespace {

void printRaster(const RasterBuffer& raster) {
    for (uint32_t y = 0; y < raster.height; ++y) {
        std::string row;
        for (uint32_t x = 0; x < raster.width; ++x) {
            row += readPixel(raster, static_cast<int32_t>(x), static_cast<int32_t>(y)).a == 0
                 ? '.' : '#';
        }
        std::printf("  %s\n", row.c_str());
    }
}

int opaqueCount(const RasterBuffer& raster) {
    int count = 0;
    for (uint32_t y = 0; y < raster.height; ++y) {
        for (uint32_t x = 0; x < raster.width; ++x) {
            count += readPixel(raster, static_cast<int32_t>(x), static_cast<int32_t>(y)).a != 0;
        }
    }
    return count;
}

CompileProfile previewProfile(uint32_t size) {
    CompileProfile profile;
    profile.type = CompileProfileType::Preview;
    profile.outputWidth = size;
    profile.outputHeight = size;
    profile.palette = PalettePolicy::Unconstrained;
    return profile;
}

} // namespace

int main() {
    fast::Document doc;
    if (!doc.create("untitled", 16, 16)) {
        std::printf("could not create a document\n");
        return 1;
    }
    std::printf("Sprit's'fast — engine %d.%d.%d\n\n",
                (LS_ENGINE_VERSION >> 16) & 0xFF,
                (LS_ENGINE_VERSION >> 8) & 0xFF,
                LS_ENGINE_VERSION & 0xFF);

    LSContext& engine = doc.engine();

    const SpriteId sprite = doc.sprite();
    const LayerId layer = engine.createLayer(sprite, {"main"}).value;

    // One action: a shape filled with a colour. Bracketing it is what makes it
    // undoable, and it is the only thing a tool has to remember to do.
    doc.beginAction("Draw square");
    {
        const GeometryId rect =
            engine.createRect(doc.id(), {{4.f, 4.f}, 8.f, 8.f, 0.f}).value;
        FillSolidOp fill;
        fill.targetRegion = engine.createRegionFromGeometry(rect).value;
        fill.fallbackColor = {230, 120, 60, 255};
        engine.addOperation(layer, fill);
    }
    doc.endAction();

    auto compiled = engine.compileSprite(sprite, previewProfile(16));
    std::printf("after the draw (%d pixels, undo: \"%s\"):\n",
                opaqueCount(compiled.value.raster), doc.undoLabel().c_str());
    printRaster(compiled.value.raster);

    doc.undo();
    compiled = engine.compileSprite(sprite, previewProfile(16));
    std::printf("\nafter undo (%d pixels, redo: \"%s\"):\n",
                opaqueCount(compiled.value.raster), doc.redoLabel().c_str());

    doc.redo();
    compiled = engine.compileSprite(sprite, previewProfile(16));
    std::printf("after redo (%d pixels)\n", opaqueCount(compiled.value.raster));

    // Round-trip through a file. The document is the operations, so what is
    // saved is the instruction to fill a rectangle, not the pixels above.
    const std::string path = std::string("fast_smoke") + fast::kFileExtension;
    doc.setUiState("{\"zoom\":8}");

    std::string error;
    if (!doc.save(path, &error)) {
        std::printf("save failed: %s\n", error.c_str());
        return 1;
    }

    fast::Document reopened;
    if (!reopened.open(path, &error)) {
        std::printf("open failed: %s\n", error.c_str());
        return 1;
    }

    // The reopened document mints its own ids, so the sprite is found by asking
    // the document what it contains rather than by reusing the handle above.
    auto info = reopened.engine().getDocumentInfo(reopened.id());
    if (info.fail() || info.value.sprites.empty()) {
        std::printf("the reopened document has no sprites\n");
        return 1;
    }

    auto again = reopened.engine().compileSprite(info.value.sprites.front(),
                                                 previewProfile(16));
    const int reopenedCount = opaqueCount(again.value.raster);
    std::printf("\nsaved and reopened %s\n", path.c_str());
    std::printf("  ui state   : %s\n", reopened.uiState().c_str());
    std::printf("  canvas     : %ux%u\n", info.value.canvasWidth, info.value.canvasHeight);
    std::printf("  recompiled : %d pixels\n", reopenedCount);

    if (reopenedCount != 64) {
        std::printf("\nFAIL: expected the 8x8 square back, got %d pixels\n", reopenedCount);
        return 1;
    }

    std::printf("\nOK\n");
    return 0;
}
