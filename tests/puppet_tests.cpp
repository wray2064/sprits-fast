// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

// puppet_tests.cpp — cutout puppets: a character drawn in a frame, split into
// parts that hang from each other, posed by hand and captured as frames.
//
// The promises: splitting a drawing into a puppet changes no pixel of it;
// parts stay out of the timeline; a posed part turns about its joint and
// keeps its joint on its socket; a captured frame is exactly the puppet as
// posed, pixel for pixel, still shapes, and stands on its own once taken;
// every capture of a puppet puts each part on the same track, and no track
// of the frames is lost; the pose is written on the frame; and a puppet, its
// joints and its captures come back from a file as they were -- the engine's
// own anchors, which is what lets another program pick the puppet up.

#include "app/animation.h"
#include "app/canvas_ops.h"
#include "app/document.h"
#include "app/layers.h"
#include "app/paint.h"
#include "app/puppet.h"
#include "app/shape.h"
#include "app/tracks.h"

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

using namespace fast;

namespace {

constexpr int kSize = 48;
const ls::Color kTorso   { 200, 200, 220, 255 };
const ls::Color kArm     { 220, 120, 60, 255 };
const ls::Color kForearm { 90, 160, 90, 255 };
const ls::Color kHand    { 250, 240, 120, 255 };
const ls::Color kHead    { 60, 120, 200, 255 };

ls::RasterBuffer picture(Document& doc, ls::SpriteId sprite) {
    auto compiled = doc.engine().compileSprite(
        sprite, compileProfile(ls::CompileProfileType::Export, kSize, kSize));
    return compiled.ok() ? compiled.value.raster : ls::RasterBuffer{};
}

ls::RasterBuffer assembly(Document& doc, ls::SpriteId root) {
    auto compiled = doc.engine().compileAssembly(
        root, compileProfile(ls::CompileProfileType::Export, kSize, kSize));
    return compiled.ok() ? compiled.value.raster : ls::RasterBuffer{};
}

bool near(float a, float b, float tolerance = 0.01f) {
    return std::fabs(a - b) <= tolerance;
}

bool nearPoint(ls::Vec2f p, ls::Vec2f q, float tolerance = 0.01f) {
    return near(p.x, q.x, tolerance) && near(p.y, q.y, tolerance);
}

int count(const ls::RasterBuffer& raster, ls::Color colour) {
    int n = 0;
    for (uint32_t y = 0; y < raster.height; ++y) {
        for (uint32_t x = 0; x < raster.width; ++x) {
            const ls::Color c = ls::readPixel(raster, static_cast<int32_t>(x), static_cast<int32_t>(y));
            n += c.r == colour.r && c.g == colour.g && c.b == colour.b && c.a == colour.a ? 1 : 0;
        }
    }
    return n;
}

// One layer of the character: a box, and on the forearm a line too, so the
// shapes in a part are not all boxes.
ls::LayerId drawLayer(Document& doc, const char* name, ls::Color colour, ls::Vec2f from,
                      ls::Vec2f to, bool line = false) {
    PaintLayer layer;
    if (!createPaintLayer(doc, doc.sprite(), name, colour, &layer)) {
        return ls::LayerId{};
    }
    ShapeParams box;
    box.from = from;
    box.to = to;
    ShapeLayer made;
    doc.beginAction("Draw");
    addShapeTo(doc, layer.layer, ShapeKind::Rectangle, box, colour, ls::kColorRoleNone, &made);
    if (line) {
        ShapeParams stroke;
        stroke.from = linePoint({ from.x, to.y });
        stroke.to = linePoint({ to.x + 2.f, to.y + 3.f });
        addShapeTo(doc, layer.layer, ShapeKind::Line, stroke, kHand, ls::kColorRoleNone, &made);
    }
    doc.endAction();
    return layer.layer;
}

// A character drawn in the first frame, one layer a body part, then split
// into a puppet: torso the root, head and arm hung from it, forearm from the
// arm -- each where it was drawn.
struct Puppet {
    ls::SpriteId frame;
    ls::SpriteId torso, head, arm, forearm;
    ls::RasterBuffer drawn;       // the frame before it was split
};

bool build(Document& doc, Puppet* p) {
    if (!doc.create("puppet", kSize, kSize)) {
        return false;
    }
    p->frame = doc.sprite();
    const ls::LayerId torso = drawLayer(doc, "torso", kTorso, { 18.f, 16.f }, { 28.f, 32.f });
    const ls::LayerId head = drawLayer(doc, "head", kHead, { 19.f, 8.f }, { 27.f, 16.f });
    const ls::LayerId arm = drawLayer(doc, "arm", kArm, { 28.f, 17.f }, { 31.f, 26.f });
    const ls::LayerId forearm = drawLayer(doc, "forearm", kForearm, { 28.f, 26.f }, { 31.f, 34.f }, true);
    p->drawn = picture(doc, p->frame);
    p->torso = partFromLayer(doc, torso, "torso", { 23.f, 24.f });
    p->head = partFromLayer(doc, head, "head", { 23.f, 16.f });
    // Joints in the middle of a pixel (or on a corner): a quarter turn about
    // one sends every pixel to a pixel (see jointPoint in puppet.h).
    p->arm = partFromLayer(doc, arm, "arm", { 29.5f, 17.5f });
    p->forearm = partFromLayer(doc, forearm, "forearm", { 29.5f, 26.5f });
    return p->torso.valid() && p->head.valid() && p->arm.valid() && p->forearm.valid() &&
           hangPartWhereItIs(doc, p->head, p->torso, "neck").valid() &&
           hangPartWhereItIs(doc, p->arm, p->torso, "shoulder").valid() &&
           hangPartWhereItIs(doc, p->forearm, p->arm, "elbow").valid();
}

// --- building it -----------------------------------------------------------

void testSplittingADrawingChangesNoPixel() {
    Document doc;
    Puppet p;
    REQUIRE(build(doc, &p));
    CHECK(count(p.drawn, kArm) > 0 && count(p.drawn, kHand) > 0);
    // The puppet at rest is the character as drawn, pixel for pixel.
    CHECK(assembly(doc, p.torso).pixels == p.drawn.pixels);
    // The layers left the frame for the parts.
    CHECK(layerOrder(doc, p.frame).empty());
    CHECK(rootPart(doc, p.forearm) == p.torso);
    CHECK(parentPart(doc, p.forearm) == p.arm);
    CHECK(puppetRoots(doc).size() == 1);
}

void testPartsAreNotFrames() {
    Document doc;
    Puppet p;
    REQUIRE(build(doc, &p));
    CHECK(readFrames(doc).size() == 1);
    CHECK(readFrames(doc).front().sprite == p.frame);
    CHECK(partsOf(doc).size() == 4);
    CHECK(partName(doc, p.forearm) == "forearm");
    // A frame added keeps the frames first and the parts after them.
    REQUIRE(addFrame(doc, 0) == 1);
    CHECK(readFrames(doc).size() == 2);
    auto info = doc.engine().getDocumentInfo(doc.id());
    REQUIRE(info.ok());
    CHECK(info.value.sprites.front() == p.frame);
    CHECK(isPart(doc, info.value.sprites.back()));
}

// --- posing ----------------------------------------------------------------

void testAPosedPartTurnsAboutItsJoint() {
    Document doc;
    Puppet p;
    REQUIRE(build(doc, &p));
    const ls::Vec2f elbow = doc.engine().getPivotWorldPosition(jointOf(doc, p.forearm)).value;
    doc.beginAction("Pose");
    REQUIRE(setJointAngle(doc, p.arm, 90.f));
    doc.endAction();
    CHECK(near(jointAngle(doc, p.arm), 90.f));
    // The arm turned about the shoulder: its joint stayed on it, and the elbow
    // swung round with it -- a quarter turn about (29.5, 17.5) sends
    // (29.5, 26.5) to (20.5, 17.5).
    const ls::Vec2f shoulder = doc.engine().getPivotWorldPosition(jointOf(doc, p.arm)).value;
    CHECK(nearPoint(shoulder, { 29.5f, 17.5f }));
    CHECK(nearPoint(doc.engine().getPivotWorldPosition(jointOf(doc, p.forearm)).value,
                    { 20.5f, 17.5f }));
    CHECK(!nearPoint(elbow, { 20.5f, 17.5f }));

    // The root turns about its own joint and moves.
    doc.beginAction("Pose");
    REQUIRE(setJointAngle(doc, p.torso, 30.f));
    REQUIRE(setRootOffset(doc, p.torso, { 5.f, -2.f }));
    doc.endAction();
    CHECK(near(jointAngle(doc, p.torso), 30.f));
    CHECK(nearPoint(rootOffset(doc, p.torso), { 5.f, -2.f }));
    CHECK(nearPoint(doc.engine().getPivotWorldPosition(jointOf(doc, p.torso)).value, { 28.f, 22.f }));

    REQUIRE(restPose(doc, p.torso));
    CHECK(assembly(doc, p.torso).pixels == p.drawn.pixels);
}

// --- capturing -------------------------------------------------------------

void testACaptureIsThePoseExactly() {
    Document doc;
    Puppet p;
    REQUIRE(build(doc, &p));
    const struct { float torso, arm, forearm, head; ls::Vec2f offset; } poses[] = {
        { 0.f, 0.f, 0.f, 0.f, { 0.f, 0.f } },
        { 0.f, 90.f, -45.f, 0.f, { 0.f, 0.f } },
        { 0.f, 37.f, 60.f, -12.f, { 0.f, 0.f } },
        { 15.f, -120.f, 30.f, 8.f, { 3.f, 1.f } },
        { 90.f, 180.f, 90.f, 0.f, { -2.f, 4.f } },
    };
    int after = 0;
    for (const auto& pose : poses) {
        doc.beginAction("Pose");
        setJointAngle(doc, p.torso, pose.torso);
        setRootOffset(doc, p.torso, pose.offset);
        setJointAngle(doc, p.arm, pose.arm);
        setJointAngle(doc, p.forearm, pose.forearm);
        setJointAngle(doc, p.head, pose.head);
        doc.endAction();
        const ls::RasterBuffer posed = assembly(doc, p.torso);
        const int at = capturePose(doc, p.torso, after);
        REQUIRE(at == after + 1);
        const ls::SpriteId frame = readFrames(doc)[static_cast<size_t>(at)].sprite;
        const ls::RasterBuffer captured = picture(doc, frame);
        int off = 0;
        for (size_t i = 0; i + 3 < posed.pixels.size(); i += 4) {
            off += std::equal(posed.pixels.begin() + static_cast<long>(i),
                              posed.pixels.begin() + static_cast<long>(i) + 4,
                              captured.pixels.begin() + static_cast<long>(i)) ? 0 : 1;
        }
        if (off != 0) {
            std::printf("  pose %g %g %g %g: %d pixel(s) of the capture off\n",
                        static_cast<double>(pose.torso), static_cast<double>(pose.arm),
                        static_cast<double>(pose.forearm), static_cast<double>(pose.head), off);
        }
        CHECK(off == 0);
        after = at;
    }
    CHECK(readFrames(doc).size() == 6);
}

// A capture is a frame of its own: changing the puppet afterwards leaves it
// as it was taken, and it can be drawn on.
void testACaptureStandsOnItsOwn() {
    Document doc;
    Puppet p;
    REQUIRE(build(doc, &p));
    doc.beginAction("Pose");
    setJointAngle(doc, p.arm, 45.f);
    doc.endAction();
    const int at = capturePose(doc, p.torso, 0);
    REQUIRE(at == 1);
    const ls::SpriteId frame = readFrames(doc)[1].sprite;
    const ls::RasterBuffer taken = picture(doc, frame);

    doc.beginAction("Pose");
    setJointAngle(doc, p.arm, -80.f);
    doc.endAction();
    drawLayer(doc, "unused", kHead, { 1.f, 1.f }, { 3.f, 3.f });   // into the first frame
    ShapeParams more;
    more.from = { 20.f, 20.f };
    more.to = { 22.f, 22.f };
    ShapeLayer made;
    REQUIRE(addShapeTo(doc, layerOrder(doc, p.torso).front(), ShapeKind::Rectangle, more, kHead,
                       ls::kColorRoleNone, &made));
    CHECK(picture(doc, frame).pixels == taken.pixels);

    // Its layers are the parts' shapes, each placed by a move: nothing baked.
    const std::vector<ls::LayerId> layers = layerOrder(doc, frame);
    CHECK(layers.size() >= 4);
    bool placed = false;
    for (ls::LayerId layer : layers) {
        auto ops = doc.engine().getLayerOperations(layer);
        for (const ls::OperationInfo& op : ops.ok() ? ops.value : std::vector<ls::OperationInfo>{}) {
            placed = placed || op.type == "MatrixTransformOp";
        }
    }
    CHECK(placed);
}

// Every capture puts each part on its own track -- the same one each time --
// and the frames keep every track they had.
void testCapturesShareTracks() {
    Document doc;
    Puppet p;
    REQUIRE(build(doc, &p));
    REQUIRE(tracksOn(doc));
    // The first frame gets a layer of its own before any capture.
    const ls::LayerId notes = drawLayer(doc, "notes", kHead, { 1.f, 1.f }, { 4.f, 4.f });
    REQUIRE(notes.valid());
    const std::string notesKey = ensureTrackKey(doc, notes);

    REQUIRE(capturePose(doc, p.torso, 0) == 1);
    doc.beginAction("Pose");
    setJointAngle(doc, p.arm, 60.f);
    doc.endAction();
    REQUIRE(capturePose(doc, p.torso, 1) == 2);

    const std::vector<Frame> frames = readFrames(doc);
    REQUIRE(frames.size() == 3);
    const auto keys = [&](ls::SpriteId frame) {
        std::vector<std::string> out;
        for (ls::LayerId layer : layerOrder(doc, frame)) {
            out.push_back(trackKey(doc, layer));
        }
        return out;
    };
    CHECK(keys(frames[1].sprite) == keys(frames[2].sprite));
    CHECK(keys(frames[0].sprite) == keys(frames[1].sprite));
    // The notes track is in every frame, and the arm's track too.
    const std::string armKey = trackKey(doc, layerOrder(doc, p.arm).front());
    for (const Frame& f : frames) {
        CHECK(layerOfTrack(doc, f.sprite, notesKey).valid());
        CHECK(layerOfTrack(doc, f.sprite, armKey).valid());
    }
    // The first frame's arm row is empty; the captures' are not.
    CHECK(count(picture(doc, frames[0].sprite), kArm) == 0);
    CHECK(count(picture(doc, frames[1].sprite), kArm) > 0);
}

void testThePoseIsWrittenOnTheFrame() {
    Document doc;
    Puppet p;
    REQUIRE(build(doc, &p));
    doc.beginAction("Pose");
    setRootOffset(doc, p.torso, { 2.f, -1.f });
    setJointAngle(doc, p.arm, 90.f);
    setJointAngle(doc, p.forearm, -30.f);
    doc.endAction();
    const int at = capturePose(doc, p.torso, 0);
    REQUIRE(at == 1);
    CapturedPose pose;
    REQUIRE(readCapturedPose(doc, readFrames(doc)[1].sprite, &pose));
    CHECK(pose.root == "torso");
    CHECK(nearPoint(pose.rootOffset, { 2.f, -1.f }));
    bool arm = false, forearm = false;
    for (const auto& [name, angle] : pose.angles) {
        arm = arm || (name == "arm" && near(angle, 90.f));
        forearm = forearm || (name == "forearm" && near(angle, -30.f));
    }
    CHECK(arm && forearm);
    CHECK(!readCapturedPose(doc, readFrames(doc)[0].sprite, &pose));
}

// --- the rest of the rig ---------------------------------------------------

void testUndoAndTheFile() {
    Document doc;
    Puppet p;
    REQUIRE(build(doc, &p));
    doc.beginAction("Pose");
    setJointAngle(doc, p.arm, 70.f);
    doc.endAction();
    REQUIRE(capturePose(doc, p.torso, 0) == 1);
    const ls::RasterBuffer captured = picture(doc, readFrames(doc)[1].sprite);
    const ls::RasterBuffer posed = assembly(doc, p.torso);

    // One undo takes the capture away, and leaves the pose.
    REQUIRE(doc.undo());
    CHECK(readFrames(doc).size() == 1);
    CHECK(near(jointAngle(doc, p.arm), 70.f));
    REQUIRE(doc.redo());
    CHECK(readFrames(doc).size() == 2);

    const std::string path = "fast_puppet_test.lsprite";
    std::string error;
    REQUIRE(doc.save(path, &error));
    Document opened;
    REQUIRE(opened.open(path, &error));
    CHECK(partsOf(opened).size() == 4);
    CHECK(readFrames(opened).size() == 2);
    const std::vector<ls::SpriteId> roots = puppetRoots(opened);
    REQUIRE(roots.size() == 1);
    CHECK(partName(opened, roots.front()) == "torso");
    CHECK(assembly(opened, roots.front()).pixels == posed.pixels);
    CHECK(picture(opened, readFrames(opened)[1].sprite).pixels == captured.pixels);
    // The joints are the engine's own: an attachment per hung part.
    int hung = 0;
    for (ls::SpriteId part : partsOf(opened)) {
        hung += opened.engine().getAttachment(part).ok() ? 1 : 0;
    }
    CHECK(hung == 3);
    std::remove(path.c_str());
}

// Moving a joint keeps the part where it is drawn -- turned or not -- and so
// does taking a part off its socket.
void testMovingAJointAndUnhanging() {
    Document doc;
    Puppet p;
    REQUIRE(build(doc, &p));
    doc.beginAction("Pose");
    setJointAngle(doc, p.forearm, 40.f);
    doc.endAction();
    const ls::RasterBuffer before = assembly(doc, p.torso);
    REQUIRE(setJoint(doc, p.forearm, { 29.5f, 30.5f }));
    CHECK(assembly(doc, p.torso).pixels == before.pixels);
    CHECK(nearPoint(jointPosition(doc, p.forearm), { 29.5f, 30.5f }));

    REQUIRE(unhangPart(doc, p.forearm));
    CHECK(!parentPart(doc, p.forearm).valid());
    CHECK(puppetRoots(doc).size() == 2);
    // Drawn where it was, now on its own.
    const ls::RasterBuffer forearm = assembly(doc, p.forearm);
    CHECK(count(forearm, kForearm) == count(before, kForearm));

    // Deleting a part lets go of what hung from it.
    REQUIRE(deletePart(doc, p.torso));
    CHECK(partsOf(doc).size() == 3);
    CHECK(!parentPart(doc, p.arm).valid());
    CHECK(!parentPart(doc, p.head).valid());
}


// The canvas turned or flipped under a posed puppet and a capture of it: both
// come out exactly turned or flipped, and every joint lands where the canvas
// sends it -- the rig hangs together through it.
int offBy(const ls::RasterBuffer& before, const ls::RasterBuffer& after, bool flip) {
    int off = 0;
    for (int y = 0; y < kSize; ++y) {
        for (int x = 0; x < kSize; ++x) {
            const int tx = flip ? kSize - 1 - x : kSize - 1 - y;
            const int ty = flip ? y : x;
            const ls::Color a = ls::readPixel(before, x, y);
            const ls::Color b = ls::readPixel(after, tx, ty);
            off += a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a ? 0 : 1;
        }
    }
    return off;
}

void testTheCanvasCarriesThePuppet() {
    Document doc;
    Puppet p;
    REQUIRE(build(doc, &p));
    doc.beginAction("Pose");
    setRootOffset(doc, p.torso, { 2.f, 1.f });
    setJointAngle(doc, p.arm, 90.f);
    setJointAngle(doc, p.forearm, -90.f);
    doc.endAction();
    REQUIRE(capturePose(doc, p.torso, 0) == 1);
    const ls::RasterBuffer posed = assembly(doc, p.torso);
    const ls::RasterBuffer taken = picture(doc, readFrames(doc)[1].sprite);
    std::string error;

    for (bool flip : { false, true }) {
        REQUIRE(flip ? flipCanvas(doc, true, &error) : rotateCanvas(doc, 1, &error));
        const int puppetOff = offBy(posed, assembly(doc, p.torso), flip);
        const int captureOff = offBy(taken, picture(doc, readFrames(doc)[1].sprite), flip);
        if (puppetOff != 0 || captureOff != 0) {
            std::printf("  canvas %s: puppet %d, capture %d pixel(s) off\n",
                        flip ? "flipped" : "turned", puppetOff, captureOff);
        }
        CHECK(puppetOff == 0);
        CHECK(captureOff == 0);
        // The pose reads the same, the other way round for a flip.
        CHECK(near(jointAngle(doc, p.arm), flip ? -90.f : 90.f));
        REQUIRE(doc.undo());
        CHECK(assembly(doc, p.torso).pixels == posed.pixels);
    }

    // At any angle, the joints land where the canvas sends them.
    doc.beginAction("Pose");
    setJointAngle(doc, p.arm, 37.f);
    setJointAngle(doc, p.forearm, 21.f);
    doc.endAction();
    const ls::Vec2f hand = doc.engine().getPivotWorldPosition(jointOf(doc, p.forearm)).value;
    REQUIRE(rotateCanvas(doc, 1, &error));
    CHECK(nearPoint(doc.engine().getPivotWorldPosition(jointOf(doc, p.forearm)).value,
                    { kSize - hand.y, hand.x }, 0.001f));
}

} // namespace

int main() {
    testSplittingADrawingChangesNoPixel();
    testPartsAreNotFrames();
    testAPosedPartTurnsAboutItsJoint();
    testACaptureIsThePoseExactly();
    testACaptureStandsOnItsOwn();
    testCapturesShareTracks();
    testThePoseIsWrittenOnTheFrame();
    testUndoAndTheFile();
    testMovingAJointAndUnhanging();
    testTheCanvasCarriesThePuppet();
    if (failures == 0) {
        std::printf("fast_puppet: all checks passed\n");
        return 0;
    }
    std::printf("fast_puppet: %d check(s) failed\n", failures);
    return 1;
}
