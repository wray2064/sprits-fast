// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

// layers_tests.cpp — the stack behaves like a stack.
//
// Blend and opacity change pixels; order is the engine's and moving a layer
// moves what draws over what; a group is a run of adjacent layers composited
// as one; a copy is its own from the first stroke and can land in another
// frame; a clip draws only where the layer below does; a lock survives a
// save. Each is one undo step, however many engine calls it took.

#include "app/animation.h"
#include "app/document.h"
#include "app/file_io.h"
#include "app/layers.h"
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

constexpr uint32_t kSize = 8;

struct Stack {
    Document doc;
    ls::SpriteId sprite;
    PaintLayer bottom, middle, top;      // three full-canvas layers, bottom first

    bool build() {
        if (!doc.create("layers", kSize, kSize)) { return false; }
        sprite = doc.sprite();
        return make("bottom", ls::Color{ 100, 0, 0, 255 }, &bottom) &&
               make("middle", ls::Color{ 0, 100, 0, 255 }, &middle) &&
               make("top",    ls::Color{ 0, 0, 100, 255 }, &top);
    }

    bool make(const char* name, ls::Color colour, PaintLayer* out) {
        if (!createPaintLayer(doc, sprite, name, colour, out)) { return false; }
        std::vector<ls::Vec2i> all;
        for (int y = 0; y < static_cast<int>(kSize); ++y) {
            for (int x = 0; x < static_cast<int>(kSize); ++x) { all.push_back({ x, y }); }
        }
        doc.beginAction("fill");
        const bool ok = paintPixels(doc, *out, all);
        doc.endAction();
        return ok;
    }

    ls::Color at(int x, int y, ls::SpriteId which = ls::SpriteId{}) {
        ls::CompileProfile profile;
        profile.type = ls::CompileProfileType::Export;
        profile.outputWidth = kSize;
        profile.outputHeight = kSize;
        profile.palette = ls::PalettePolicy::Unconstrained;
        auto compiled = doc.engine().compileSprite(which.valid() ? which : sprite, profile);
        if (compiled.fail()) { return ls::Color{ 0, 0, 0, 0 }; }
        return ls::readPixel(compiled.value.raster, x, y);
    }
};

std::vector<std::string> namesOf(Document& doc, ls::SpriteId sprite) {
    std::vector<std::string> out;
    for (ls::LayerId id : layerOrder(doc, sprite)) {
        LayerProps props;
        out.push_back(readLayerProps(doc, id, &props) ? props.name : "?");
    }
    return out;
}

// --- blend and opacity ----------------------------------------------------------

void testBlendAndOpacityChangeThePicture() {
    Stack s;
    REQUIRE(s.build());
    CHECK(s.at(3, 3).b == 100 && s.at(3, 3).r == 0);       // the top covers all

    s.doc.beginAction("opacity");
    CHECK(setLayerOpacity(s.doc, s.top.layer, 0.5f));
    s.doc.endAction();
    const ls::Color half = s.at(3, 3);
    CHECK(half.b > 0 && half.b < 100 && half.g > 0);        // half blue over green

    s.doc.beginAction("blend");
    CHECK(setLayerBlend(s.doc, s.top.layer, ls::BlendMode::Add));
    CHECK(setLayerOpacity(s.doc, s.top.layer, 1.f));
    s.doc.endAction();
    const ls::Color added = s.at(3, 3);
    CHECK(added.g == 100 && added.b == 100);                 // green + blue

    LayerProps props;
    REQUIRE(readLayerProps(s.doc, s.top.layer, &props));
    CHECK(props.blend == ls::BlendMode::Add);
    CHECK(props.opacity == 1.f);
    CHECK(props.name == "top");
    CHECK(blendModeNames().size() == 11);
    CHECK(std::string(blendModeNames()[static_cast<int>(ls::BlendMode::Add)]) == "Add");

    CHECK(s.doc.undo());
    CHECK(s.at(3, 3).g > 0 && s.at(3, 3).b < 100);           // the half-opacity state
    CHECK(s.doc.undo());
    CHECK(s.at(3, 3).b == 100 && s.at(3, 3).g == 0);
}

