// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

// text_tests.cpp — text that stays text.
//
// The promises: the face lays out every printable character, proportionally,
// at any whole scale; a text element draws its glyphs in its ink, is listed
// as text, and reads back what it says; retyping rebuilds the pixels; a slot
// recolours it; it survives a file; fresh paint lands over it rather than
// into it; and enlarging the canvas carries its place and size along.

#include "app/canvas_ops.h"
#include "app/document.h"
#include "app/element.h"
#include "app/ink.h"
#include "app/paint.h"
#include "app/palette.h"
#include "app/pixel_font.h"
#include "app/text.h"
#include "app/file_io.h"

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

bool has(const std::vector<ls::Vec2i>& pixels, int x, int y) {
    for (ls::Vec2i p : pixels) {
        if (p.x == x && p.y == y) { return true; }
    }
    return false;
}

ls::Color at(Document& doc, int x, int y) {
    auto size = doc.engine().getCanvasSize(doc.id());
    auto compiled = doc.engine().compileSprite(
        doc.sprite(), compileProfile(ls::CompileProfileType::Export,
                                     static_cast<uint32_t>(size.value.x),
                                     static_cast<uint32_t>(size.value.y)));
    return compiled.ok() ? ls::readPixel(compiled.value.raster, x, y) : ls::Color{};
}

void testTheFaceLaysOut() {
    int w = 0;
    int h = 0;
    // "I": a three-wide bar top and bottom, a stem between.
    const std::vector<ls::Vec2i> i = layOutText("I", { 0, 0 }, 1, &w, &h);
    CHECK(w == 3 && h == 7);
    CHECK(has(i, 0, 0) && has(i, 1, 3) && !has(i, 0, 3) && has(i, 2, 6));
    // Proportional: "il" is narrower than "mm".
    int narrow = 0;
    int wide = 0;
    layOutText("il", { 0, 0 }, 1, &narrow, nullptr);
    layOutText("mm", { 0, 0 }, 1, &wide, nullptr);
    CHECK(narrow < wide);
    // Every printable character draws something (the space aside).
    for (char c = '!'; c <= '~'; ++c) {
        CHECK(!layOutText(std::string(1, c), { 0, 0 }, 1).empty());
    }
    // Scale doubles each pixel; a newline starts a line.
    CHECK(layOutText("I", { 0, 0 }, 2).size() == i.size() * 4);
    layOutText("A\nB", { 0, 0 }, 1, nullptr, &h);
    CHECK(h == pixelFontLineHeight() + 7);
}

void testATextElementStaysText() {
    Document doc;
    REQUIRE(doc.create("text", 32, 16));
    REQUIRE(ensurePalette(doc, doc.sprite()));
    const ls::ColorRole slot = addPaletteEntry(doc, doc.sprite(), { 10, 200, 10, 255 });
    PaintLayer layer;
    REQUIRE(createPaintLayer(doc, doc.sprite(), "sign", { 1, 1, 1, 255 }, &layer));
    Ink ink;
    ink.colour = { 10, 200, 10, 255 };
    ink.role = slot;
    TextSpec spec;
    spec.text = "HI";
    spec.at = { 2, 3 };
    PaintLayer text;
    doc.beginAction("Text");
    REQUIRE(addTextElement(doc, layer.layer, spec, ink, &text));
    doc.endAction();
    CHECK(at(doc, 2, 3).g == 200);                  // H's top-left
    CHECK(at(doc, 4, 3).a == 0);                    // inside the H

    bool listed = false;
    for (const Element& element : elementsOf(doc, layer.layer)) {
        listed = listed || element.kind == ElementKind::Text;
    }
    CHECK(listed);
    TextSpec read;
    REQUIRE(readTextElement(doc, text.region, &read));
    CHECK(read.text == "HI" && read.at.x == 2 && read.at.y == 3 && read.scale == 1);

    // Retyped: the pixels follow the words.
    spec.text = "II";
    doc.beginAction("Retype");
    REQUIRE(updateTextElement(doc, text.region, spec));
    doc.endAction();
    CHECK(at(doc, 3, 6).g == 200);                  // I's stem
    CHECK(at(doc, 2, 6).a == 0);

    // A slot recolours it.
    doc.beginAction("slot");
    REQUIRE(setPaletteEntry(doc, slot, { 200, 10, 10, 255 }));
    doc.endAction();
    CHECK(at(doc, 3, 6).r == 200);

    // Fresh paint lands over it, not into it: the text still says "II".
    doc.beginAction("paint");
    InkStroke stroke;
    Ink blue;
    blue.colour = { 0, 0, 255, 255 };
    REQUIRE(beginInkStroke(doc, layer.layer, blue, &stroke));
    REQUIRE(strokeInk(doc, stroke, {{ 3, 6 }}));
    doc.endAction();
    CHECK(at(doc, 3, 6).b == 255);
    REQUIRE(readTextElement(doc, text.region, &read));
    CHECK(read.text == "II");

    // Survives a file.
    std::string error;
    const std::string path = "text_roundtrip.lsprite";
    REQUIRE(doc.save(path, &error));
    Document reread;
    REQUIRE(reread.open(path, &error));
    auto info = reread.engine().getSpriteInfo(reread.sprite());
    REQUIRE(info.ok() && info.value.layers.size() == 1);
    bool found = false;
    for (const Element& element : elementsOf(reread, info.value.layers.front())) {
        TextSpec back;
        if (element.kind == ElementKind::Text && readTextElement(reread, element.region, &back)) {
            found = back.text == "II";
        }
    }
    CHECK(found);
    deleteFile(path);

    // Enlarging carries its place and size along.
    REQUIRE(enlargeSprite(doc, 2, &error));
    REQUIRE(readTextElement(doc, text.region, &read));
    CHECK(read.at.x == 4 && read.at.y == 6 && read.scale == 2);
}

} // namespace

int main() {
    testTheFaceLaysOut();
    testATextElementStaysText();
    if (failures == 0) {
        std::printf("text: all passed\n");
        return 0;
    }
    std::printf("text: %d failure(s)\n", failures);
    return 1;
}
