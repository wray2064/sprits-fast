// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

#include "app/puppet.h"

#include "app/animation.h"
#include "app/layers.h"
#include "app/paint.h"
#include "app/shape.h"
#include "app/tracks.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <sstream>

namespace fast {

namespace {

constexpr float kPi = 3.14159265358979323846f;

// A name as metadata keeps it: bounded, and without the characters the pose
// text separates on.
std::string cleanName(const std::string& name) {
    std::string out;
    for (char c : name) {
        out += (c == '\t' || c == '\n' || c == '\r') ? ' ' : c;
    }
    if (out.size() > kMaxNameLength) {
        out.resize(kMaxNameLength);
    }
    return out.empty() ? std::string("part") : out;
}

float angleOf(const ls::Mat3f& m) {
    return std::atan2(m.m[3], m.m[0]) * 180.f / kPi;
}

// A root's transform: turned about its joint, then moved.
ls::Mat3f rootTransform(ls::Vec2f joint, ls::Vec2f offset, float degrees) {
    return ls::Mat3f::translation(offset).mul(
        ls::Mat3f::aroundPivot(ls::Mat3f::rotation(degrees), joint));
}

bool attachedHow(Document& doc, ls::SpriteId part, ls::AttachmentInfo* out) {
    auto info = doc.engine().getAttachment(part);
    if (info.fail()) {
        return false;
    }
    *out = info.value;
    return true;
}

// Re-attaches with a new joint transform, keeping everything else.
bool reattach(Document& doc, ls::SpriteId part, const ls::AttachmentInfo& how,
              const ls::Mat3f& joint) {
    ls::AttachmentDesc desc;
    desc.socket = how.socket;
    desc.childPivot = how.childPivot;
    desc.localOffset = joint;
    desc.behindParent = how.behindParent;
    return doc.engine().attachSprite(part, desc).ok();
}

// Frames, then tilesets, then parts: the order a document keeps.
bool keepPartsLast(Document& doc) {
    auto info = doc.engine().getDocumentInfo(doc.id());
    if (info.fail()) {
        return false;
    }
    std::vector<ls::SpriteId> order;
    std::vector<ls::SpriteId> parts;
    for (ls::SpriteId sprite : info.value.sprites) {
        (isPart(doc, sprite) ? parts : order).push_back(sprite);
    }
    order.insert(order.end(), parts.begin(), parts.end());
    return order == info.value.sprites || doc.engine().setSpriteOrder(doc.id(), order).ok();
}

std::string poseText(Document& doc, ls::SpriteId root) {
    std::ostringstream out;
    const ls::Vec2f offset = rootOffset(doc, root);
    char line[160];
    std::snprintf(line, sizeof(line), "root\t%s\t%.4f\t%.4f\n", partName(doc, root).c_str(),
                  static_cast<double>(offset.x), static_cast<double>(offset.y));
    out << line;
    for (ls::SpriteId part : puppetOrder(doc, root)) {
        std::snprintf(line, sizeof(line), "%s\t%.4f\n", partName(doc, part).c_str(),
                      static_cast<double>(jointAngle(doc, part)));
        out << line;
    }
    return out.str();
}

} // namespace

// ------------------------------------------------------------------ parts --

bool isPart(Document& doc, ls::SpriteId sprite) {
    return doc.engine().getMetadata(sprite.value, kPartKey).ok();
}

std::vector<ls::SpriteId> partsOf(Document& doc) {
    std::vector<ls::SpriteId> parts;
    auto info = doc.engine().getDocumentInfo(doc.id());
    if (info.fail()) {
        return parts;
    }
    for (ls::SpriteId sprite : info.value.sprites) {
        if (isPart(doc, sprite)) {
            parts.push_back(sprite);
        }
    }
    return parts;
}

std::string partName(Document& doc, ls::SpriteId part) {
    auto name = doc.engine().getMetadata(part.value, kPartKey);
    return name.ok() ? name.value : std::string();
}

bool renamePart(Document& doc, ls::SpriteId part, const std::string& name) {
    if (!isPart(doc, part)) {
        return false;
    }
    doc.beginAction("Rename part");
    const bool ok = doc.engine().setMetadata(part.value, kPartKey, cleanName(name)).ok();
    ok ? doc.endAction() : doc.abandonAction();
    return ok;
}

namespace {

// A part with no layers yet: the sprite, its name and its joint.
ls::SpriteId makePart(Document& doc, const std::string& name, ls::Vec2f joint) {
    if (partsOf(doc).size() >= kMaxParts) {
        return ls::SpriteId{};
    }
    auto made = doc.engine().createSprite(doc.id());
    if (made.fail()) {
        return ls::SpriteId{};
    }
    const ls::SpriteId part = made.value;
    if (doc.engine().setMetadata(part.value, kPartKey, cleanName(name)).fail() ||
        doc.engine().createPivot(part, ls::PivotDesc{ kJointName, joint }).fail() ||
        !keepPartsLast(doc)) {
        return ls::SpriteId{};
    }
    return part;
}

} // namespace

ls::SpriteId addPart(Document& doc, const std::string& name, ls::Vec2f joint) {
    doc.beginAction("New part");
    const ls::SpriteId part = makePart(doc, name, joint);
    PaintLayer layer;
    if (!part.valid() || !createPaintLayer(doc, part, "Layer 1", ls::Color{ 0, 0, 0, 255 }, &layer)) {
        doc.abandonAction();
        return ls::SpriteId{};
    }
    doc.endAction();
    return part;
}

ls::SpriteId partFromLayer(Document& doc, ls::LayerId layer, const std::string& name,
                           ls::Vec2f joint) {
    auto info = doc.engine().getLayerInfo(layer);
    if (info.fail() || isPart(doc, info.value.sprite)) {
        return ls::SpriteId{};
    }
    doc.beginAction("Make part");
    const ls::SpriteId part = makePart(doc, name, joint);
    if (!part.valid() || doc.engine().cloneLayer(layer, part, -1).fail() ||
        doc.engine().deleteLayer(layer).fail()) {
        doc.abandonAction();
        return ls::SpriteId{};
    }
    doc.endAction();
    return part;
}

bool deletePart(Document& doc, ls::SpriteId part) {
    if (!isPart(doc, part)) {
        return false;
    }
    doc.beginAction("Delete part");
    const bool ok = doc.engine().deleteSprite(part).ok();
    ok ? doc.endAction() : doc.abandonAction();
    return ok;
}

// ----------------------------------------------------- joints and sockets --

ls::PivotId jointOf(Document& doc, ls::SpriteId part) {
    auto found = doc.engine().findPivot(part, kJointName);
    if (found.ok()) {
        return found.value;
    }
    auto info = doc.engine().getSpriteInfo(part);
    return info.ok() ? info.value.pivot : ls::PivotId{};
}

ls::Vec2f jointPosition(Document& doc, ls::SpriteId part) {
    auto at = doc.engine().getPivot(jointOf(doc, part));
    return at.ok() ? at.value : ls::Vec2f{ 0.f, 0.f };
}

bool setJoint(Document& doc, ls::SpriteId part, ls::Vec2f position) {
    const ls::PivotId joint = jointOf(doc, part);
    auto was = doc.engine().getPivot(joint);
    if (was.fail()) {
        return false;
    }
    doc.beginAction("Move joint");
    // A part that hangs keeps where it is drawn: its socket, when it is the
    // only thing hanging there, moves with the joint -- by the joint's step
    // as the socket frame and the joint's turn carry it.
    ls::AttachmentInfo how;
    if (attachedHow(doc, part, &how)) {
        auto hanging = doc.engine().getAttachedSprites(how.socket);
        auto socket = doc.engine().getSocketTransform(how.socket);
        auto desc = doc.engine().getSocket(how.socket);
        if (hanging.ok() && hanging.value.size() == 1 && socket.ok() && desc.ok()) {
            const ls::Mat3f carry = socket.value.mul(how.localOffset);
            const ls::Vec2f step = carry.transformVector({ position.x - was.value.x,
                                                           position.y - was.value.y });
            doc.engine().moveSocket(how.socket, { desc.value.position.x + step.x,
                                                  desc.value.position.y + step.y });
        }
    }
    const bool ok = doc.engine().setPivot(joint, position).ok();
    ok ? doc.endAction() : doc.abandonAction();
    return ok;
}

std::vector<PartSocket> socketsOf(Document& doc, ls::SpriteId part) {
    std::vector<PartSocket> out;
    auto info = doc.engine().getSpriteInfo(part);
    if (info.fail()) {
        return out;
    }
    for (ls::SocketId id : info.value.sockets) {
        auto desc = doc.engine().getSocket(id);
        if (desc.ok()) {
            out.push_back({ id, desc.value.name, desc.value.position });
        }
    }
    return out;
}

ls::SocketId addSocketTo(Document& doc, ls::SpriteId part, const std::string& name,
                         ls::Vec2f position) {
    if (!isPart(doc, part)) {
        return ls::SocketId{};
    }
    doc.beginAction("Add socket");
    auto made = doc.engine().addSocket(part, { cleanName(name), position, 0.f, { 1.f, 1.f } });
    made.ok() ? doc.endAction() : doc.abandonAction();
    return made.ok() ? made.value : ls::SocketId{};
}

bool moveSocketTo(Document& doc, ls::SocketId socket, ls::Vec2f position) {
    doc.beginAction("Move socket");
    const bool ok = doc.engine().moveSocket(socket, position).ok();
    ok ? doc.endAction() : doc.abandonAction();
    return ok;
}

bool removeSocketFrom(Document& doc, ls::SocketId socket) {
    doc.beginAction("Remove socket");
    const bool ok = doc.engine().removeSocket(socket).ok();
    ok ? doc.endAction() : doc.abandonAction();
    return ok;
}

// --------------------------------------------------------------- hanging --

bool hangPart(Document& doc, ls::SpriteId child, ls::SocketId socket, bool behind) {
    if (!isPart(doc, child)) {
        return false;
    }
    ls::AttachmentDesc desc;
    desc.socket = socket;
    desc.childPivot = jointOf(doc, child);
    desc.behindParent = behind;
    doc.beginAction("Hang part");
    const bool ok = doc.engine().attachSprite(child, desc).ok();
    ok ? doc.endAction() : doc.abandonAction();
    return ok;
}

ls::SocketId hangPartWhereItIs(Document& doc, ls::SpriteId child, ls::SpriteId parent,
                               const std::string& socketName) {
    if (!isPart(doc, child) || !isPart(doc, parent) || child == parent) {
        return ls::SocketId{};
    }
    // The child's joint where it shows, in the parent's own space.
    const ls::Vec2f shown = partPlacement(doc, child).transformPoint(jointPosition(doc, child));
    auto into = partPlacement(doc, parent).inverse();
    if (into.fail()) {
        return ls::SocketId{};
    }
    doc.beginAction("Hang part");
    // The child's own transform went into standing where it stands; hung, the
    // socket and the joint do that, so the transform is cleared -- and a turn
    // it had against the parent goes into the joint:
    //   parent * socket * joint * pivot^-1 = what it was.
    const ls::Vec2f socketAt = into.value.transformPoint(shown);
    auto made = doc.engine().addSocket(parent, { cleanName(socketName), socketAt, 0.f, { 1.f, 1.f } });
    const ls::Vec2f pivot = jointPosition(doc, child);
    const ls::Mat3f turn = ls::Mat3f::translation({ -socketAt.x, -socketAt.y })
                               .mul(into.value)
                               .mul(partPlacement(doc, child))
                               .mul(ls::Mat3f::translation(pivot));
    ls::AttachmentDesc desc;
    desc.socket = made.ok() ? made.value : ls::SocketId{};
    desc.childPivot = jointOf(doc, child);
    desc.localOffset = turn;
    if (made.fail() || doc.engine().setSpriteTransform(child, ls::Mat3f::identity()).fail() ||
        doc.engine().attachSprite(child, desc).fail()) {
        doc.abandonAction();
        return ls::SocketId{};
    }
    doc.endAction();
    return made.value;
}

bool unhangPart(Document& doc, ls::SpriteId child) {
    // It stays where it shows: its placement becomes its own transform.
    const ls::Mat3f shown = partPlacement(doc, child);
    doc.beginAction("Unhang part");
    const bool ok = doc.engine().detachSprite(child).ok() &&
                    doc.engine().setSpriteTransform(child, shown).ok();
    ok ? doc.endAction() : doc.abandonAction();
    return ok;
}

bool setBehindParent(Document& doc, ls::SpriteId child, bool behind) {
    ls::AttachmentInfo how;
    if (!attachedHow(doc, child, &how)) {
        return false;
    }
    how.behindParent = behind;
    doc.beginAction(behind ? "Behind its parent" : "In front of its parent");
    const bool ok = reattach(doc, child, how, how.localOffset);
    ok ? doc.endAction() : doc.abandonAction();
    return ok;
}

ls::SpriteId parentPart(Document& doc, ls::SpriteId part) {
    ls::AttachmentInfo how;
    return attachedHow(doc, part, &how) ? how.parent : ls::SpriteId{};
}

ls::SpriteId rootPart(Document& doc, ls::SpriteId part) {
    std::set<uint64_t> seen;
    ls::SpriteId at = part;
    while (seen.insert(at.value).second) {
        const ls::SpriteId up = parentPart(doc, at);
        if (!up.valid()) {
            break;
        }
        at = up;
    }
    return at;
}

std::vector<ls::SpriteId> puppetRoots(Document& doc) {
    std::vector<ls::SpriteId> roots;
    for (ls::SpriteId part : partsOf(doc)) {
        if (!parentPart(doc, part).valid()) {
            roots.push_back(part);
        }
    }
    return roots;
}

std::vector<ls::SpriteId> puppetOrder(Document& doc, ls::SpriteId root) {
    auto order = doc.engine().assemblyOrder(root);
    return order.ok() ? order.value : std::vector<ls::SpriteId>{};
}

// ------------------------------------------------------------------ pose --

float jointAngle(Document& doc, ls::SpriteId part) {
    ls::AttachmentInfo how;
    if (attachedHow(doc, part, &how)) {
        return angleOf(how.localOffset);
    }
    auto own = doc.engine().getSpriteTransform(part);
    return own.ok() ? angleOf(own.value) : 0.f;
}

bool setJointAngle(Document& doc, ls::SpriteId part, float degrees) {
    ls::AttachmentInfo how;
    if (attachedHow(doc, part, &how)) {
        return reattach(doc, part, how, ls::Mat3f::rotation(degrees));
    }
    if (!isPart(doc, part)) {
        return false;
    }
    return doc.engine().setSpriteTransform(
        part, rootTransform(jointPosition(doc, part), rootOffset(doc, part), degrees)).ok();
}

ls::Vec2f rootOffset(Document& doc, ls::SpriteId root) {
    auto own = doc.engine().getSpriteTransform(root);
    if (own.fail()) {
        return { 0.f, 0.f };
    }
    const ls::Vec2f joint = jointPosition(doc, root);
    const ls::Vec2f moved = own.value.transformPoint(joint);
    return { moved.x - joint.x, moved.y - joint.y };
}

bool setRootOffset(Document& doc, ls::SpriteId root, ls::Vec2f offset) {
    if (!isPart(doc, root) || parentPart(doc, root).valid()) {
        return false;
    }
    return doc.engine().setSpriteTransform(
        root, rootTransform(jointPosition(doc, root), offset, jointAngle(doc, root))).ok();
}

bool restPose(Document& doc, ls::SpriteId root) {
    doc.beginAction("Rest pose");
    bool ok = doc.engine().setSpriteTransform(root, ls::Mat3f::identity()).ok();
    for (ls::SpriteId part : puppetOrder(doc, root)) {
        ls::AttachmentInfo how;
        if (part != root && attachedHow(doc, part, &how)) {
            ok = reattach(doc, part, how, ls::Mat3f::identity()) && ok;
        }
    }
    ok ? doc.endAction() : doc.abandonAction();
    return ok;
}

ls::Mat3f partPlacement(Document& doc, ls::SpriteId part) {
    auto world = doc.engine().getSpriteWorldTransform(part);
    return world.ok() ? world.value : ls::Mat3f::identity();
}

ls::SpriteId partAt(Document& doc, ls::SpriteId root, ls::Vec2f point) {
    auto size = doc.engine().getCanvasSize(doc.id());
    if (size.fail()) {
        return ls::SpriteId{};
    }
    const ls::CompileProfile profile = compileProfile(
        ls::CompileProfileType::Preview, static_cast<uint32_t>(size.value.x),
        static_cast<uint32_t>(size.value.y));
    std::vector<ls::SpriteId> order = puppetOrder(doc, root);
    for (auto it = order.rbegin(); it != order.rend(); ++it) {
        // The point where the part's own picture has it: its own transform
        // is in that picture, the chain above it is not.
        auto own = doc.engine().getSpriteTransform(*it);
        auto undo = partPlacement(doc, *it).inverse();
        if (own.fail() || undo.fail()) {
            continue;
        }
        const ls::Vec2f local = own.value.mul(undo.value).transformPoint(point);
        auto compiled = doc.engine().compileSprite(*it, profile);
        if (compiled.ok() &&
            ls::readPixel(compiled.value.raster, static_cast<int32_t>(std::floor(local.x)),
                          static_cast<int32_t>(std::floor(local.y))).a != 0) {
            return *it;
        }
    }
    return ls::SpriteId{};
}

int hangingDepth(Document& doc, ls::SpriteId part) {
    int depth = 0;
    std::set<uint64_t> seen { part.value };
    for (ls::SpriteId up = parentPart(doc, part); up.valid() && seen.insert(up.value).second;
         up = parentPart(doc, up)) {
        ++depth;
    }
    return depth;
}

// ---------------------------------------------------------------- capture --

int capturePose(Document& doc, ls::SpriteId root, int after) {
    if (!isPart(doc, root)) {
        return -1;
    }
    const std::vector<ls::SpriteId> order = puppetOrder(doc, root);
    const std::vector<Frame> before = readFrames(doc);
    doc.beginAction("Capture pose");
    const int at = addFrame(doc, after);
    const std::vector<Frame> frames = readFrames(doc);
    if (at < 0 || at >= static_cast<int>(frames.size())) {
        doc.abandonAction();
        return -1;
    }
    const ls::SpriteId frame = frames[static_cast<size_t>(at)].sprite;
    const bool tracks = tracksOn(doc) && !before.empty();
    if (tracks) {
        // Every track the other frames have, empty, before the capture fills
        // its own: a frame without them would, followed, empty the rest.
        const int from = std::clamp(after, 0, static_cast<int>(before.size()) - 1);
        syncTracks(doc, before[static_cast<size_t>(from)].sprite);
    }

    for (ls::SpriteId part : order) {
        const ls::Mat3f placement = partPlacement(doc, part);
        const std::string name = partName(doc, part);
        const std::vector<ls::LayerId> layers = layerOrder(doc, part);
        for (ls::LayerId layer : layers) {
            // The part's layer is a track of its own, the same in every
            // capture of this puppet.
            const std::string key = ensureTrackKey(doc, layer);
            int index = -1;
            if (tracks) {
                const ls::LayerId empty = layerOfTrack(doc, frame, key);
                if (empty.valid()) {
                    index = indexOfLayer(doc, frame, empty);
                    doc.engine().deleteLayer(empty);
                }
            }
            auto copy = doc.engine().cloneLayer(layer, frame, index);
            if (copy.fail()) {
                doc.abandonAction();
                return -1;
            }
            doc.engine().setMetadata(copy.value.value, "fast.track", key);
            auto info = doc.engine().getLayerInfo(layer);
            doc.engine().setLayerName(copy.value, layers.size() > 1 && info.ok()
                                                      ? name + " " + info.value.name : name);
            ls::MatrixTransformOp place;
            place.targetLayer = copy.value;
            place.matrix = placement;
            place.rounding = ls::RoundingPolicy::Exact;
            if (doc.engine().addOperation(copy.value, place).fail()) {
                doc.abandonAction();
                return -1;
            }
            keepEffectsLast(doc, copy.value);
        }
    }
    doc.engine().setMetadata(frame.value, kPoseKey, poseText(doc, root));
    if (tracks) {
        syncTracks(doc, frame);
    }
    doc.endAction();
    return at;
}

bool readCapturedPose(Document& doc, ls::SpriteId frame, CapturedPose* out) {
    auto text = doc.engine().getMetadata(frame.value, kPoseKey);
    if (text.fail() || out == nullptr) {
        return false;
    }
    CapturedPose pose;
    std::istringstream in(text.value);
    std::string line;
    bool first = true;
    while (std::getline(in, line)) {
        std::vector<std::string> fields;
        std::string field;
        std::istringstream split(line);
        while (std::getline(split, field, '\t')) {
            fields.push_back(field);
        }
        if (first) {
            if (fields.size() != 4 || fields[0] != "root") {
                return false;
            }
            pose.root = fields[1];
            pose.rootOffset = { std::strtof(fields[2].c_str(), nullptr),
                                std::strtof(fields[3].c_str(), nullptr) };
            first = false;
        } else if (fields.size() == 2) {
            pose.angles.emplace_back(fields[0], std::strtof(fields[1].c_str(), nullptr));
        }
    }
    if (first) {
        return false;
    }
    *out = pose;
    return true;
}

} // namespace fast