// --- order ----------------------------------------------------------------------

void testMovingALayerMovesWhatDrawsOverWhat() {
    Stack s;
    REQUIRE(s.build());
    CHECK(indexOfLayer(s.doc, s.sprite, s.top.layer) == 2);

    CHECK(lowerLayer(s.doc, s.top.layer));
    CHECK(namesOf(s.doc, s.sprite) == std::vector<std::string>({ "bottom", "top", "middle" }));
    CHECK(s.at(3, 3).g == 100);                                // middle is on top now

    CHECK(moveLayer(s.doc, s.top.layer, 0));
    CHECK(namesOf(s.doc, s.sprite) == std::vector<std::string>({ "top", "bottom", "middle" }));
    CHECK(!lowerLayer(s.doc, s.top.layer));                    // already at the bottom
    CHECK(raiseLayer(s.doc, s.top.layer));
    CHECK(namesOf(s.doc, s.sprite) == std::vector<std::string>({ "bottom", "top", "middle" }));

    // Out of range clamps; the same place is not an action.
    CHECK(moveLayer(s.doc, s.top.layer, 99));
    CHECK(namesOf(s.doc, s.sprite) == std::vector<std::string>({ "bottom", "middle", "top" }));
    const std::string label = s.doc.undoLabel();
    CHECK(moveLayer(s.doc, s.top.layer, 2));
    CHECK(s.doc.undoLabel() == label);

    // Every move was one undo step.
    CHECK(s.doc.undo());
    CHECK(namesOf(s.doc, s.sprite) == std::vector<std::string>({ "bottom", "top", "middle" }));
    CHECK(s.doc.undo());
    CHECK(namesOf(s.doc, s.sprite) == std::vector<std::string>({ "top", "bottom", "middle" }));
}

// --- copies ---------------------------------------------------------------------

void testADuplicateIsItsOwnAndSitsAbove() {
    Stack s;
    REQUIRE(s.build());
    const ls::LayerId copy = duplicateLayer(s.doc, s.middle.layer);
    REQUIRE(copy.valid());
    CHECK(namesOf(s.doc, s.sprite) ==
          std::vector<std::string>({ "bottom", "middle", "middle copy", "top" }));

    // Drawing on the copy leaves the original alone: erase from the copy, and
    // the original still covers the pixel.
    std::vector<PaintLayer> layers;
    REQUIRE(adoptPaintLayers(s.doc, s.sprite, &layers));
    REQUIRE(layers.size() == 4);
    s.doc.beginAction("hide top");
    setLayerVisible(s.doc, s.top.layer, false);
    setLayerVisible(s.doc, s.middle.layer, false);
    s.doc.endAction();
    CHECK(s.at(3, 3).g == 100);                                // the copy shows
    s.doc.beginAction("erase");
    CHECK(erasePixels(s.doc, layers[2], {{ 3, 3 }}));
    s.doc.endAction();
    CHECK(s.at(3, 3).r == 100);                                // bottom shows through the hole
    setLayerVisible(s.doc, s.middle.layer, true);
    CHECK(s.at(3, 3).g == 100);                                // the original is whole

    CHECK(s.doc.undo() && s.doc.undo() && s.doc.undo());
    CHECK(namesOf(s.doc, s.sprite) == std::vector<std::string>({ "bottom", "middle", "top" }));
}

