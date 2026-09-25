// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

#include "app/layers.h"

#include <algorithm>
#include <cstdlib>
#include <cstdio>

namespace fast {
namespace {

const std::vector<const char*> kBlendNames {
    "Normal", "Multiply", "Screen", "Overlay", "Add", "Subtract",
    "Darken", "Lighten", "Difference", "Erase", "Replace",
};

ls::SpriteId spriteOf(Document& doc, ls::LayerId layer) {
    auto info = doc.engine().getLayerInfo(layer);
    return info.ok() ? info.value.sprite : ls::SpriteId{};
}

// The group a layer belongs to at `index` of `order`, judged by its
// neighbours: inside a run of one group, that group. At the edge of a run --
// one neighbour in a group, the other not -- it stays in the group it was in
// if that is the one beside it, so raising a member to the top of its group
// keeps it a member, and a stranger dropped just outside stays outside.
// `moving` is skipped so a layer is not its own neighbour.
ls::GroupId groupAt(Document& doc, const std::vector<ls::LayerId>& order, size_t index,
                    ls::LayerId moving, ls::GroupId current) {
    ls::GroupId below, above;
    for (size_t i = index; i-- > 0;) {
        if (order[i] != moving) { below = groupOf(doc, order[i]); break; }
    }
    for (size_t i = index + 1; i < order.size(); ++i) {
        if (order[i] != moving) { above = groupOf(doc, order[i]); break; }
    }
    if (below.valid() && below == above) {
        return below;
    }
    if (current.valid() && (below == current || above == current)) {
        return current;
    }
    return ls::GroupId{};
}

// A group with no layers left in it is nothing a person can see or select,
// so it goes when its last member does.
void dropIfEmpty(Document& doc, ls::GroupId group) {
    if (!group.valid()) {
        return;
    }
    auto info = doc.engine().getGroupInfo(group);
    if (info.ok() && info.value.layers.empty()) {
        doc.engine().deleteGroup(group);
    }
}

} // namespace

const std::vector<const char*>& blendModeNames() { return kBlendNames; }

bool readLayerProps(Document& doc, ls::LayerId layer, LayerProps* out) {
    if (out == nullptr) {
        return false;
    }
    auto info = doc.engine().getLayerInfo(layer);
    if (info.fail()) {
        return false;
    }
    LayerProps props;
    props.name = info.value.name;
    props.opacity = info.value.opacity;
    props.blend = info.value.blend;
    props.visible = info.value.visible;
    props.group = info.value.parentId;
    props.locked = layerLocked(doc, layer);
    props.tagged = layerTag(doc, layer, &props.tag);
    props.notes = layerNotes(doc, layer);
    if (info.value.hasClip) {
        // The engine says only that there is one; which layer it is has to be
        // inferred: a clip made here is always to the layer below.
        const ls::SpriteId sprite = info.value.sprite;
        const std::vector<ls::LayerId> order = layerOrder(doc, sprite);
        const int at = indexOfLayer(doc, sprite, layer);
        if (at > 0) {
            props.clipBase = order[static_cast<size_t>(at) - 1];
        }
    }
    *out = props;
    return true;
}

bool setLayerOpacity(Document& doc, ls::LayerId layer, float opacity) {
    return doc.engine().setLayerOpacity(layer, std::clamp(opacity, 0.f, 1.f)).ok();
}

bool setLayerBlend(Document& doc, ls::LayerId layer, ls::BlendMode blend) {
    return doc.engine().setLayerBlendMode(layer, blend).ok();
}

bool setLayerVisible(Document& doc, ls::LayerId layer, bool visible) {
    return doc.engine().setLayerVisibility(layer, visible).ok();
}

bool renameLayer(Document& doc, ls::LayerId layer, const std::string& name) {
    return doc.engine().setLayerName(layer, name).ok();
}

bool layerLocked(Document& doc, ls::LayerId layer) {
    auto value = doc.engine().getMetadata(layer.value, kLayerLockedKey);
    return value.ok() && value.value == "1";
}

bool setLayerLocked(Document& doc, ls::LayerId layer, bool locked) {
    if (locked) {
        return doc.engine().setMetadata(layer.value, kLayerLockedKey, "1").ok();
    }
    doc.engine().clearMetadata(layer.value, kLayerLockedKey);
    return true;
}

bool layerTag(Document& doc, ls::LayerId layer, ls::Color* out) {
    auto value = doc.engine().getMetadata(layer.value, kLayerTagKey);
    if (value.fail() || value.value.size() != 7 || value.value[0] != '#') {
        return false;
    }
    char* end = nullptr;
    const unsigned long rgb = std::strtoul(value.value.c_str() + 1, &end, 16);
    if (end != value.value.c_str() + 7) {
        return false;
    }
    if (out != nullptr) {
        *out = ls::Color{ static_cast<uint8_t>((rgb >> 16) & 0xFF),
                          static_cast<uint8_t>((rgb >> 8) & 0xFF),
                          static_cast<uint8_t>(rgb & 0xFF), 255 };
    }
    return true;
}

bool setLayerTag(Document& doc, ls::LayerId layer, const ls::Color* tag) {
    if (tag == nullptr) {
        doc.engine().clearMetadata(layer.value, kLayerTagKey);
        return true;
    }
    char text[8];
    std::snprintf(text, sizeof(text), "#%02x%02x%02x", tag->r, tag->g, tag->b);
    return doc.engine().setMetadata(layer.value, kLayerTagKey, text).ok();
}

std::string layerNotes(Document& doc, ls::LayerId layer) {
    auto value = doc.engine().getMetadata(layer.value, kLayerNotesKey);
    return value.ok() ? value.value : std::string();
}

bool setLayerNotes(Document& doc, ls::LayerId layer, const std::string& notes) {
    if (notes.empty()) {
        doc.engine().clearMetadata(layer.value, kLayerNotesKey);
        return true;
    }
    return doc.engine().setMetadata(layer.value, kLayerNotesKey, notes).ok();
}

// --- order ----------------------------------------------------------------------

std::vector<ls::LayerId> layerOrder(Document& doc, ls::SpriteId sprite) {
    auto info = doc.engine().getSpriteInfo(sprite);
    return info.ok() ? info.value.layers : std::vector<ls::LayerId>{};
}

int indexOfLayer(Document& doc, ls::SpriteId sprite, ls::LayerId layer) {
    const std::vector<ls::LayerId> order = layerOrder(doc, sprite);
    for (size_t i = 0; i < order.size(); ++i) {
        if (order[i] == layer) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

bool moveLayer(Document& doc, ls::LayerId layer, int toIndex) {
    const ls::SpriteId sprite = spriteOf(doc, layer);
    std::vector<ls::LayerId> order = layerOrder(doc, sprite);
    const int from = indexOfLayer(doc, sprite, layer);
    if (from < 0 || order.empty()) {
        return false;
    }
    const int last = static_cast<int>(order.size()) - 1;
    const int to = std::clamp(toIndex, 0, last);
    if (to == from) {
        return true;
    }
    doc.beginAction("Move layer");
    order.erase(order.begin() + from);
    order.insert(order.begin() + to, layer);
    if (doc.engine().setLayerOrder(sprite, order).fail()) {
        doc.abandonAction();
        return false;
    }
    // Where it landed decides its group. A clip to the layer below no longer
    // means the same thing, so it is cleared rather than left pointing at a
    // layer that is now somewhere else.
    const ls::GroupId was = groupOf(doc, layer);
    const ls::GroupId wanted = groupAt(doc, order, static_cast<size_t>(to), layer, was);
    if (wanted != was) {
        doc.engine().setLayerParent(layer, wanted);
        dropIfEmpty(doc, was);
    }
    auto info = doc.engine().getLayerInfo(layer);
    if (info.ok() && info.value.hasClip) {
        doc.engine().clearLayerClip(layer);
    }
    doc.endAction();
    return true;
}

bool raiseLayer(Document& doc, ls::LayerId layer) {
    const int at = indexOfLayer(doc, spriteOf(doc, layer), layer);
    return at >= 0 && moveLayer(doc, layer, at + 1);
}

bool lowerLayer(Document& doc, ls::LayerId layer) {
    const int at = indexOfLayer(doc, spriteOf(doc, layer), layer);
    return at > 0 && moveLayer(doc, layer, at - 1);
}

// --- copies ---------------------------------------------------------------------

ls::LayerId duplicateLayer(Document& doc, ls::LayerId layer) {
    const ls::SpriteId sprite = spriteOf(doc, layer);
    const int at = indexOfLayer(doc, sprite, layer);
    auto info = doc.engine().getLayerInfo(layer);
    if (at < 0 || info.fail()) {
        return ls::LayerId{};
    }
    doc.beginAction("Duplicate layer");
    auto made = doc.engine().cloneLayer(layer, sprite, at + 1);
    if (made.fail()) {
        doc.abandonAction();
        return ls::LayerId{};
    }
    doc.engine().setLayerName(made.value, info.value.name + " copy");
    if (info.value.parentId.valid()) {
        doc.engine().setLayerParent(made.value, info.value.parentId);
    }
    if (layerLocked(doc, layer)) {
        setLayerLocked(doc, made.value, true);
    }
    doc.endAction();
    return made.value;
}

ls::LayerId pasteLayer(Document& doc, ls::LayerId source, ls::SpriteId into, int atIndex) {
    doc.beginAction("Paste layer");
    auto made = doc.engine().cloneLayer(source, into, atIndex);
    if (made.fail()) {
        doc.abandonAction();
        return ls::LayerId{};
    }
    if (atIndex >= 0) {
        const std::vector<ls::LayerId> order = layerOrder(doc, into);
        const int at = indexOfLayer(doc, into, made.value);
        const ls::GroupId wanted =
            groupAt(doc, order, static_cast<size_t>(at), made.value, ls::GroupId{});
        if (wanted.valid()) {
            doc.engine().setLayerParent(made.value, wanted);
        }
    }
    doc.endAction();
    return made.value;
}

// --- groups ---------------------------------------------------------------------

ls::LayerId mergeDown(Document& doc, ls::LayerId upper, std::string* why) {
    const auto refuse = [&](const char* reason) {
        if (why != nullptr) { *why = reason; }
        return ls::LayerId{};
    };
    ls::LSContext& engine = doc.engine();
    auto upperInfo = engine.getLayerInfo(upper);
    if (upperInfo.fail()) {
        return refuse("there is no such layer");
    }
    const ls::SpriteId sprite = upperInfo.value.sprite;
    const int at = indexOfLayer(doc, sprite, upper);
    if (at <= 0) {
        return refuse("there is no layer below this one to merge into");
    }
    const ls::LayerId lower = layerOrder(doc, sprite)[static_cast<size_t>(at - 1)];
    LayerProps top, bottom;
    if (!readLayerProps(doc, upper, &top) || !readLayerProps(doc, lower, &bottom)) {
        return refuse("the layers could not be read");
    }
    if (top.group != bottom.group) {
        return refuse("the layer below is in a different group");
    }
    if (!top.visible) {
        return refuse("this layer is hidden; merging would show it");
    }
    if (top.clipBase.valid() || bottom.clipBase.valid()) {
        return refuse("a clipped layer draws only where another does; unclip it first");
    }
    if (layerLocked(doc, lower)) {
        return refuse("the layer below is locked");
    }

    // Anything that acts on a layer as a whole -- a transform, an outline --
    // would, after a merge, act on both layers' drawings. Refused rather than
    // silently extended.
    const auto wholeLayerRules = [&](ls::LayerId layer) {
        auto operations = engine.getLayerOperations(layer);
        if (operations.fail()) {
            return true;
        }
        for (const ls::OperationInfo& op : operations.value) {
            const std::string& type = op.type;
            if (type != "FillSolidOp" && type != "FillDitherOp" && type != "StrokePolylineOp" &&
                type != "StrokeRegionBoundaryOp") {
                return true;
            }
        }
        return false;
    };
    if (wholeLayerRules(upper) || wholeLayerRules(lower)) {
        return refuse("a transform or an outline on one of the layers would then act on "
                      "both; remove it first");
    }
    if (top.blend != ls::BlendMode::Normal && bottom.blend != ls::BlendMode::Normal) {
        return refuse("both layers blend in their own way, and one element cannot carry both");
    }

    doc.beginAction("Merge down");
    auto operations = engine.getLayerOperations(upper);
    for (const ls::OperationInfo& op : operations.value) {
        auto source = engine.getOperation(op.id);
        if (source.fail()) {
            doc.abandonAction();
            return refuse("an element could not be moved");
        }
        auto moved = engine.addOperation(lower, source.value);
        if (moved.fail()) {
            doc.abandonAction();
            return refuse("an element could not be moved");
        }
        // The upper layer's opacity and blend, folded into the element so it
        // looks as it did when the layer carried them.
        if (top.opacity < 1.f) {
            auto opacity = engine.getOperationParameter(moved.value, "opacity");
            const float* was = opacity.ok() ? std::get_if<float>(&opacity.value) : nullptr;
            engine.setOperationParameter(moved.value, "opacity",
                                         ls::ParameterValue{ (was ? *was : 1.f) * top.opacity });
        }
        if (top.blend != ls::BlendMode::Normal) {
            engine.setOperationParameter(moved.value, "blend",
                                         ls::ParameterValue{ static_cast<int64_t>(top.blend) });
        }
    }
    if (engine.deleteLayer(upper).fail()) {
        doc.abandonAction();
        return refuse("the layer could not be removed after merging");
    }
    doc.endAction();
    return lower;
}

std::vector<ls::GroupId> groupOrder(Document& doc, ls::SpriteId sprite) {
    auto info = doc.engine().getSpriteInfo(sprite);
    return info.ok() ? info.value.groups : std::vector<ls::GroupId>{};
}

bool readGroupProps(Document& doc, ls::GroupId group, GroupProps* out) {
    if (out == nullptr) {
        return false;
    }
    auto info = doc.engine().getGroupInfo(group);
    if (info.fail()) {
        return false;
    }
    GroupProps props;
    props.name = info.value.name;
    props.opacity = info.value.opacity;
    props.blend = info.value.blend;
    props.visible = info.value.visible;
    // The group's own list is membership, not order; the sprite's is order.
    if (!info.value.layers.empty()) {
        const ls::SpriteId sprite = spriteOf(doc, info.value.layers.front());
        for (ls::LayerId layer : layerOrder(doc, sprite)) {
            if (groupOf(doc, layer) == group) {
                props.layers.push_back(layer);
            }
        }
    }
    *out = props;
    return true;
}

ls::GroupId groupOf(Document& doc, ls::LayerId layer) {
    auto info = doc.engine().getLayerInfo(layer);
    return info.ok() ? info.value.parentId : ls::GroupId{};
}

ls::GroupId groupLayers(Document& doc, const std::vector<ls::LayerId>& layers,
                        const std::string& name) {
    if (layers.empty()) {
        return ls::GroupId{};
    }
    const ls::SpriteId sprite = spriteOf(doc, layers.front());
    if (!sprite.valid()) {
        return ls::GroupId{};
    }
    for (ls::LayerId layer : layers) {
        if (spriteOf(doc, layer) != sprite) {
            return ls::GroupId{};
        }
    }

    // Gather the members, in stack order, at the position of the topmost.
    std::vector<ls::LayerId> order = layerOrder(doc, sprite);
    const auto isMember = [&](ls::LayerId id) {
        return std::find(layers.begin(), layers.end(), id) != layers.end();
    };
    std::vector<ls::LayerId> members;
    size_t top = 0;
    for (size_t i = 0; i < order.size(); ++i) {
        if (isMember(order[i])) {
            members.push_back(order[i]);
            top = i;
        }
    }
    if (members.empty()) {
        return ls::GroupId{};
    }
    std::vector<ls::LayerId> rest;
    size_t insertAt = 0;
    for (size_t i = 0; i < order.size(); ++i) {
        if (!isMember(order[i])) {
            rest.push_back(order[i]);
            if (i < top) {
                ++insertAt;
            }
        }
    }
    rest.insert(rest.begin() + static_cast<ptrdiff_t>(insertAt), members.begin(), members.end());

    doc.beginAction("Group layers");
    if (doc.engine().setLayerOrder(sprite, rest).fail()) {
        doc.abandonAction();
        return ls::GroupId{};
    }
    ls::GroupDesc desc;
    desc.name = name;
    auto made = doc.engine().createGroup(sprite, desc);
    if (made.fail()) {
        doc.abandonAction();
        return ls::GroupId{};
    }
    std::vector<ls::GroupId> left;
    for (ls::LayerId layer : members) {
        const ls::GroupId was = groupOf(doc, layer);
        if (was.valid() && was != made.value) {
            left.push_back(was);
        }
        doc.engine().setLayerParent(layer, made.value);
    }
    for (ls::GroupId was : left) {
        dropIfEmpty(doc, was);
    }
    doc.endAction();
    return made.value;
}

bool ungroup(Document& doc, ls::GroupId group) {
    doc.beginAction("Ungroup");
    if (doc.engine().deleteGroup(group).fail()) {
        doc.abandonAction();
        return false;
    }
    doc.endAction();
    return true;
}

bool addToGroup(Document& doc, ls::LayerId layer, ls::GroupId group) {
    GroupProps props;
    if (!readGroupProps(doc, group, &props) || props.layers.empty()) {
        return false;
    }
    const ls::SpriteId sprite = spriteOf(doc, layer);
    if (sprite != spriteOf(doc, props.layers.front())) {
        return false;
    }
    if (groupOf(doc, layer) == group) {
        return true;
    }
    doc.beginAction("Add to group");
    // To the top of the run. Indices shift when the layer leaves its old
    // place, so the target is found after the erase.
    std::vector<ls::LayerId> order = layerOrder(doc, sprite);
    order.erase(std::remove(order.begin(), order.end(), layer), order.end());
    const ls::LayerId top = props.layers.back();
    size_t at = 0;
    for (size_t i = 0; i < order.size(); ++i) {
        if (order[i] == top) { at = i + 1; }
    }
    order.insert(order.begin() + static_cast<ptrdiff_t>(at), layer);
    if (doc.engine().setLayerOrder(sprite, order).fail()) {
        doc.abandonAction();
        return false;
    }
    const ls::GroupId was = groupOf(doc, layer);
    doc.engine().setLayerParent(layer, group);
    dropIfEmpty(doc, was);
    auto info = doc.engine().getLayerInfo(layer);
    if (info.ok() && info.value.hasClip) {
        doc.engine().clearLayerClip(layer);
    }
    doc.endAction();
    return true;
}

bool removeFromGroup(Document& doc, ls::LayerId layer) {
    const ls::GroupId group = groupOf(doc, layer);
    if (!group.valid()) {
        return false;
    }
    GroupProps props;
    if (!readGroupProps(doc, group, &props) || props.layers.empty()) {
        return false;
    }
    const ls::SpriteId sprite = spriteOf(doc, layer);
    doc.beginAction("Remove from group");
    // Just above the run, so it keeps drawing over what it drew over.
    std::vector<ls::LayerId> order = layerOrder(doc, sprite);
    order.erase(std::remove(order.begin(), order.end(), layer), order.end());
    size_t at = order.size();
    for (size_t i = 0; i < order.size(); ++i) {
        if (groupOf(doc, order[i]) == group) { at = i + 1; }
    }
    order.insert(order.begin() + static_cast<ptrdiff_t>(at), layer);
    if (doc.engine().setLayerOrder(sprite, order).fail()) {
        doc.abandonAction();
        return false;
    }
    doc.engine().setLayerParent(layer, ls::GroupId{});
    dropIfEmpty(doc, group);
    auto info = doc.engine().getLayerInfo(layer);
    if (info.ok() && info.value.hasClip) {
        doc.engine().clearLayerClip(layer);
    }
    doc.endAction();
    return true;
}

bool setGroupOpacity(Document& doc, ls::GroupId group, float opacity) {
    return doc.engine().setGroupOpacity(group, std::clamp(opacity, 0.f, 1.f)).ok();
}

bool setGroupBlend(Document& doc, ls::GroupId group, ls::BlendMode blend) {
    return doc.engine().setGroupBlendMode(group, blend).ok();
}

bool setGroupVisible(Document& doc, ls::GroupId group, bool visible) {
    return doc.engine().setGroupVisibility(group, visible).ok();
}

bool renameGroup(Document& doc, ls::GroupId group, const std::string& name) {
    return doc.engine().setGroupName(group, name).ok();
}

// --- clipping -------------------------------------------------------------------

bool clipToBelow(Document& doc, ls::LayerId layer, bool on) {
    if (!on) {
        return doc.engine().clearLayerClip(layer).ok();
    }
    const ls::SpriteId sprite = spriteOf(doc, layer);
    const std::vector<ls::LayerId> order = layerOrder(doc, sprite);
    const int at = indexOfLayer(doc, sprite, layer);
    if (at <= 0) {
        return false;
    }
    return doc.engine().setLayerClip(layer, order[static_cast<size_t>(at) - 1]).ok();
}

} // namespace fast
