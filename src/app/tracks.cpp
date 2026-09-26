// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

#include "app/tracks.h"
#include "app/animation.h"
#include "app/tilemap.h"

#include "app/layers.h"
#include "app/transform.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <random>
#include <set>
#include <vector>

namespace fast {

namespace {

constexpr const char* kTracksOnKey = "fast.tracks";
constexpr const char* kTrackKey = "fast.track";
constexpr const char* kTrackFromKey = "fast.track.from";
constexpr const char* kLinkKey = "fast.link";

std::string meta(Document& doc, uint64_t entity, const char* key) {
    auto value = doc.engine().getMetadata(entity, key);
    return value.ok() ? value.value : std::string();
}

void setMeta(Document& doc, uint64_t entity, const char* key, const std::string& value) {
    if (value.empty()) {
        doc.engine().clearMetadata(entity, key);
    } else {
        doc.engine().setMetadata(entity, key, value);
    }
}

std::string newKey() {
    static std::mt19937_64 random{ std::random_device{}() };
    char text[20];
    std::snprintf(text, sizeof(text), "%016llx",
                  static_cast<unsigned long long>(random()));
    return text;
}

// The frames: a tileset is a sprite of the document too, but not one.
std::vector<ls::SpriteId> framesOf(Document& doc) {
    std::vector<ls::SpriteId> frames;
    for (const Frame& frame : readFrames(doc)) {
        frames.push_back(frame.sprite);
    }
    return frames;
}

// One frame's stack as far as tracks care: which track each layer is, what
// it looks like, and which group it is in -- and nothing of what it draws.
struct TrackLayer {
    ls::LayerId id;
    std::string key;
    std::string from;
    std::string groupKey;
    LayerProps  props;
};

struct TrackGroup {
    ls::GroupId id;
    std::string key;
    GroupProps  props;
};

struct Stack {
    std::vector<TrackLayer> layers;     // bottom first
    std::vector<TrackGroup> groups;
};

Stack stackOf(Document& doc, ls::SpriteId sprite) {
    Stack stack;
    std::map<uint64_t, std::string> groupKeys;
    for (ls::GroupId group : groupOrder(doc, sprite)) {
        TrackGroup g;
        g.id = group;
        g.key = meta(doc, group.value, kTrackKey);
        readGroupProps(doc, group, &g.props);
        groupKeys[group.value] = g.key;
        stack.groups.push_back(g);
    }
    for (ls::LayerId layer : layerOrder(doc, sprite)) {
        TrackLayer l;
        l.id = layer;
        l.key = meta(doc, layer.value, kTrackKey);
        l.from = meta(doc, layer.value, kTrackFromKey);
        readLayerProps(doc, layer, &l.props);
        if (l.props.group.valid()) {
            l.groupKey = groupKeys[l.props.group.value];
        }
        stack.layers.push_back(l);
    }
    return stack;
}

std::string number(float v) {
    char text[24];
    std::snprintf(text, sizeof(text), "%.4f", static_cast<double>(v));
    return text;
}

// Everything a frame's stack must agree on, as one string to compare.
std::string signature(const Stack& stack) {
    std::string out;
    for (const TrackLayer& l : stack.layers) {
        const LayerProps& p = l.props;
        out += l.key + '\x1f' + p.name + '\x1f' + number(p.opacity) + '\x1f' +
               std::to_string(static_cast<int>(p.blend)) + '\x1f' + (p.visible ? '1' : '0') +
               (p.locked ? '1' : '0') + (p.reference ? '1' : '0') +
               (p.clipBase.valid() ? '1' : '0') + '\x1f' +
               (p.tagged ? std::to_string(p.tag.r) + "," + std::to_string(p.tag.g) + "," +
                               std::to_string(p.tag.b)
                         : std::string()) +
               '\x1f' + p.notes + '\x1f' + l.groupKey + '\x1e';
    }
    out += '\x1d';
    for (const TrackGroup& g : stack.groups) {
        out += g.key + '\x1f' + g.props.name + '\x1f' + number(g.props.opacity) + '\x1f' +
               std::to_string(static_cast<int>(g.props.blend)) + '\x1f' +
               (g.props.visible ? '1' : '0') + '\x1e';
    }
    return out;
}

// The order and grouping alone: whether the stack has to be rebuilt, not
// just have its properties copied.
std::string shape(const Stack& stack) {
    std::string out;
    for (const TrackLayer& l : stack.layers) {
        out += l.key + '\x1f' + l.groupKey + '\x1e';
    }
    return out;
}

void copyLayerProps(Document& doc, ls::LayerId to, const LayerProps& from, const LayerProps& now) {
    if (now.name != from.name) {
        renameLayer(doc, to, from.name);
    }
    if (now.opacity != from.opacity) {
        setLayerOpacity(doc, to, from.opacity);
    }
    if (now.blend != from.blend) {
        setLayerBlend(doc, to, from.blend);
    }
    if (now.visible != from.visible) {
        setLayerVisible(doc, to, from.visible);
    }
    if (now.locked != from.locked) {
        setLayerLocked(doc, to, from.locked);
    }
    if (now.reference != from.reference) {
        setReferenceLayer(doc, to, from.reference);
    }
    const bool tagDiffers = now.tagged != from.tagged ||
        (from.tagged && (now.tag.r != from.tag.r || now.tag.g != from.tag.g ||
                         now.tag.b != from.tag.b));
    if (tagDiffers) {
        setLayerTag(doc, to, from.tagged ? &from.tag : nullptr);
    }
    if (now.notes != from.notes) {
        setLayerNotes(doc, to, from.notes);
    }
    if (now.clipBase.valid() != from.clipBase.valid()) {
        clipToBelow(doc, to, from.clipBase.valid());
    }
}

// Gives every layer and group of `master` a key of its own: one it lacks, or
// one it shares with another -- a clone carries its original's. A layer
// marked as a copy remembers which track it copies, for this sync only.
std::map<std::string, std::string> keyMaster(Document& doc, ls::SpriteId master) {
    std::map<std::string, std::string> copies;      // new key -> track copied
    std::set<std::string> seen;
    for (ls::LayerId layer : layerOrder(doc, master)) {
        std::string key = meta(doc, layer.value, kTrackKey);
        const std::string from = meta(doc, layer.value, kTrackFromKey);
        if (key.empty() || seen.count(key) != 0) {
            key = newKey();
            setMeta(doc, layer.value, kTrackKey, key);
            if (!from.empty()) {
                copies[key] = from;
            }
        }
        if (!from.empty()) {
            setMeta(doc, layer.value, kTrackFromKey, std::string());
        }
        seen.insert(key);
    }
    seen.clear();
    for (ls::GroupId group : groupOrder(doc, master)) {
        std::string key = meta(doc, group.value, kTrackKey);
        if (key.empty() || seen.count(key) != 0) {
            key = newKey();
            setMeta(doc, group.value, kTrackKey, key);
        }
        seen.insert(key);
    }
    return copies;
}

// Brings one frame into line with the master's stack.
bool syncFrame(Document& doc, ls::SpriteId frame, const Stack& master,
               const std::map<std::string, std::string>& copies) {
    ls::LSContext& engine = doc.engine();
    Stack here = stackOf(doc, frame);
    std::set<std::string> masterKeys;
    for (const TrackLayer& m : master.layers) {
        masterKeys.insert(m.key);
    }

    // Which of this frame's layers is which track. Every layer has a key by
    // now (see claimTracks); one whose track the master no longer has was
    // deleted there, and goes.
    std::map<std::string, ls::LayerId> have;
    bool changed = false;
    for (const TrackLayer& l : here.layers) {
        if (masterKeys.count(l.key) != 0 && have.count(l.key) == 0) {
            have[l.key] = l.id;
        } else {
            engine.deleteLayer(l.id);
            changed = true;
        }
    }

    // Tracks this frame lacks: a copy of its own cel of the track copied, or
    // an empty layer.
    for (const TrackLayer& m : master.layers) {
        if (have.count(m.key) != 0) {
            continue;
        }
        ls::LayerId made;
        const auto copied = copies.find(m.key);
        if (copied != copies.end()) {
            const ls::LayerId source = layerOfTrack(doc, frame, copied->second);
            if (source.valid()) {
                auto clone = engine.cloneLayer(source, frame, -1);
                if (clone.ok()) {
                    made = clone.value;
                }
            }
        }
        if (!made.valid()) {
            ls::LayerDesc desc;
            desc.name = m.props.name;
            auto created = engine.createLayer(frame, desc);
            if (created.fail()) {
                return changed;
            }
            made = created.value;
            // A tilemap track's new cel is a tilemap too, of empty cells.
            TilemapLayer like;
            if (readTilemapLayer(doc, m.id, &like)) {
                makeTilemapLike(doc, made, like);
            }
        }
        setMeta(doc, made.value, kTrackKey, m.key);
        setMeta(doc, made.value, kTrackFromKey, std::string());
        have[m.key] = made;
        changed = true;
    }

    // Order and groups: rebuilt only when they differ. Groups are dissolved,
    // the layers put in the master's order, and the groups made again round
    // the same runs.
    here = stackOf(doc, frame);
    if (shape(here) != shape(master)) {
        for (const TrackGroup& g : here.groups) {
            ungroup(doc, g.id);
        }
        for (size_t i = 0; i < master.layers.size(); ++i) {
            moveLayer(doc, have[master.layers[i].key], static_cast<int>(i));
        }
        for (const TrackGroup& g : master.groups) {
            std::vector<ls::LayerId> members;
            for (const TrackLayer& m : master.layers) {
                if (m.groupKey == g.key) {
                    members.push_back(have[m.key]);
                }
            }
            if (members.empty()) {
                continue;
            }
            const ls::GroupId made = groupLayers(doc, members, g.props.name);
            if (made.valid()) {
                setMeta(doc, made.value, kTrackKey, g.key);
            }
        }
        changed = true;
    }

    // Properties, layer by layer and group by group.
    here = stackOf(doc, frame);
    std::map<std::string, const TrackLayer*> now;
    for (const TrackLayer& l : here.layers) {
        now[l.key] = &l;
    }
    for (const TrackLayer& m : master.layers) {
        const auto it = now.find(m.key);
        if (it != now.end()) {
            copyLayerProps(doc, it->second->id, m.props, it->second->props);
        }
    }
    std::map<std::string, const TrackGroup*> groups;
    for (const TrackGroup& g : here.groups) {
        groups[g.key] = &g;
    }
    for (const TrackGroup& g : master.groups) {
        const auto it = groups.find(g.key);
        if (it == groups.end()) {
            continue;
        }
        const GroupProps& p = it->second->props;
        if (p.name != g.props.name) { renameGroup(doc, it->second->id, g.props.name); }
        if (p.opacity != g.props.opacity) { setGroupOpacity(doc, it->second->id, g.props.opacity); }
        if (p.blend != g.props.blend) { setGroupBlend(doc, it->second->id, g.props.blend); }
        if (p.visible != g.props.visible) { setGroupVisible(doc, it->second->id, g.props.visible); }
    }
    return true;
}

// Every layer of every other frame given a track before anything is synced:
// a layer with no key -- from an older file, a frame made some other way --
// or one claiming a track twice joins a track of the master's by name, or
// becomes a track of its own, added to the master empty so that it reaches
// every frame. Nothing any frame holds is lost to a sync.
void claimTracks(Document& doc, ls::SpriteId master, const std::vector<ls::SpriteId>& frames) {
    for (ls::SpriteId frame : frames) {
        if (frame == master) {
            continue;
        }
        std::map<std::string, std::string> masterByName;     // name -> key
        std::set<std::string> masterKeys;
        for (ls::LayerId layer : layerOrder(doc, master)) {
            LayerProps props;
            readLayerProps(doc, layer, &props);
            const std::string key = meta(doc, layer.value, kTrackKey);
            masterKeys.insert(key);
            masterByName.emplace(props.name, key);
        }
        std::set<std::string> have;
        for (ls::LayerId layer : layerOrder(doc, frame)) {
            const std::string key = meta(doc, layer.value, kTrackKey);
            if (!key.empty() && have.count(key) == 0) {
                have.insert(key);
                continue;
            }
            LayerProps props;
            readLayerProps(doc, layer, &props);
            const auto named = masterByName.find(props.name);
            if (named != masterByName.end() && have.count(named->second) == 0) {
                setMeta(doc, layer.value, kTrackKey, named->second);
                have.insert(named->second);
                continue;
            }
            const std::string made = newKey();
            setMeta(doc, layer.value, kTrackKey, made);
            have.insert(made);
            ls::LayerDesc desc;
            desc.name = props.name;
            auto added = doc.engine().createLayer(master, desc);
            if (added.ok()) {
                setMeta(doc, added.value.value, kTrackKey, made);
            }
        }
    }
}

// --- linked cels ---------------------------------------------------------------

bool isLayerOrSprite(const std::string& name) {
    return name == "targetLayer" || name == "targetSprite";
}

std::string valueText(const ls::ParameterValue& value) {
    char text[64];
    if (const bool* b = std::get_if<bool>(&value)) { return *b ? "1" : "0"; }
    if (const int64_t* i = std::get_if<int64_t>(&value)) { return std::to_string(*i); }
    if (const float* f = std::get_if<float>(&value)) { return number(*f); }
    if (const ls::Vec2f* v = std::get_if<ls::Vec2f>(&value)) {
        return number(v->x) + "," + number(v->y);
    }
    if (const ls::Color* c = std::get_if<ls::Color>(&value)) {
        std::snprintf(text, sizeof(text), "%u,%u,%u,%u", c->r, c->g, c->b, c->a);
        return text;
    }
    if (const ls::Mat3f* m = std::get_if<ls::Mat3f>(&value)) {
        std::string out;
        for (float e : m->m) { out += number(e) + ","; }
        return out;
    }
    if (const std::string* s = std::get_if<std::string>(&value)) { return *s; }
    if (const uint64_t* h = std::get_if<uint64_t>(&value)) { return std::to_string(*h); }
    return std::string();
}

// A layer's operations as text: type and every parameter but those naming
// the layer or sprite itself -- the parts a linked cel must share.
std::vector<std::string> operationsText(Document& doc, ls::LayerId layer) {
    std::vector<std::string> out;
    auto operations = doc.engine().getLayerOperations(layer);
    if (operations.fail()) {
        return out;
    }
    for (const ls::OperationInfo& op : operations.value) {
        std::string text = op.type;
        auto params = doc.engine().describeOperation(op.id);
        if (params.ok()) {
            for (const ls::ParameterInfo& param : params.value) {
                if (isLayerOrSprite(param.name) || param.type == ls::ParameterType::Unsupported) {
                    continue;
                }
                auto value = doc.engine().getOperationParameter(op.id, param.name);
                text += '\x1f' + param.name + '=' + (value.ok() ? valueText(value.value) : "?");
            }
        }
        // A point list the parameters cannot reach is still part of the op.
        text += '\x1f' + op.summary;
        out.push_back(text);
    }
    return out;
}

// Makes `to` draw what `from` draws, through operations of its own naming the
// same regions and shapes. Anything naming a layer or a sprite is pointed at
// `to`'s own.
void shareOperations(Document& doc, ls::LayerId from, ls::LayerId to) {
    ls::LSContext& engine = doc.engine();
    auto existing = engine.getLayerOperations(to);
    if (existing.ok()) {
        for (const ls::OperationInfo& op : existing.value) {
            engine.removeOperation(to, op.id);
        }
    }
    auto source = engine.getLayerOperations(from);
    auto fromInfo = engine.getLayerInfo(from);
    auto toInfo = engine.getLayerInfo(to);
    if (source.fail() || fromInfo.fail() || toInfo.fail()) {
        return;
    }
    for (const ls::OperationInfo& op : source.value) {
        auto operation = engine.getOperation(op.id);
        if (operation.fail()) {
            continue;
        }
        auto added = engine.addOperation(to, operation.value);
        if (added.fail()) {
            continue;
        }
        auto params = engine.describeOperation(added.value);
        if (params.fail()) {
            continue;
        }
        for (const ls::ParameterInfo& param : params.value) {
            if (!isLayerOrSprite(param.name)) {
                continue;
            }
            auto value = engine.getOperationParameter(added.value, param.name);
            const uint64_t* handle = value.ok() ? std::get_if<uint64_t>(&value.value) : nullptr;
            if (handle == nullptr) {
                continue;
            }
            if (param.name == "targetLayer" && *handle == from.value) {
                engine.setOperationParameter(added.value, param.name,
                                             ls::ParameterValue{ static_cast<uint64_t>(to.value) });
            } else if (param.name == "targetSprite" && *handle == fromInfo.value.sprite.value) {
                engine.setOperationParameter(
                    added.value, param.name,
                    ls::ParameterValue{ static_cast<uint64_t>(toInfo.value.sprite.value) });
            }
        }
    }
}

} // namespace

std::string linkOf(Document& doc, ls::LayerId layer) {
    return meta(doc, layer.value, kLinkKey);
}

int linkCels(Document& doc, ls::LayerId layer, const std::vector<ls::SpriteId>& frames) {
    const std::string key = meta(doc, layer.value, kTrackKey);
    auto info = doc.engine().getLayerInfo(layer);
    if (key.empty() || info.fail()) {
        return 0;
    }
    std::string link = meta(doc, layer.value, kLinkKey);
    int linked = 0;
    for (ls::SpriteId frame : frames) {
        if (frame == info.value.sprite) {
            continue;
        }
        const ls::LayerId other = layerOfTrack(doc, frame, key);
        if (!other.valid()) {
            continue;
        }
        if (link.empty()) {
            link = newKey();
            setMeta(doc, layer.value, kLinkKey, link);
        }
        setMeta(doc, other.value, kLinkKey, link);
        shareOperations(doc, layer, other);
        ++linked;
    }
    return linked;
}

ls::LayerId unlinkCel(Document& doc, ls::LayerId layer) {
    ls::LSContext& engine = doc.engine();
    const std::string link = meta(doc, layer.value, kLinkKey);
    auto info = engine.getLayerInfo(layer);
    if (link.empty() || info.fail()) {
        return layer;
    }
    const ls::SpriteId sprite = info.value.sprite;
    const int at = indexOfLayer(doc, sprite, layer);
    // A copy is a deep one: its regions and shapes its own.
    auto copy = engine.cloneLayer(layer, sprite, at);
    if (copy.fail()) {
        return layer;
    }
    setMeta(doc, copy.value.value, kLinkKey, std::string());
    setMeta(doc, copy.value.value, kTrackKey, meta(doc, layer.value, kTrackKey));
    // The shared regions are still the rest of the link's; deleting this
    // layer's operations leaves them be.
    engine.deleteLayer(layer);
    // A link of one is no link.
    std::vector<ls::LayerId> rest;
    for (ls::SpriteId frame : framesOf(doc)) {
        for (ls::LayerId other : layerOrder(doc, frame)) {
            if (meta(doc, other.value, kLinkKey) == link) {
                rest.push_back(other);
            }
        }
    }
    if (rest.size() == 1) {
        setMeta(doc, rest.front().value, kLinkKey, std::string());
    }
    return copy.value;
}

bool syncLinks(Document& doc, ls::SpriteId master) {
    const std::vector<ls::SpriteId> frames = framesOf(doc);
    if (std::find(frames.begin(), frames.end(), master) == frames.end()) {
        return false;
    }
    bool changed = false;
    for (ls::LayerId layer : layerOrder(doc, master)) {
        const std::string link = meta(doc, layer.value, kLinkKey);
        if (link.empty()) {
            continue;
        }
        const std::vector<std::string> wanted = operationsText(doc, layer);
        for (ls::SpriteId frame : frames) {
            if (frame == master) {
                continue;
            }
            for (ls::LayerId other : layerOrder(doc, frame)) {
                if (meta(doc, other.value, kLinkKey) != link) {
                    continue;
                }
                if (operationsText(doc, other) != wanted) {
                    shareOperations(doc, layer, other);
                    changed = true;
                }
            }
        }
    }
    return changed;
}

bool tweenTransforms(Document& doc, const std::vector<ls::SpriteId>& frames,
                     const std::string& key, TweenEasing easing, std::string* why) {
    const auto fail = [&](const char* reason) {
        if (why != nullptr) { *why = reason; }
        return false;
    };
    if (frames.size() < 3) {
        return fail("a tween needs a frame between its two keys");
    }
    const ls::LayerId first = layerOfTrack(doc, frames.front(), key);
    const ls::LayerId last = layerOfTrack(doc, frames.back(), key);
    if (!first.valid() || !last.valid()) {
        return fail("the layer is not in both key frames");
    }
    const std::vector<TransformEntry> a = listTransforms(doc, first);
    const std::vector<TransformEntry> b = listTransforms(doc, last);
    if (a.empty()) {
        return fail("the first key frame's layer has no transforms to tween");
    }
    if (a.size() != b.size()) {
        return fail("the two key frames' layers have different transforms");
    }
    for (size_t i = 0; i < a.size(); ++i) {
        if (a[i].kind != b[i].kind) {
            return fail("the two key frames' layers have different transforms");
        }
    }
    const auto mix = [](float x, float y, float t) { return x + (y - x) * t; };
    const auto mix2 = [&](ls::Vec2f x, ls::Vec2f y, float t) {
        return ls::Vec2f{ mix(x.x, y.x, t), mix(x.y, y.y, t) };
    };
    for (size_t f = 1; f + 1 < frames.size(); ++f) {
        const ls::LayerId layer = layerOfTrack(doc, frames[f], key);
        if (!layer.valid()) {
            continue;
        }
        float t = static_cast<float>(f) / static_cast<float>(frames.size() - 1);
        if (easing == TweenEasing::EaseInOut) {
            t = t * t * (3.f - 2.f * t);
        }
        // The keys' transforms, if this frame's are not the same kinds.
        std::vector<TransformEntry> here = listTransforms(doc, layer);
        bool same = here.size() == a.size();
        for (size_t i = 0; same && i < a.size(); ++i) {
            same = here[i].kind == a[i].kind;
        }
        if (!same) {
            clearTransforms(doc, layer);
            for (const TransformEntry& k : a) {
                switch (k.kind) {
                    case TransformKind::Rotate: addRotate(doc, layer, k.angleDegrees, k.pivot, k.sampling); break;
                    case TransformKind::Scale:  addScale(doc, layer, k.factor, k.pivot, k.sampling); break;
                    case TransformKind::Mirror: addMirror(doc, layer, k.axis, k.pivot); break;
                    case TransformKind::Offset: addOffset(doc, layer, k.delta); break;
                }
            }
            here = listTransforms(doc, layer);
        }
        for (size_t i = 0; i < a.size() && i < here.size(); ++i) {
            const TransformEntry& x = a[i];
            const TransformEntry& y = b[i];
            switch (x.kind) {
                case TransformKind::Rotate:
                    setRotateAngle(doc, here[i].id, mix(x.angleDegrees, y.angleDegrees, t));
                    break;
                case TransformKind::Scale:
                    setScaleFactor(doc, here[i].id, mix2(x.factor, y.factor, t));
                    break;
                case TransformKind::Offset:
                    setOffsetDelta(doc, here[i].id,
                                   { std::round(mix(x.delta.x, y.delta.x, t)),
                                     std::round(mix(x.delta.y, y.delta.y, t)) });
                    break;
                case TransformKind::Mirror:
                    break;
            }
            if (x.kind != TransformKind::Offset) {
                setTransformPivot(doc, here[i].id, mix2(x.pivot, y.pivot, t));
            }
            if (x.kind == TransformKind::Rotate || x.kind == TransformKind::Scale) {
                setTransformSampling(doc, here[i].id, x.sampling);
            }
        }
    }
    return true;
}

bool tracksOn(Document& doc) {
    return meta(doc, doc.id().value, kTracksOnKey) == "1";
}

void setTracksOn(Document& doc, bool on) {
    setMeta(doc, doc.id().value, kTracksOnKey, on ? "1" : "");
}

std::string trackKey(Document& doc, ls::LayerId layer) {
    return meta(doc, layer.value, kTrackKey);
}

void markTrackCopy(Document& doc, ls::LayerId copy, ls::LayerId source) {
    std::string from = meta(doc, source.value, kTrackKey);
    if (from.empty()) {
        // A source not yet a track becomes one, so the copy can name it.
        from = newKey();
        setMeta(doc, source.value, kTrackKey, from);
    }
    setMeta(doc, copy.value, kTrackKey, std::string());
    setMeta(doc, copy.value, kTrackFromKey, from);
}

void markNewTrack(Document& doc, ls::LayerId layer) {
    setMeta(doc, layer.value, kTrackKey, std::string());
    setMeta(doc, layer.value, kTrackFromKey, std::string());
}

ls::LayerId layerOfTrack(Document& doc, ls::SpriteId frame, const std::string& key) {
    if (key.empty()) {
        return ls::LayerId{};
    }
    for (ls::LayerId layer : layerOrder(doc, frame)) {
        if (meta(doc, layer.value, kTrackKey) == key) {
            return layer;
        }
    }
    return ls::LayerId{};
}

bool syncTracks(Document& doc, ls::SpriteId master) {
    if (!tracksOn(doc) || !master.valid()) {
        return false;
    }
    const std::vector<ls::SpriteId> frames = framesOf(doc);
    // An action that removed the frame being edited leaves nothing to follow:
    // syncing to a frame that is gone would empty every other.
    if (std::find(frames.begin(), frames.end(), master) == frames.end()) {
        return false;
    }
    if (frames.size() < 2) {
        // One frame is in line with itself -- but its layers still want keys,
        // for the frame that comes next.
        keyMaster(doc, master);
        return false;
    }
    const std::map<std::string, std::string> copies = keyMaster(doc, master);
    claimTracks(doc, master, frames);
    const Stack stack = stackOf(doc, master);
    const std::string wanted = signature(stack);
    bool changed = false;
    for (ls::SpriteId frame : frames) {
        if (frame == master) {
            continue;
        }
        if (copies.empty() && signature(stackOf(doc, frame)) == wanted) {
            continue;
        }
        changed = syncFrame(doc, frame, stack, copies) || changed;
    }
    return changed;
}

void adoptTracks(Document& doc, ls::SpriteId master) {
    // Keys by name, the same name the same track in every frame; within a
    // frame a second layer of a name is a track of its own.
    std::map<std::string, std::string> byName;
    const std::vector<ls::SpriteId> frames = framesOf(doc);
    for (ls::SpriteId frame : frames) {
        std::set<std::string> used;
        for (ls::LayerId layer : layerOrder(doc, frame)) {
            LayerProps props;
            readLayerProps(doc, layer, &props);
            std::string key = meta(doc, layer.value, kTrackKey);
            if (key.empty() || used.count(key) != 0) {
                auto known = byName.find(props.name);
                key = (known != byName.end() && used.count(known->second) == 0)
                          ? known->second : newKey();
                byName.emplace(props.name, key);
                setMeta(doc, layer.value, kTrackKey, key);
            }
            used.insert(key);
        }
    }
    // The master holds every track, so the sync that follows puts every one
    // in every frame and deletes nothing.
    std::set<std::string> inMaster;
    for (ls::LayerId layer : layerOrder(doc, master)) {
        inMaster.insert(meta(doc, layer.value, kTrackKey));
    }
    for (ls::SpriteId frame : frames) {
        if (frame == master) {
            continue;
        }
        for (ls::LayerId layer : layerOrder(doc, frame)) {
            const std::string key = meta(doc, layer.value, kTrackKey);
            if (inMaster.count(key) != 0) {
                continue;
            }
            LayerProps props;
            readLayerProps(doc, layer, &props);
            ls::LayerDesc desc;
            desc.name = props.name;
            auto made = doc.engine().createLayer(master, desc);
            if (made.ok()) {
                setMeta(doc, made.value.value, kTrackKey, key);
                inMaster.insert(key);
            }
        }
    }
    setTracksOn(doc, true);
    syncTracks(doc, master);
}

} // namespace fast