void testPasteLandsInAnotherFrame() {
    Stack s;
    REQUIRE(s.build());
    REQUIRE(addFrame(s.doc, 0) == 1);
    const std::vector<Frame> frames = readFrames(s.doc);
    REQUIRE(frames.size() == 2);
    CHECK(layerOrder(s.doc, frames[1].sprite).empty());

    const ls::LayerId pasted = pasteLayer(s.doc, s.top.layer, frames[1].sprite, -1);
    REQUIRE(pasted.valid());
    CHECK(namesOf(s.doc, frames[1].sprite) == std::vector<std::string>({ "top" }));
    CHECK(s.at(3, 3, frames[1].sprite).b == 100);
    CHECK(s.doc.undo());
    CHECK(layerOrder(s.doc, frames[1].sprite).empty());

    // Into the middle of the same frame, by index.
    const ls::LayerId again = pasteLayer(s.doc, s.bottom.layer, s.sprite, 1);
    REQUIRE(again.valid());
    CHECK(namesOf(s.doc, s.sprite) ==
          std::vector<std::string>({ "bottom", "bottom", "middle", "top" }));
}

// --- groups ---------------------------------------------------------------------

void testAGroupIsARunCompositedAsOne() {
    Stack s;
    REQUIRE(s.build());
    // bottom and top, not adjacent: grouping gathers them at the topmost's place.
    const ls::GroupId group = groupLayers(s.doc, { s.bottom.layer, s.top.layer }, "pair");
    REQUIRE(group.valid());
    CHECK(namesOf(s.doc, s.sprite) == std::vector<std::string>({ "middle", "bottom", "top" }));
    CHECK(groupOf(s.doc, s.bottom.layer) == group);
    CHECK(groupOf(s.doc, s.top.layer) == group);
    CHECK(!groupOf(s.doc, s.middle.layer).valid());
    GroupProps props;
    REQUIRE(readGroupProps(s.doc, group, &props));
    CHECK(props.name == "pair");
    CHECK(props.layers == std::vector<ls::LayerId>({ s.bottom.layer, s.top.layer }));
    CHECK(groupOrder(s.doc, s.sprite) == std::vector<ls::GroupId>({ group }));

    // Group opacity is not per-layer opacity: at half, the group (blue over
    // red, opaque within itself) blends with the green under it as a whole,
    // so no red shows through -- the red is inside the group, under the blue.
    CHECK(setGroupOpacity(s.doc, group, 0.5f));
    const ls::Color half = s.at(3, 3);
    CHECK(half.r == 0 && half.g > 0 && half.b > 0);
    CHECK(setGroupOpacity(s.doc, group, 1.f));
    CHECK(setGroupVisible(s.doc, group, false));
    CHECK(s.at(3, 3).g == 100);                                 // only middle draws
    CHECK(setGroupVisible(s.doc, group, true));
    CHECK(renameGroup(s.doc, group, "both"));
    REQUIRE(readGroupProps(s.doc, group, &props));
    CHECK(props.name == "both");

    // Moving a member to the top of its run keeps it in; moving it past the
    // run takes it out; moving a stranger into the run brings it in.
    CHECK(raiseLayer(s.doc, s.bottom.layer));
    CHECK(namesOf(s.doc, s.sprite) == std::vector<std::string>({ "middle", "top", "bottom" }));
    CHECK(groupOf(s.doc, s.bottom.layer) == group);
    CHECK(moveLayer(s.doc, s.bottom.layer, 0));
    CHECK(!groupOf(s.doc, s.bottom.layer).valid());
    CHECK(moveLayer(s.doc, s.middle.layer, 1));            // between bottom and top: not in a run
    CHECK(!groupOf(s.doc, s.middle.layer).valid());
    CHECK(groupLayers(s.doc, { s.bottom.layer, s.top.layer }, "again").valid());
    CHECK(namesOf(s.doc, s.sprite) == std::vector<std::string>({ "middle", "bottom", "top" }));
    CHECK(moveLayer(s.doc, s.middle.layer, 1));            // now inside the run
    CHECK(groupOf(s.doc, s.middle.layer) == groupOf(s.doc, s.top.layer));

    // Ungroup leaves the order alone.
    const ls::GroupId second = groupOf(s.doc, s.top.layer);
    CHECK(ungroup(s.doc, second));
    CHECK(namesOf(s.doc, s.sprite) == std::vector<std::string>({ "bottom", "middle", "top" }));
    CHECK(!groupOf(s.doc, s.top.layer).valid());
    CHECK(groupOrder(s.doc, s.sprite).empty());
    CHECK(s.doc.undo());
    CHECK(groupOf(s.doc, s.top.layer) == second);

    // A duplicate of a member is a member.
    const ls::LayerId copy = duplicateLayer(s.doc, s.top.layer);
    CHECK(copy.valid() && groupOf(s.doc, copy) == second);
}

