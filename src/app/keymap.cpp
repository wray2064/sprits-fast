// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

#include "app/keymap.h"

#include <algorithm>
#include <cctype>

namespace fast {

namespace {

Chord key(const char* name, bool ctrl = false, bool shift = false, bool alt = false) {
    Chord chord;
    chord.key = name;
    chord.ctrl = ctrl;
    chord.shift = shift;
    chord.alt = alt;
    return chord;
}

Chord ctrl(const char* name, bool shift = false) { return key(name, true, shift); }
Chord shift(const char* name) { return key(name, false, true); }

std::string lower(std::string text) {
    for (char& c : text) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return text;
}

std::string trim(const std::string& text) {
    size_t a = 0;
    size_t b = text.size();
    while (a < b && std::isspace(static_cast<unsigned char>(text[a]))) { ++a; }
    while (b > a && std::isspace(static_cast<unsigned char>(text[b - 1]))) { --b; }
    return text.substr(a, b - a);
}

} // namespace

const std::vector<std::string>& keyNames() {
    static const std::vector<std::string> names = [] {
        std::vector<std::string> out;
        for (char c = 'A'; c <= 'Z'; ++c) { out.push_back(std::string(1, c)); }
        for (char c = '0'; c <= '9'; ++c) { out.push_back(std::string(1, c)); }
        for (int f = 1; f <= 12; ++f) { out.push_back("F" + std::to_string(f)); }
        for (const char* other : { "[", "]", ",", ".", "=", "-", "/", ";", "'", "`", "\\",
                                   "Enter", "Tab", "Space", "Backspace", "Delete", "Insert",
                                   "Home", "End", "PageUp", "PageDown",
                                   "Left", "Right", "Up", "Down" }) {
            out.push_back(other);
        }
        return out;
    }();
    return names;
}

std::string chordText(const Chord& chord) {
    if (!chord.valid()) {
        return std::string();
    }
    std::string out;
    if (chord.ctrl)  { out += "Ctrl+"; }
    if (chord.shift) { out += "Shift+"; }
    if (chord.alt)   { out += "Alt+"; }
    return out + chord.key;
}

bool parseChord(const std::string& text, Chord* out) {
    if (out == nullptr) {
        return false;
    }
    Chord chord;
    std::string rest = trim(text);
    if (rest.empty() || rest.size() > 32) {
        return false;
    }
    // Modifiers, each followed by '+'. The key itself may be '+'-less
    // punctuation, so the last part is taken whole.
    for (;;) {
        const size_t plus = rest.find('+');
        if (plus == std::string::npos || plus == 0 || plus + 1 >= rest.size()) {
            break;
        }
        const std::string part = lower(trim(rest.substr(0, plus)));
        if (part == "ctrl" || part == "control") { chord.ctrl = true; }
        else if (part == "shift")                { chord.shift = true; }
        else if (part == "alt")                  { chord.alt = true; }
        else { break; }
        rest = trim(rest.substr(plus + 1));
    }
    for (const std::string& name : keyNames()) {
        if (lower(name) == lower(rest)) {
            chord.key = name;
            *out = chord;
            return true;
        }
    }
    return false;
}

const std::vector<CommandInfo>& commandList() {
    static const std::vector<CommandInfo> list = {
        { "tool.pencil", "Pencil", "Tools", { key("B") } },
        { "tool.spray", "Spray", "Tools", { shift("B") } },
        { "tool.eraser", "Eraser", "Tools", { key("E") } },
        { "tool.bucket", "Fill", "Tools", { key("G") } },
        { "tool.gradient", "Gradient", "Tools", { shift("G") } },
        { "tool.picker", "Pick colour", "Tools", { key("I") } },
        { "tool.rectangle", "Rectangle", "Tools", { key("R") } },
        { "tool.ellipse", "Ellipse", "Tools", { key("U") } },
        { "tool.line", "Line", "Tools", { key("L") } },
        { "tool.contour", "Contour", "Tools", { key("D") } },
        { "tool.text", "Text", "Tools", { shift("T") } },
        { "tool.select", "Select", "Tools", { key("M") } },
        { "tool.select-ellipse", "Select ellipse", "Tools", { shift("M") } },
        { "tool.lasso", "Lasso", "Tools", { key("Q") } },
        { "tool.polygon-lasso", "Polygon lasso", "Tools", { shift("Q") } },
        { "tool.polygon", "Polygon", "Tools", { shift("D") } },
        { "tool.curve", "Curve", "Tools", { shift("L") } },
        { "tool.slice", "Slice", "Tools", { key("C") } },
        { "tool.wand", "Magic wand", "Tools", { key("W") } },
        { "tool.move", "Move", "Tools", { key("V") } },
        { "tool.hand", "Hand", "Tools", { key("H") } },
        { "tool.zoom", "Zoom", "Tools", { key("Z") } },
        { "brush.bigger", "Brush larger", "Brush", { shift("]") }, true },
        { "brush.smaller", "Brush smaller", "Brush", { shift("[") }, true },
        { "colour.swap", "Swap the two colours", "Brush", { key("X") } },
        { "file.new", "New", "File", { ctrl("N") } },
        { "file.open", "Open", "File", { ctrl("O") } },
        { "file.save", "Save", "File", { ctrl("S") } },
        { "file.save-as", "Save as", "File", { ctrl("S", true) } },
        { "file.library", "Library", "File", { ctrl("L") } },
        { "file.close", "Close tab", "File", { ctrl("W") } },
        { "file.quit", "Quit", "File", { ctrl("Q") } },
        { "edit.undo", "Undo", "Edit", { ctrl("Z") }, true },
        { "edit.redo", "Redo", "Edit", { ctrl("Z", true), ctrl("Y") }, true },
        { "edit.cut", "Cut", "Edit", { ctrl("X") } },
        { "edit.copy", "Copy", "Edit", { ctrl("C") } },
        { "edit.paste", "Paste", "Edit", { ctrl("V") } },
        { "edit.paste-layer", "Paste as new layer", "Edit", { ctrl("V", true) } },
        { "edit.brush", "Brush from selection", "Edit", { ctrl("B") } },
        { "edit.fill", "Fill the selection", "Edit", {} },
        { "edit.stroke", "Stroke the selection", "Edit", {} },
        { "selection.flip-h", "Flip horizontally", "Edit", { shift("H") } },
        { "selection.flip-v", "Flip vertically", "Edit", { shift("V") } },
        { "select.all", "Select all", "Select", { ctrl("A") } },
        { "select.none", "Deselect", "Select", { ctrl("D") } },
        { "select.invert", "Invert selection", "Select", { ctrl("I", true) } },
        { "layer.duplicate", "Duplicate layer", "Layers", { ctrl("J") } },
        { "layer.merge", "Merge down", "Layers", { ctrl("E") } },
        { "layer.group", "Group layers", "Layers", { ctrl("G") } },
        { "layer.ungroup", "Ungroup", "Layers", { ctrl("G", true) } },
        { "layer.raise", "Raise layer", "Layers", { ctrl("]") } },
        { "layer.lower", "Lower layer", "Layers", { ctrl("[") } },
        { "palette.swap", "Next palette", "Palette", { ctrl("P") } },
        { "anim.play", "Play or stop", "Animation", { key("Enter") } },
        { "anim.previous", "Previous frame", "Animation", { key(",") }, true },
        { "anim.next", "Next frame", "Animation", { key(".") }, true },
        { "anim.onion", "Onion skin", "Animation", { key("O") } },
        { "anim.duplicate", "Duplicate frame", "Animation", { ctrl("D", true) } },
        { "view.timeline", "Timeline", "View", { key("T") } },
        { "view.preview", "Preview", "View", { key("P") } },
        { "view.references", "References", "View", { shift("R") } },
        { "view.reset-layout", "Reset layout", "View", {} },
        { "view.zoom-in", "Zoom in", "View", { key("]"), ctrl("=") }, true },
        { "view.zoom-out", "Zoom out", "View", { key("["), ctrl("-") }, true },
        { "view.fit", "Fit to window", "View", { ctrl("0") } },
        { "view.snap", "Snap to grid", "View", { shift("S") } },
        { "view.next-tab", "Next tab", "View", { ctrl("Tab") } },
        { "view.previous-tab", "Previous tab", "View", { ctrl("Tab", true) } },
    };
    return list;
}

const CommandInfo* findCommand(const std::string& id) {
    for (const CommandInfo& info : commandList()) {
        if (id == info.id) {
            return &info;
        }
    }
    return nullptr;
}

const std::vector<Chord>& Keymap::chordsFor(const std::string& id) const {
    static const std::vector<Chord> none;
    auto own = overrides_.find(id);
    if (own != overrides_.end()) {
        return own->second;
    }
    const CommandInfo* info = findCommand(id);
    return info != nullptr ? info->defaults : none;
}

void Keymap::setChords(const std::string& id, const std::vector<Chord>& chords) {
    const CommandInfo* info = findCommand(id);
    if (info == nullptr) {
        return;
    }
    if (chords == info->defaults) {
        overrides_.erase(id);
    } else {
        overrides_[id] = chords;
    }
}

void Keymap::reset(const std::string& id) {
    overrides_.erase(id);
}

std::vector<std::string> Keymap::commandsUsing(const Chord& chord) const {
    std::vector<std::string> out;
    for (const CommandInfo& info : commandList()) {
        for (const Chord& own : chordsFor(info.id)) {
            if (own == chord) {
                out.push_back(info.id);
            }
        }
    }
    return out;
}

std::string Keymap::save() const {
    std::string out = "# Sprit's'fast keys: only what differs from the defaults.\n";
    for (const auto& [id, chords] : overrides_) {
        out += id + " =";
        for (size_t i = 0; i < chords.size(); ++i) {
            out += (i == 0 ? " " : " | ") + chordText(chords[i]);
        }
        out += "\n";
    }
    return out;
}

void Keymap::load(const std::string& text) {
    overrides_.clear();
    size_t start = 0;
    size_t lines = 0;
    while (start < text.size() && lines < 1024) {
        size_t end = text.find('\n', start);
        if (end == std::string::npos) {
            end = text.size();
        }
        const std::string line = trim(text.substr(start, end - start));
        start = end + 1;
        ++lines;
        if (line.empty() || line[0] == '#') {
            continue;
        }
        const size_t equals = line.find('=');
        if (equals == std::string::npos) {
            continue;
        }
        const std::string id = trim(line.substr(0, equals));
        if (findCommand(id) == nullptr) {
            continue;               // a command this build does not have
        }
        // Chords are separated by '|', which no key is called -- a comma is
        // a key, so it cannot also be the separator.
        std::vector<Chord> chords;
        const std::string rest = line.substr(equals + 1);
        size_t at = 0;
        while (at <= rest.size() && chords.size() < 4) {
            const size_t bar = rest.find('|', at);
            Chord chord;
            if (parseChord(rest.substr(at, bar == std::string::npos ? std::string::npos
                                                                    : bar - at), &chord)) {
                chords.push_back(chord);
            }
            if (bar == std::string::npos) {
                break;
            }
            at = bar + 1;
        }
        overrides_[id] = chords;   // an empty list is "no key", deliberately
    }
}

} // namespace fast
