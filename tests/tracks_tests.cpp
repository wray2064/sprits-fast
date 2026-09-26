// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

// tracks_tests.cpp — layers that are the same in every frame.
//
// A layer added, deleted, renamed, hidden, moved or grouped in one frame is so
// in every frame; each frame keeps its own pixels; a duplicated layer copies
// each frame's own cel; a pasted one is empty elsewhere; a new frame gets
// every track, empty; a layer with no track is never lost; the whole thing is
// one undo step with the change that caused it; and an older document's
// separate stacks are brought together without losing anything.

#include "app/animation.h"
#include "app/document.h"
#include "app/file_io.h"
#include "app/ink.h"
#include "app/layers.h"
#include "app/paint.h"
#include "app/tracks.h"
#include "app/transform.h"

#include <cmath>
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

constexpr uint32_t kSize = 8;

std::vector<ls::SpriteId> frames(Document& doc) {
    std::vector<ls::SpriteId> out;
    for (const Frame& frame : readFrames(doc)) {
        out.push_back(frame.sprite);
    }
    return out;
}

std::vector<std::string> names(Document& doc, ls::SpriteId sprite) {
    std::vector<std::string> out;
    for (ls::LayerId layer : layerOrder(doc, sprite)) {
        LayerProps props;
        readLayerProps(doc, layer, &props);
        out.push_back(props.name);
    }
    return out;
}

ls::LayerId named(Document& doc, ls::SpriteId sprite, const std::string& name) {
    for (ls::LayerId layer : layerOrder(doc, sprite)) {
        LayerProps props;
        readLayerProps(doc, layer, &props);
        if (props.name == name) {
            return layer;
        }
    }
    return ls::LayerId{};
}

// Whether a layer draws anything at (x, y), compiled on its own.
bool draws(Document& doc, ls::LayerId layer, int x, int y) {
    auto compiled = doc.engine().compileLayer(
        layer, compileProfile(ls::CompileProfileType::Export, kSize, kSize));
    return compiled.ok() && ls::readPixel(compiled.value.raster, x, y).a != 0;
}

bool paint(Document& doc, ls::SpriteId sprite, const std::string& name, ls::Vec2i at) {
    std::vector<PaintLayer> layers;
    if (!adoptPaintLayers(doc, sprite, &layers)) {
        return false;
    }
    for (PaintLayer& layer : layers) {
        LayerProps props;
        readLayerProps(doc, layer.layer, &props);
        if (props.name == name) {
            doc.beginAction("Pencil");
            const bool ok = paintPixels(doc, layer, { at });
            doc.endAction();
            return ok;
        }
    }
    return false;
}


// Two frames with "body" and "arm", each frame's pixels its own, keyed as
// tracks and kept in step by the hook an editor sets.
struct Scene {
    Document doc;
    ls::SpriteId first;
    ls::SpriteId second;

    bool build() {
        if (!doc.create("tracks", kSize, kSize)) { return false; }
        PaintLayer body, arm;
        if (!createPaintLayer(doc, doc.sprite(), "body", { 200, 40, 40, 255 }, &body) ||
            !createPaintLayer(doc, doc.sprite(), "arm", { 40, 40, 200, 255 }, &arm)) {
            return false;
        }
        first = doc.sprite();
        if (!paint(doc, first, "body", { 1, 1 })) { return false; }
        if (duplicateFrame(doc, 0) != 1) { return false; }
        second = frames(doc)[1];
        if (!paint(doc, second, "body", { 5, 5 })) { return false; }
        // Keyed once, as an editor does when it opens a document.
        syncTracks(doc, first);
        doc.setBeforeCommit([this](Document& d) {
            syncTracks(d, master);
            syncLinks(d, master);
        });
        return true;
    }

    ls::SpriteId master;       // the frame being edited, as an editor's would be
};

void testNewDocumentsKeepTracks() {
    Document doc;
    REQUIRE(doc.create("on", kSize, kSize));
    CHECK(tracksOn(doc));
    setTracksOn(doc, false);
    CHECK(!tracksOn(doc));
}