// Explicitly in and out, by name rather than by where a drag landed.
void testAddingToAndLeavingAGroup() {
    Stack s;
    REQUIRE(s.build());
    const ls::GroupId group = groupLayers(s.doc, { s.top.layer }, "one");
    REQUIRE(group.valid());
    CHECK(namesOf(s.doc, s.sprite) == std::vector<std::string>({ "bottom", "middle", "top" }));

    // bottom joins: it moves to the top of the group's run.
    CHECK(addToGroup(s.doc, s.bottom.layer, group));
    CHECK(groupOf(s.doc, s.bottom.layer) == group);
    CHECK(namesOf(s.doc, s.sprite) == std::vector<std::string>({ "middle", "top", "bottom" }));
    CHECK(addToGroup(s.doc, s.bottom.layer, group));      // already there: no change
    CHECK(namesOf(s.doc, s.sprite) == std::vector<std::string>({ "middle", "top", "bottom" }));

    // top leaves: just above the run, so still over middle and under bottom.
    CHECK(removeFromGroup(s.doc, s.top.layer));
    CHECK(!groupOf(s.doc, s.top.layer).valid());
    CHECK(namesOf(s.doc, s.sprite) == std::vector<std::string>({ "middle", "bottom", "top" }));
    CHECK(!removeFromGroup(s.doc, s.top.layer));           // not in one

    // The last member leaving takes the group with it.
    CHECK(removeFromGroup(s.doc, s.bottom.layer));
    CHECK(groupOrder(s.doc, s.sprite).empty());
    CHECK(s.doc.undo());
    CHECK(groupOrder(s.doc, s.sprite).size() == 1);
    CHECK(groupOf(s.doc, s.bottom.layer).valid());
}

// --- clipping -------------------------------------------------------------------

void testAClipDrawsOnlyWhereTheLayerBelowDoes() {
    Stack s;
    REQUIRE(s.build());
    // Cut a hole in middle; top, clipped to it, must show the hole too.
    s.doc.beginAction("hole");
    CHECK(erasePixels(s.doc, s.middle, {{ 3, 3 }}));
    s.doc.endAction();
    CHECK(s.at(3, 3).b == 100);                                 // top still covers the hole
    s.doc.beginAction("clip");
    CHECK(clipToBelow(s.doc, s.top.layer, true));
    s.doc.endAction();
    CHECK(s.at(3, 3).r == 100);                                 // bottom shows through
    CHECK(s.at(0, 0).b == 100);                                 // elsewhere top draws
    LayerProps props;
    REQUIRE(readLayerProps(s.doc, s.top.layer, &props));
    CHECK(props.clipBase == s.middle.layer);

    // Moving the clipped layer clears the clip, because "below" changed.
    CHECK(moveLayer(s.doc, s.top.layer, 0));
    REQUIRE(readLayerProps(s.doc, s.top.layer, &props));
    CHECK(!props.clipBase.valid());
    CHECK(!clipToBelow(s.doc, s.top.layer, true));              // nothing below it

    CHECK(s.doc.undo());
    REQUIRE(readLayerProps(s.doc, s.top.layer, &props));
    CHECK(props.clipBase == s.middle.layer);
    CHECK(clipToBelow(s.doc, s.top.layer, false));
    CHECK(s.at(3, 3).b == 100);
}

