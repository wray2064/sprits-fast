// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors
#pragma once

// puppet.h — cutout puppets: parts that hang from each other, posed by hand
// and captured, pose by pose, as frames.
//
// A puppet is built from the engine's own anchors, so that another program
// reading the document -- Pract, which adds IK, movement ramps and key poses
// on top -- finds the puppet it is, with nothing to convert:
//
//   - A **part** is a sprite of the document that is not a frame (kept after
//     the frames and tilesets, like them out of the timeline), named in its
//     metadata. It is drawn like a frame: layers, shapes, everything.
//   - Its **joint** is its pivot: the point it turns about, and the point it
//     presents when it hangs.
//   - A **socket** is a named point on a part where another hangs.
//   - A part **hangs** from a socket by an engine attachment, its joint on
//     the socket. The part a puppet starts from hangs from nothing: its root.
//   - A **pose** is how each joint is turned -- the rotation at the joint,
//     which is the attachment's joint transform -- and where the root stands
//     and how it is turned, which is the root's own transform.
//
// Posing is by hand; there are no solvers here. **Capturing** a pose makes a
// frame of it: each part's layers, copied in the puppet's drawing order, with
// the part's placement as their last move -- still shapes, drawn exactly
// where the posed puppet draws them, and after that a frame like any other,
// to fix by hand. The pose it was taken from is written on the frame
// (kPoseKey), part by part, for whatever wants to know it.
//
// Each part's layer is a track of its own (tracks.h): every capture of the
// same puppet puts the same part on the same row of the timeline.

#include "app/document.h"

#include <cmath>
#include <string>
#include <vector>

namespace fast {

constexpr const char* kPartKey = "fast.part";     // on a part: its name
constexpr const char* kPoseKey = "fast.pose";     // on a captured frame: its pose
constexpr const char* kJointName = "joint";       // a part's pivot

constexpr size_t kMaxParts = 256;

// ------------------------------------------------------------------ parts --

bool isPart(Document& doc, ls::SpriteId sprite);
// Every part, in the document's order.
std::vector<ls::SpriteId> partsOf(Document& doc);
std::string partName(Document& doc, ls::SpriteId part);
bool renamePart(Document& doc, ls::SpriteId part, const std::string& name);

// A new part with one empty layer and its joint at `joint` (canvas space: a
// part is drawn where it sits on the character). One undo step. Null when the
// document has as many parts as it may.
ls::SpriteId addPart(Document& doc, const std::string& name, ls::Vec2f joint);

// A part made of a frame's layer: the layer moves out of the frame into a new
// part, drawn where it was, with its joint at `joint`. One undo step.
ls::SpriteId partFromLayer(Document& doc, ls::LayerId layer, const std::string& name,
                           ls::Vec2f joint);

// Deletes a part; what hung from it hangs from nothing. One undo step.
bool deletePart(Document& doc, ls::SpriteId part);

// ----------------------------------------------------- joints and sockets --

// Where a joint is put for the pixel whose top-left corner is `pixel`: in its
// middle, as a line's end is (linePoint). A joint in the middle of a pixel,
// or on a corner, turns a quarter sending every pixel to a pixel, so a limb
// posed at 90 is the limb turned, exactly. Halfway along an edge -- the
// middle one way, a corner the other -- it cannot be: the turned shape's
// edges then run through pixel middles, half of each such pixel in it, and no
// rule decides that half the same from every side. The joint tool puts them
// here; the model takes any point.
inline ls::Vec2f jointPoint(ls::Vec2f pixel) {
    return { std::floor(pixel.x) + 0.5f, std::floor(pixel.y) + 0.5f };
}

ls::PivotId jointOf(Document& doc, ls::SpriteId part);
ls::Vec2f jointPosition(Document& doc, ls::SpriteId part);
// Moves the joint: where the part turns and hangs. What hangs from nothing
// moves on the canvas; what hangs stays hung, turning about the new point.
bool setJoint(Document& doc, ls::SpriteId part, ls::Vec2f position);

struct PartSocket {
    ls::SocketId id;
    std::string  name;
    ls::Vec2f    position;
};
std::vector<PartSocket> socketsOf(Document& doc, ls::SpriteId part);
ls::SocketId addSocketTo(Document& doc, ls::SpriteId part, const std::string& name,
                         ls::Vec2f position);
bool moveSocketTo(Document& doc, ls::SocketId socket, ls::Vec2f position);
bool removeSocketFrom(Document& doc, ls::SocketId socket);

// --------------------------------------------------------------- hanging --

// Hangs `child` from `socket` by its joint, unturned. Refused when that would
// make a loop, or when `child` is not a part. One undo step.
bool hangPart(Document& doc, ls::SpriteId child, ls::SocketId socket, bool behind = false);
// Hangs `child` from `parent` where it is drawn: a socket is made on the
// parent at the child's joint, so hanging moves nothing. One undo step.
ls::SocketId hangPartWhereItIs(Document& doc, ls::SpriteId child, ls::SpriteId parent,
                               const std::string& socketName);
bool unhangPart(Document& doc, ls::SpriteId child);
bool setBehindParent(Document& doc, ls::SpriteId child, bool behind);
// The part `part` hangs from, or null.
ls::SpriteId parentPart(Document& doc, ls::SpriteId part);
// The part at the top of `part`'s chain: its puppet's root.
ls::SpriteId rootPart(Document& doc, ls::SpriteId part);
// Every root, in the document's order: one per puppet.
std::vector<ls::SpriteId> puppetRoots(Document& doc);
// A puppet's parts in drawing order, bottom first.
std::vector<ls::SpriteId> puppetOrder(Document& doc, ls::SpriteId root);

// ------------------------------------------------------------------ pose --

// A hung part's joint angle, degrees clockwise; for a root, its turn.
float jointAngle(Document& doc, ls::SpriteId part);
// Turns a part at its joint -- or a root about its joint. Does not bracket an
// action: a drag is one.
bool setJointAngle(Document& doc, ls::SpriteId part, float degrees);
// Where a root stands, as how far its joint has moved from where it was drawn.
ls::Vec2f rootOffset(Document& doc, ls::SpriteId root);
bool setRootOffset(Document& doc, ls::SpriteId root, ls::Vec2f offset);
// Every joint back to 0 and the root where it was drawn. One undo step.
bool restPose(Document& doc, ls::SpriteId root);

// Where `part` lands on the canvas as posed: the whole chain above it.
ls::Mat3f partPlacement(Document& doc, ls::SpriteId part);

// ---------------------------------------------------------------- capture --

// A new frame after frame `after` holding the puppet as posed (see the top of
// this file), its pose written on it. Returns the new frame's index, or -1.
// One undo step.
int capturePose(Document& doc, ls::SpriteId root, int after);

// The pose a captured frame was taken from, part name to joint angle, with
// the root's offset: what capturePose wrote. False for a frame not captured.
struct CapturedPose {
    std::string root;
    ls::Vec2f   rootOffset { 0.f, 0.f };
    std::vector<std::pair<std::string, float>> angles;   // the root's first
};
bool readCapturedPose(Document& doc, ls::SpriteId frame, CapturedPose* out);

} // namespace fast