void testAddingAndDeletingALayer() {
    Scene s;
    REQUIRE(s.build());
    s.master = s.first;
    PaintLayer made;
    s.doc.beginAction("Add layer");
    REQUIRE(createPaintLayer(s.doc, s.first, "cape", { 90, 200, 90, 255 }, &made));
    s.doc.endAction();
    CHECK(names(s.doc, s.second) == std::vector<std::string>({ "body", "arm", "cape" }));
    CHECK(!trackKey(s.doc, made.layer).empty());
    CHECK(trackKey(s.doc, made.layer) == trackKey(s.doc, named(s.doc, s.second, "cape")));

    // Each frame's body still draws its own pixels.
    CHECK(draws(s.doc, named(s.doc, s.first, "body"), 1, 1));
    CHECK(!draws(s.doc, named(s.doc, s.first, "body"), 5, 5));
    CHECK(draws(s.doc, named(s.doc, s.second, "body"), 5, 5));

    // Deleted in the second frame, it goes from the first -- one undo brings
    // both back.
    s.master = s.second;
    s.doc.beginAction("Delete layer");
    s.doc.engine().deleteLayer(named(s.doc, s.second, "arm"));
    s.doc.endAction();
    CHECK(names(s.doc, s.first) == std::vector<std::string>({ "body", "cape" }));
    REQUIRE(s.doc.undo());
    CHECK(names(s.doc, s.first) == std::vector<std::string>({ "body", "arm", "cape" }));
    CHECK(names(s.doc, s.second) == std::vector<std::string>({ "body", "arm", "cape" }));
}

void testPropertiesAndOrderFollow() {
    Scene s;
    REQUIRE(s.build());
    s.master = s.second;
    s.doc.beginAction("Change");
    const ls::LayerId arm = named(s.doc, s.second, "arm");
    renameLayer(s.doc, arm, "left arm");
    setLayerVisible(s.doc, arm, false);
    setLayerOpacity(s.doc, arm, 0.5f);
    const ls::Color tag{ 200, 100, 10, 255 };
    setLayerTag(s.doc, arm, &tag);
    moveLayer(s.doc, arm, 0);
    s.doc.endAction();
    CHECK(names(s.doc, s.first) == std::vector<std::string>({ "left arm", "body" }));
    LayerProps props;
    REQUIRE(readLayerProps(s.doc, named(s.doc, s.first, "left arm"), &props));
    CHECK(!props.visible && props.opacity == 0.5f && props.tagged && props.tag.r == 200);
}

void testDuplicateCopiesEachFramesOwnCel() {
    Scene s;
    REQUIRE(s.build());
    s.master = s.first;
    s.doc.beginAction("Duplicate layer");
    const ls::LayerId copy = duplicateLayer(s.doc, named(s.doc, s.first, "body"));
    s.doc.endAction();
    REQUIRE(copy.valid());
    LayerProps props;
    REQUIRE(readLayerProps(s.doc, copy, &props));
    const ls::LayerId there = named(s.doc, s.second, props.name);
    REQUIRE(there.valid());
    CHECK(trackKey(s.doc, there) == trackKey(s.doc, copy));
    CHECK(trackKey(s.doc, copy) != trackKey(s.doc, named(s.doc, s.first, "body")));
    // The second frame's copy is of the second frame's body, which alone has
    // the pixel at (5, 5); the first frame's copy is of the first's.
    CHECK(draws(s.doc, there, 5, 5));
    CHECK(draws(s.doc, copy, 1, 1) && !draws(s.doc, copy, 5, 5));
}

void testPastedLayerIsEmptyElsewhere() {
    Scene s;
    REQUIRE(s.build());
    s.master = s.second;
    s.doc.beginAction("Paste layer");
    const ls::LayerId pasted = pasteLayer(s.doc, named(s.doc, s.first, "body"), s.second, -1);
    renameLayer(s.doc, pasted, "pasted");
    s.doc.endAction();
    REQUIRE(pasted.valid());
    const ls::LayerId there = named(s.doc, s.first, "pasted");
    REQUIRE(there.valid());
    CHECK(!draws(s.doc, there, 1, 1) && !draws(s.doc, there, 5, 5));
    CHECK(draws(s.doc, pasted, 1, 1));
}

void testGroupsFollow() {
    Scene s;
    REQUIRE(s.build());
    s.master = s.first;
    s.doc.beginAction("Group");
    const ls::GroupId group = groupLayers(
        s.doc, { named(s.doc, s.first, "body"), named(s.doc, s.first, "arm") }, "figure");
    s.doc.endAction();
    REQUIRE(group.valid());
    const std::vector<ls::GroupId> there = groupOrder(s.doc, s.second);
    REQUIRE(there.size() == 1);
    GroupProps props;
    REQUIRE(readGroupProps(s.doc, there.front(), &props));
    CHECK(props.name == "figure" && props.layers.size() == 2);

    s.doc.beginAction("Hide group");
    setGroupVisible(s.doc, group, false);
    s.doc.endAction();
    REQUIRE(readGroupProps(s.doc, there.front(), &props));
    CHECK(!props.visible);
}