// --- lock -----------------------------------------------------------------------

void testALockIsKeptAndSurvivesASave() {
    Stack s;
    REQUIRE(s.build());
    CHECK(!layerLocked(s.doc, s.top.layer));
    CHECK(setLayerLocked(s.doc, s.top.layer, true));
    CHECK(layerLocked(s.doc, s.top.layer));
    const ls::LayerId copy = duplicateLayer(s.doc, s.top.layer);
    CHECK(copy.valid() && layerLocked(s.doc, copy));

    std::string error;
    const std::string path = "fast_layers_lock.lsprite";
    REQUIRE(s.doc.save(path, &error));
    Document again;
    REQUIRE(again.open(path, &error));
    deleteFile(path);
    const std::vector<ls::LayerId> order = layerOrder(again, again.sprite());
    REQUIRE(order.size() == 4);
    CHECK(layerLocked(again, order[2]));
    CHECK(layerLocked(again, order[3]));
    CHECK(!layerLocked(again, order[0]));
    CHECK(setLayerLocked(again, order[2], false));
    CHECK(!layerLocked(again, order[2]));
}

// --- tag and notes --------------------------------------------------------------

void testATagAndNotesAreKept() {
    Stack s;
    REQUIRE(s.build());
    LayerProps props;
    REQUIRE(readLayerProps(s.doc, s.top.layer, &props));
    CHECK(!props.tagged && props.notes.empty());
    const ls::Color orange{ 240, 130, 20, 255 };
    CHECK(setLayerTag(s.doc, s.top.layer, &orange));
    CHECK(setLayerNotes(s.doc, s.top.layer, "the cape; redraw frame 3\nkeep the fold"));
    REQUIRE(readLayerProps(s.doc, s.top.layer, &props));
    CHECK(props.tagged && props.tag.r == 240 && props.tag.g == 130 && props.tag.b == 20);
    CHECK(props.notes == "the cape; redraw frame 3\nkeep the fold");

    std::string error;
    const std::string path = "fast_layers_tag.lsprite";
    REQUIRE(s.doc.save(path, &error));
    Document again;
    REQUIRE(again.open(path, &error));
    deleteFile(path);
    const std::vector<ls::LayerId> order = layerOrder(again, again.sprite());
    REQUIRE(order.size() == 3);
    ls::Color back;
    CHECK(layerTag(again, order[2], &back) && back.r == 240 && back.b == 20);
    CHECK(layerNotes(again, order[2]) == "the cape; redraw frame 3\nkeep the fold");

    CHECK(setLayerTag(again, order[2], nullptr));
    CHECK(setLayerNotes(again, order[2], ""));
    CHECK(!layerTag(again, order[2], nullptr));
    CHECK(layerNotes(again, order[2]).empty());
}

// --- the file -------------------------------------------------------------------