void testANewFrameHasEveryTrack() {
    Scene s;
    REQUIRE(s.build());
    s.master = s.first;
    REQUIRE(addFrame(s.doc, 1) == 2);
    const ls::SpriteId third = frames(s.doc)[2];
    CHECK(names(s.doc, third) == std::vector<std::string>({ "body", "arm" }));
    CHECK(!draws(s.doc, named(s.doc, third, "body"), 1, 1));
}

void testNothingUnkeyedIsLost() {
    Scene s;
    REQUIRE(s.build());
    // A layer made behind the tracks' back in the second frame, drawn on.
    ls::LayerDesc desc;
    desc.name = "stray";
    auto stray = s.doc.engine().createLayer(s.second, desc);
    REQUIRE(stray.ok());
    s.master = s.first;
    s.doc.beginAction("Anything");
    renameLayer(s.doc, named(s.doc, s.first, "arm"), "arm");
    s.doc.endAction();
    CHECK(named(s.doc, s.second, "stray").valid());
    CHECK(named(s.doc, s.first, "stray").valid());       // a track of its own now
}

void testAnOlderDocumentIsBroughtTogether() {
    Document doc;
    REQUIRE(doc.create("older", kSize, kSize));
    setTracksOn(doc, false);
    PaintLayer body, arm;
    REQUIRE(createPaintLayer(doc, doc.sprite(), "body", { 200, 40, 40, 255 }, &body));
    REQUIRE(createPaintLayer(doc, doc.sprite(), "arm", { 40, 40, 200, 255 }, &arm));
    REQUIRE(addFrame(doc, 0) == 1);
    const ls::SpriteId second = frames(doc)[1];
    PaintLayer hat;
    REQUIRE(createPaintLayer(doc, second, "hat", { 90, 90, 90, 255 }, &hat));
    REQUIRE(createPaintLayer(doc, second, "body", { 200, 40, 40, 255 }, &body));
    REQUIRE(paint(doc, second, "body", { 3, 3 }));

    doc.beginAction("Layers across frames");
    adoptTracks(doc, doc.sprite());
    doc.endAction();
    CHECK(tracksOn(doc));
    const std::vector<std::string> a = names(doc, doc.sprite());
    const std::vector<std::string> b = names(doc, second);
    CHECK(a == b);
    CHECK(a.size() == 3);
    CHECK(draws(doc, named(doc, second, "body"), 3, 3));     // nothing lost
    CHECK(trackKey(doc, named(doc, doc.sprite(), "body")) ==
          trackKey(doc, named(doc, second, "body")));
}

void testLinkedCelsShareTheirPixels() {
    Scene s;
    REQUIRE(s.build());
    s.master = s.first;
    const ls::LayerId body1 = named(s.doc, s.first, "body");
    s.doc.beginAction("Link");
    CHECK(linkCels(s.doc, body1, { s.second }) == 1);
    s.doc.endAction();
    ls::LayerId body2 = named(s.doc, s.second, "body");
    CHECK(!linkOf(s.doc, body1).empty() && linkOf(s.doc, body1) == linkOf(s.doc, body2));
    // The second frame's body is now the first's cel.
    CHECK(draws(s.doc, body2, 1, 1) && !draws(s.doc, body2, 5, 5));

    // A stroke in one lands in the other as it is drawn.
    REQUIRE(paint(s.doc, s.first, "body", { 6, 2 }));
    CHECK(draws(s.doc, body2, 6, 2));

    // A colour new to the cel -- an element of its own -- follows as the
    // action closes.
    Ink green;
    green.colour = { 40, 200, 60, 255 };
    s.doc.beginAction("Pencil");
    InkStroke stroke;
    REQUIRE(beginInkStroke(s.doc, body1, green, &stroke));
    REQUIRE(strokeInk(s.doc, stroke, { { 2, 6 } }));
    s.doc.endAction();
    CHECK(draws(s.doc, body2, 2, 6));

    // Kept by a file.
    std::string error;
    const std::string path = "fast_tracks_linked.lsprite";
    REQUIRE(s.doc.save(path, &error));
    {
        Document again;
        REQUIRE(again.open(path, &error));
        const std::vector<ls::SpriteId> both = frames(again);
        REQUIRE(both.size() == 2);
        const ls::LayerId a = named(again, both[0], "body");
        const ls::LayerId b = named(again, both[1], "body");
        CHECK(!linkOf(again, a).empty() && linkOf(again, a) == linkOf(again, b));
        again.beginAction("Pencil");
        std::vector<PaintLayer> layers;
        REQUIRE(adoptPaintLayers(again, both[0], &layers));
        for (PaintLayer& layer : layers) {
            if (layer.layer == a) {
                paintPixels(again, layer, { { 7, 1 } });
            }
        }
        again.endAction();
        CHECK(draws(again, b, 7, 1));                // still one cel
    }
    deleteFile(path);

    // Unlinked, the second frame keeps what it showed and stops following.
    s.master = s.second;
    s.doc.beginAction("Unlink");
    const ls::LayerId own = unlinkCel(s.doc, body2);
    s.doc.endAction();
    CHECK(own.valid() && linkOf(s.doc, own).empty());
    CHECK(linkOf(s.doc, named(s.doc, s.first, "body")).empty());     // a link of one is none
    CHECK(draws(s.doc, own, 6, 2) && draws(s.doc, own, 2, 6));
    s.master = s.first;
    REQUIRE(paint(s.doc, s.first, "body", { 7, 7 }));
    CHECK(!draws(s.doc, named(s.doc, s.second, "body"), 7, 7));
    CHECK(names(s.doc, s.second) == names(s.doc, s.first));

    // A duplicated frame's copy of a linked cel is its own.
    s.doc.beginAction("Link again");
    linkCels(s.doc, named(s.doc, s.first, "body"), { s.second });
    s.doc.endAction();
    REQUIRE(duplicateFrame(s.doc, 1) == 2);
    CHECK(linkOf(s.doc, named(s.doc, frames(s.doc)[2], "body")).empty());
}

// A tween: the frames between two keys get the keys' transforms with every
// value in between; a frame with none gets them; keys that differ refuse.
void testATweenFillsTheFramesBetween() {
    Scene s;
    REQUIRE(s.build());
    s.master = s.first;
    REQUIRE(addFrame(s.doc, 1) == 2);
    REQUIRE(addFrame(s.doc, 2) == 3);
    const std::vector<ls::SpriteId> run = frames(s.doc);
    REQUIRE(run.size() == 4);
    const std::string key = trackKey(s.doc, named(s.doc, run[0], "body"));
    const ls::LayerId a = named(s.doc, run[0], "body");
    const ls::LayerId d = named(s.doc, run[3], "body");
    addRotate(s.doc, a, 0.f, { 4.f, 4.f });
    addOffset(s.doc, a, { 0.f, 0.f });
    addRotate(s.doc, d, 90.f, { 4.f, 4.f });
    addOffset(s.doc, d, { 6.f, -3.f });

    std::string why;
    REQUIRE(tweenTransforms(s.doc, run, key, TweenEasing::Linear, &why));
    const std::vector<TransformEntry> b = listTransforms(s.doc, named(s.doc, run[1], "body"));
    const std::vector<TransformEntry> c = listTransforms(s.doc, named(s.doc, run[2], "body"));
    REQUIRE(b.size() == 2 && c.size() == 2);
    CHECK(b[0].kind == TransformKind::Rotate && std::fabs(b[0].angleDegrees - 30.f) < 0.01f);
    CHECK(std::fabs(c[0].angleDegrees - 60.f) < 0.01f);
    CHECK(b[1].kind == TransformKind::Offset && b[1].delta.x == 2.f && b[1].delta.y == -1.f);
    CHECK(c[1].delta.x == 4.f && c[1].delta.y == -2.f);

    // Easing: slower at the ends.
    REQUIRE(tweenTransforms(s.doc, run, key, TweenEasing::EaseInOut, &why));
    const std::vector<TransformEntry> eased = listTransforms(s.doc, named(s.doc, run[1], "body"));
    CHECK(eased[0].angleDegrees < 30.f && eased[0].angleDegrees > 10.f);

    // Keys with different transforms refuse, and say so.
    clearTransforms(s.doc, d);
    addScale(s.doc, d, { 2.f, 2.f }, { 4.f, 4.f });
    CHECK(!tweenTransforms(s.doc, run, key, TweenEasing::Linear, &why) && !why.empty());
}

} // namespace

int main() {
    testNewDocumentsKeepTracks();
    testAddingAndDeletingALayer();
    testPropertiesAndOrderFollow();
    testDuplicateCopiesEachFramesOwnCel();
    testPastedLayerIsEmptyElsewhere();
    testGroupsFollow();
    testANewFrameHasEveryTrack();
    testNothingUnkeyedIsLost();
    testAnOlderDocumentIsBroughtTogether();
    testLinkedCelsShareTheirPixels();
    testATweenFillsTheFramesBetween();
    if (failures == 0) {
        std::printf("tracks: all passed\n");
        return 0;
    }
    std::printf("tracks: %d failure(s)\n", failures);
    return 1;
}