void testTheStackSurvivesASave() {
    Stack s;
    REQUIRE(s.build());
    const ls::GroupId group = groupLayers(s.doc, { s.middle.layer, s.top.layer }, "pair");
    REQUIRE(group.valid());
    CHECK(setGroupBlend(s.doc, group, ls::BlendMode::Multiply));
    CHECK(setGroupOpacity(s.doc, group, 0.75f));
    CHECK(setLayerBlend(s.doc, s.top.layer, ls::BlendMode::Screen));
    CHECK(setLayerOpacity(s.doc, s.top.layer, 0.25f));
    CHECK(clipToBelow(s.doc, s.top.layer, true));
    const ls::Color before = s.at(3, 3);

    std::string error;
    const std::string path = "fast_layers_stack.lsprite";
    REQUIRE(s.doc.save(path, &error));
    Document again;
    REQUIRE(again.open(path, &error));
    deleteFile(path);

    const std::vector<ls::LayerId> order = layerOrder(again, again.sprite());
    REQUIRE(order.size() == 3);
    CHECK(namesOf(again, again.sprite()) == std::vector<std::string>({ "bottom", "middle", "top" }));
    LayerProps props;
    REQUIRE(readLayerProps(again, order[2], &props));
    CHECK(props.blend == ls::BlendMode::Screen && props.opacity == 0.25f);
    CHECK(props.clipBase == order[1]);
    CHECK(props.group.valid() && props.group == groupOf(again, order[1]));
    GroupProps groupProps;
    REQUIRE(readGroupProps(again, props.group, &groupProps));
    CHECK(groupProps.name == "pair");
    CHECK(groupProps.blend == ls::BlendMode::Multiply && groupProps.opacity == 0.75f);
    CHECK(groupProps.layers == std::vector<ls::LayerId>({ order[1], order[2] }));

    Stack view;
    const ls::Color after = [&]() {
        ls::CompileProfile profile;
        profile.type = ls::CompileProfileType::Export;
        profile.outputWidth = kSize;
        profile.outputHeight = kSize;
        profile.palette = ls::PalettePolicy::Unconstrained;
        auto compiled = again.engine().compileSprite(again.sprite(), profile);
        return compiled.ok() ? ls::readPixel(compiled.value.raster, 3, 3) : ls::Color{};
    }();
    CHECK(after.r == before.r && after.g == before.g && after.b == before.b && after.a == before.a);
}

} // namespace

// --- merge down ----------------------------------------------------------------
//
// Merging moves elements; it does not flatten. The picture is the same after
// as before -- opacity and blend folded into what moved -- there is one layer
// fewer, the moved pixels are still an element of their own, it is one undo
// step, and the cases that would change the picture are refused.
void testMergeDownKeepsThePictureAndTheElements() {
    Stack s;
    REQUIRE(s.build());
    s.doc.beginAction("opacity");
    REQUIRE(setLayerOpacity(s.doc, s.top.layer, 0.5f));
    s.doc.endAction();
    const ls::Color before = s.at(3, 3);
    s.doc.clearHistory();

    std::string why;
    const ls::LayerId into = mergeDown(s.doc, s.top.layer, &why);
    REQUIRE(into == s.middle.layer);
    CHECK(layerOrder(s.doc, s.sprite).size() == 2);
    const ls::Color after = s.at(3, 3);
    CHECK(after.r == before.r && after.g == before.g && after.b == before.b);
    auto operations = s.doc.engine().getLayerOperations(s.middle.layer);
    CHECK(operations.ok() && operations.value.size() == 2);   // two colours, still two

    CHECK(s.doc.undo());
    CHECK(layerOrder(s.doc, s.sprite).size() == 3);
    CHECK(!s.doc.canUndo());

    // Nothing below the bottom layer to merge into.
    CHECK(!mergeDown(s.doc, s.bottom.layer, &why).valid());
    CHECK(!why.empty());

    // A hidden layer would be shown by merging, so it is refused.
    s.doc.beginAction("hide");
    setLayerVisible(s.doc, s.top.layer, false);
    s.doc.endAction();
    CHECK(!mergeDown(s.doc, s.top.layer, &why).valid());
}

int main() {
    testATagAndNotesAreKept();
    testBlendAndOpacityChangeThePicture();
    testMovingALayerMovesWhatDrawsOverWhat();
    testADuplicateIsItsOwnAndSitsAbove();
    testPasteLandsInAnotherFrame();
    testAGroupIsARunCompositedAsOne();
    testAddingToAndLeavingAGroup();
    testAClipDrawsOnlyWhereTheLayerBelowDoes();
    testALockIsKeptAndSurvivesASave();
    testTheStackSurvivesASave();
    testMergeDownKeepsThePictureAndTheElements();

    if (failures == 0) {
        std::printf("fast_layers: all checks passed\n");
        return 0;
    }
    std::printf("fast_layers: %d check(s) failed\n", failures);
    return 1;
}
