// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

#include "ui/ui_script.h"

#include "app/animation.h"
#include "app/element.h"
#include "app/export_png.h"
#include "app/file_io.h"
#include "app/guides.h"
#include "app/layers.h"
#include "app/slices.h"
#include "app/theme_store.h"
#include "app/tracks.h"
#include "app/transform.h"
#include "ui/canvas_view.h"
#include "ui/layout.h"
#include "ui/reference_section.h"
#include "ui/appearance.h"
#include "ui/start_screen.h"
#include "ui/tabs.h"
#include "ui/theme.h"
#include "ui/user_guide.h"

#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <sstream>

// ------------------------------------------------------ the test engine hooks --
//
// ImGui calls these for every item it adds while TestEngineHookItems is set:
// its box, and for most, its label. A script clicks a widget by finding its
// label among what the last frame drew.

namespace {

struct DrawnItem {
    ImGuiID     id = 0;
    ImRect      box;
    std::string label;
    bool        shown = true;    // its middle inside its window's clip: not scrolled away
};
std::vector<DrawnItem> gDrawing;     // this frame's, as they are added
std::vector<DrawnItem> gDrawn;       // the last whole frame's
// A label told before its item is added -- a tab's is -- kept until it is.
std::vector<std::pair<ImGuiID, std::string>> gEarlyLabels;

} // namespace

void ImGuiTestEngineHook_ItemAdd(ImGuiContext* ctx, ImGuiID id, const ImRect& bb,
                                 const ImGuiLastItemData*) {
    const bool shown = ctx == nullptr || ctx->CurrentWindow == nullptr ||
                       ctx->CurrentWindow->ClipRect.Contains(bb.GetCenter());
    std::string label;
    for (size_t i = gEarlyLabels.size(); i-- > 0;) {
        if (gEarlyLabels[i].first == id) {
            label = gEarlyLabels[i].second;
            gEarlyLabels.erase(gEarlyLabels.begin() + static_cast<std::ptrdiff_t>(i));
            break;
        }
    }
    gDrawing.push_back({ id, bb, label, shown });
}

void ImGuiTestEngineHook_ItemInfo(ImGuiContext*, ImGuiID id, const char* label,
                                  ImGuiItemStatusFlags) {
    for (size_t i = gDrawing.size(); i-- > 0;) {
        if (gDrawing[i].id == id) {
            gDrawing[i].label = label != nullptr ? label : "";
            return;
        }
    }
    // Not added yet: a tab tells its label first.
    if (label != nullptr && gEarlyLabels.size() < 256) {
        gEarlyLabels.push_back({ id, label });
    }
}

void ImGuiTestEngineHook_Log(ImGuiContext*, const char*, ...) {}

void fast::nameForScripts(const char* label) {
    ImGuiTestEngineHook_ItemInfo(ImGui::GetCurrentContext(), ImGui::GetItemID(), label, 0);
}

const char* ImGuiTestEngine_FindItemDebugLabel(ImGuiContext*, ImGuiID) { return nullptr; }

namespace fast {

namespace {

// The native dialogs a script can answer or expect, by the names it uses.
const struct { const char* name; DialogResult::Kind kind; } kDialogNames[] = {
    { "open",              DialogResult::Kind::Open },
    { "save-as",           DialogResult::Kind::SaveAs },
    { "save-copy",         DialogResult::Kind::SaveCopy },
    { "export-png",        DialogResult::Kind::ExportPng },
    { "export-sheet",      DialogResult::Kind::ExportSheet },
    { "export-animation",  DialogResult::Kind::ExportAnimation },
    { "import-palette",    DialogResult::Kind::ImportPalette },
    { "export-palette",    DialogResult::Kind::ExportPalette },
    { "import-reference",  DialogResult::Kind::ImportReference },
    { "import-sheet",      DialogResult::Kind::ImportSheet },
    { "import-theme",      DialogResult::Kind::ImportTheme },
    { "export-theme",      DialogResult::Kind::ExportTheme },
    { "project-folder",    DialogResult::Kind::ProjectFolder },
    { "reference-folder",  DialogResult::Kind::ReferenceFolder },
};

DialogResult::Kind dialogFromName(const std::string& name) {
    for (const auto& entry : kDialogNames) {
        if (name == entry.name) {
            return entry.kind;
        }
    }
    return DialogResult::Kind::None;
}

std::string dialogName(DialogResult::Kind kind) {
    for (const auto& entry : kDialogNames) {
        if (entry.kind == kind) {
            return entry.name;
        }
    }
    return "none";
}

// The label as shown: what comes before any ## that only makes the ID unique.
std::string shownLabel(const std::string& label) {
    const size_t hashes = label.find("##");
    return hashes == std::string::npos ? label : label.substr(0, hashes);
}

// Where the last frame drew a widget with this label: the `index`th of them
// in drawing order, or with 0 the last drawn, which is the topmost.
bool findDrawn(const std::string& label, int index, ImVec2* centre) {
    int seen = 0;
    const DrawnItem* found = nullptr;
    for (const DrawnItem& item : gDrawn) {
        if (item.shown && !item.label.empty() &&
            (shownLabel(item.label) == label || item.label == label)) {
            found = &item;
            if (++seen == index) {
                break;
            }
        }
    }
    if (found == nullptr || (index > 0 && seen != index)) {
        return false;
    }
    *centre = found->box.GetCenter();
    return true;
}

// "Layer 1 @2": the label, and which of several.
void splitIndex(std::string* label, int* index) {
    *index = 0;
    while (!label->empty() && label->back() == ' ') { label->pop_back(); }
    const size_t at = label->rfind(" @");
    if (at != std::string::npos && at + 2 < label->size()) {
        const std::string n = label->substr(at + 2);
        if (n.find_first_not_of("0123456789") == std::string::npos) {
            *index = std::atoi(n.c_str());
            label->erase(at);
        }
    }
}

std::vector<std::string> words(const std::string& line) {
    std::vector<std::string> out;
    std::istringstream in(line);
    std::string word;
    while (in >> word) {
        out.push_back(word);
    }
    return out;
}

bool number(const std::string& text, float* out) {
    char* end = nullptr;
    const float value = std::strtof(text.c_str(), &end);
    if (end == text.c_str() || *end != '\0') {
        return false;
    }
    *out = value;
    return true;
}

bool hexColour(const std::string& text, ls::Color* out) {
    if (text.size() != 6 && text.size() != 8) {
        return false;
    }
    char* end = nullptr;
    const unsigned long value = std::strtoul(text.c_str(), &end, 16);
    if (*end != '\0') {
        return false;
    }
    if (text.size() == 6) {
        *out = { static_cast<uint8_t>(value >> 16), static_cast<uint8_t>(value >> 8),
                 static_cast<uint8_t>(value), 255 };
    } else {
        *out = { static_cast<uint8_t>(value >> 24), static_cast<uint8_t>(value >> 16),
                 static_cast<uint8_t>(value >> 8), static_cast<uint8_t>(value) };
    }
    return true;
}

ImGuiKey keyNamed(const std::string& name) {
    if (name.size() == 1) {
        const char c = name[0];
        if (c >= 'a' && c <= 'z') { return static_cast<ImGuiKey>(ImGuiKey_A + (c - 'a')); }
        if (c >= '0' && c <= '9') { return static_cast<ImGuiKey>(ImGuiKey_0 + (c - '0')); }
        switch (c) {
            case '[': return ImGuiKey_LeftBracket;
            case ']': return ImGuiKey_RightBracket;
            case ',': return ImGuiKey_Comma;
            case '.': return ImGuiKey_Period;
            case '=': return ImGuiKey_Equal;
            case '-': return ImGuiKey_Minus;
            default: break;
        }
    }
    // f1 to f12.
    if (name.size() >= 2 && name.size() <= 3 && name[0] == 'f' &&
        name.find_first_not_of("0123456789", 1) == std::string::npos) {
        const int number = std::atoi(name.c_str() + 1);
        if (number >= 1 && number <= 12) {
            return static_cast<ImGuiKey>(ImGuiKey_F1 + (number - 1));
        }
    }
    const struct { const char* name; ImGuiKey key; } named[] = {
        { "escape", ImGuiKey_Escape }, { "enter", ImGuiKey_Enter },
        { "delete", ImGuiKey_Delete }, { "backspace", ImGuiKey_Backspace },
        { "tab", ImGuiKey_Tab }, { "space", ImGuiKey_Space },
        { "left", ImGuiKey_LeftArrow }, { "right", ImGuiKey_RightArrow },
        { "up", ImGuiKey_UpArrow }, { "down", ImGuiKey_DownArrow },
        { "shift", ImGuiMod_Shift }, { "ctrl", ImGuiMod_Ctrl }, { "alt", ImGuiMod_Alt },
    };
    for (const auto& entry : named) {
        if (name == entry.name) {
            return entry.key;
        }
    }
    return ImGuiKey_None;
}

// The sample roles `demo-roles` registers: the shapes a program's own take --
// an indicator, text, and a translucent tint -- so the editor, the file and the
// readability check can be run against them without a program that has them.
std::vector<ExtraRole> demoRoles() {
    ExtraRole mark;
    mark.id = "demo.mark";
    mark.label = "Sample mark";
    mark.group = "Sample program";
    mark.dark = Rgba{ 255, 130, 200, 255 };
    mark.light = Rgba{ 170, 0, 110, 255 };
    mark.standard = 3.0;
    mark.enhanced = 4.5;
    ExtraRole label;
    label.id = "demo.label";
    label.label = "Sample label";
    label.group = "Sample program";
    label.dark = Rgba{ 190, 220, 255, 255 };
    label.light = Rgba{ 20, 60, 130, 255 };
    label.standard = 4.5;
    label.enhanced = 7.0;
    ExtraRole tint;
    tint.id = "demo.tint";
    tint.label = "Sample tint";
    tint.group = "Sample program";
    tint.dark = Rgba{ 120, 200, 255, 90 };
    tint.light = Rgba{ 0, 80, 200, 70 };
    tint.alpha = true;
    return { mark, label, tint };
}

// --- loops ----------------------------------------------------------------------
//
// `for-each theme` ... `end-for` repeats the lines between, once for every
// built-in theme (or, `for-each scale 100 150`, once for each value listed), with
// {theme} or {scale} standing for it in any line. The loops are unrolled before a
// line is parsed, so a script is still a flat list of steps, and every step
// keeps the number of the line it was written on.

struct SourceLine {
    int         number = 0;
    std::string text;
};
using Bindings = std::vector<std::pair<std::string, std::string>>;

constexpr size_t kMaxExpandedLines = 40000;
constexpr size_t kMaxLoopValues = 64;
constexpr int kMaxLoopDepth = 3;

std::string substituted(std::string text, const Bindings& bindings) {
    for (const auto& binding : bindings) {
        const std::string key = "{" + binding.first + "}";
        for (size_t at = text.find(key); at != std::string::npos;
             at = text.find(key, at + binding.second.size())) {
            text.replace(at, key.size(), binding.second);
        }
    }
    return text;
}

bool expandLoops(const std::vector<SourceLine>& body, const Bindings& bindings, int depth,
                 std::vector<SourceLine>* out, const std::string& path, std::string* error) {
    const auto fail = [&](int number, const char* why) {
        if (error != nullptr) {
            *error = path + ":" + std::to_string(number) + ": " + why;
        }
        return false;
    };
    for (size_t i = 0; i < body.size(); ++i) {
        const std::string text = substituted(body[i].text, bindings);
        const std::vector<std::string> w = words(text);
        if (!w.empty() && w[0] == "end-for") {
            return fail(body[i].number, "end-for with no for-each");
        }
        if (w.empty() || w[0] != "for-each") {
            if (out->size() >= kMaxExpandedLines) {
                return fail(body[i].number, "the loops make too many lines");
            }
            out->push_back({ body[i].number, text });
            continue;
        }
        if (w.size() < 2) {
            return fail(body[i].number, "for-each needs a name: theme, scale, or your own");
        }
        if (depth >= kMaxLoopDepth) {
            return fail(body[i].number, "loops nested too deep");
        }
        // The end-for that matches, past any loop inside.
        size_t end = i + 1;
        for (int open = 1; end < body.size(); ++end) {
            const std::vector<std::string> inner = words(body[end].text);
            if (!inner.empty() && inner[0] == "for-each") {
                ++open;
            } else if (!inner.empty() && inner[0] == "end-for" && --open == 0) {
                break;
            }
        }
        if (end >= body.size()) {
            return fail(body[i].number, "for-each with no end-for");
        }
        std::vector<std::string> values(w.begin() + 2, w.end());
        if (values.empty() && w[1] == "theme") {
            for (const Theme& theme : builtinThemes()) {
                values.push_back(theme.id);
            }
        }
        if (values.empty() || values.size() > kMaxLoopValues) {
            return fail(body[i].number, "for-each needs between 1 and 64 values");
        }
        const std::vector<SourceLine> inside(body.begin() + static_cast<std::ptrdiff_t>(i) + 1,
                                             body.begin() + static_cast<std::ptrdiff_t>(end));
        for (const std::string& value : values) {
            Bindings next = bindings;
            next.emplace_back(w[1], value);
            if (!expandLoops(inside, next, depth + 1, out, path, error)) {
                return false;
            }
        }
        i = end;
    }
    return true;
}

} // namespace

bool UiScript::load(const std::string& path, std::string* error) {
    std::vector<uint8_t> bytes;
    if (!readFile(path, bytes, error)) {
        return false;
    }
    path_ = path;
    std::istringstream in(std::string(bytes.begin(), bytes.end()));
    std::string line;
    int number = 0;
    std::vector<SourceLine> source;
    while (std::getline(in, line)) {
        ++number;
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        // A file saved as UTF-8 by some editors starts with a byte-order mark.
        if (number == 1 && line.compare(0, 3, "\xEF\xBB\xBF") == 0) {
            line.erase(0, 3);
        }
        // A comment is a # starting the line or followed by a space -- so a
        // widget called ##slot1 is a label, not a comment.
        for (size_t hash = line.find('#'); hash != std::string::npos;
             hash = line.find('#', hash + 1)) {
            const bool starts = line.find_first_not_of(" \t") == hash;
            const bool spaced = hash + 1 >= line.size() || line[hash + 1] == ' ';
            if (starts || spaced) {
                line.erase(hash);
                break;
            }
            while (hash + 1 < line.size() && line[hash + 1] == '#') {
                ++hash;
            }
        }
        source.push_back({ number, line });
    }
    std::vector<SourceLine> flat;
    if (!expandLoops(source, {}, 0, &flat, path, error)) {
        return false;
    }
    for (const SourceLine& entry : flat) {
        if (!parse(entry.text, entry.number, error)) {
            return false;
        }
    }
    loaded_ = true;
    return true;
}

bool UiScript::parse(const std::string& line, int number, std::string* error) {
    const std::vector<std::string> w = words(line);
    if (w.empty()) {
        return true;
    }
    const auto fail = [&](const char* why) {
        if (error != nullptr) {
            *error = path_ + ":" + std::to_string(number) + ": " + why + ": " + line;
        }
        return false;
    };
    const auto add = [&](Step step) {
        step.line = number;
        steps_.push_back(std::move(step));
    };
    const auto buttonOf = [](const std::vector<std::string>& args, size_t at) {
        return at < args.size() && args[at] == "right" ? ImGuiMouseButton_Right
                                                        : ImGuiMouseButton_Left;
    };
    const std::string& verb = w[0];
    if (verb == "reference" && w.size() >= 2) {
        Step r; r.kind = Step::Reference;
        r.text = line.substr(line.find("reference") + 10);
        while (!r.text.empty() && r.text.back() == ' ') { r.text.pop_back(); }
        add(r);
    } else if (verb == "open" && w.size() >= 2) {
        Step o; o.kind = Step::Open;
        o.text = line.substr(line.find("open") + 5);
        while (!o.text.empty() && o.text.back() == ' ') { o.text.pop_back(); }
        add(o);
    } else if (verb == "answer" && w.size() >= 3) {
        Step a; a.kind = Step::Answer;
        a.args.push_back(w[1]);
        const size_t at = line.find(w[1]) + w[1].size() + 1;
        a.text = line.substr(at);
        while (!a.text.empty() && a.text.back() == ' ') { a.text.pop_back(); }
        add(a);
    } else if (verb == "remove" && w.size() >= 2) {
        Step r; r.kind = Step::Remove;
        r.text = line.substr(line.find("remove") + 7);
        while (!r.text.empty() && r.text.back() == ' ') { r.text.pop_back(); }
        add(r);
    } else if (verb == "role" && w.size() == 3) {
        Step r; r.kind = Step::Role;
        r.text = w[1];
        r.args.push_back(w[2]);
        add(r);
    } else if (verb == "theme" && w.size() == 2) {
        Step t; t.kind = Step::UseTheme; t.text = w[1]; add(t);
    } else if (verb == "scale" && w.size() == 2) {
        Step s; s.kind = Step::UseScale;
        if (!fast::number(w[1], &s.x) || s.x < 75.f || s.x > 150.f) {
            return fail("not an interface size from 75 to 150");
        }
        add(s);
    } else if (verb == "language" && w.size() == 2) {
        Step l; l.kind = Step::UseLanguage; l.text = w[1]; add(l);
    } else if (verb == "layout" && w.size() == 2) {
        if (w[1] != "drawing" && w[1] != "animation" && w[1] != "compact") {
            return fail("not a layout: drawing, animation or compact");
        }
        Step l; l.kind = Step::UseLayout; l.text = w[1]; add(l);
    } else if (verb == "demo-roles" && w.size() == 1) {
        Step d; d.kind = Step::DemoRoles; add(d);
    } else if (verb == "zoom" && w.size() == 2) {
        Step z; z.kind = Step::ZoomTo;
        if (!fast::number(w[1], &z.x)) { return fail("not a zoom"); }
        add(z);
    } else if (verb == "start" && w.size() == 1) {
        Step s; s.kind = Step::Start; add(s);
    } else if (verb == "tool" && w.size() == 2) {
        Step s; s.kind = Step::Tool; s.text = w[1]; add(s);
    } else if (verb == "colour" && w.size() == 2) {
        ls::Color c;
        if (!hexColour(w[1], &c)) { return fail("not a colour"); }
        Step s; s.kind = Step::Colour; s.text = w[1]; add(s);
    } else if (verb == "move" && w.size() == 3) {
        Step s; s.kind = Step::Pos;
        if (!fast::number(w[1], &s.x) || !fast::number(w[2], &s.y)) { return fail("not a point"); }
        add(s);
    } else if ((verb == "press" || verb == "release") && w.size() <= 2) {
        Step s; s.kind = Step::Button; s.button = buttonOf(w, 1); s.down = verb == "press"; add(s);
    } else if (verb == "click" && (w.size() == 3 || w.size() == 4)) {
        Step at; at.kind = Step::Pos;
        if (!fast::number(w[1], &at.x) || !fast::number(w[2], &at.y)) { return fail("not a point"); }
        add(at);
        Step down; down.kind = Step::Button; down.button = buttonOf(w, 3); down.down = true; add(down);
        Step up = down; up.down = false; add(up);
    } else if (verb == "drag" && w.size() >= 5 && w.size() <= 7) {
        float x0, y0, x1, y1;
        if (!fast::number(w[1], &x0) || !fast::number(w[2], &y0) ||
            !fast::number(w[3], &x1) || !fast::number(w[4], &y1)) {
            return fail("not two points");
        }
        int steps = 12;
        int button = ImGuiMouseButton_Left;
        for (size_t i = 5; i < w.size(); ++i) {
            if (w[i] == "right") { button = ImGuiMouseButton_Right; }
            else if (w[i] == "left") { button = ImGuiMouseButton_Left; }
            else { steps = std::max(1, std::atoi(w[i].c_str())); }
        }
        Step at; at.kind = Step::Pos; at.x = x0; at.y = y0; add(at);
        Step down; down.kind = Step::Button; down.button = button; down.down = true; add(down);
        for (int i = 1; i <= steps; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(steps);
            Step move; move.kind = Step::Pos;
            move.x = x0 + (x1 - x0) * t;
            move.y = y0 + (y1 - y0) * t;
            add(move);
        }
        Step up = down; up.down = false; add(up);
    } else if (verb == "drag-screen" && w.size() >= 3 && w.size() <= 5) {
        float dx, dy;
        if (!fast::number(w[1], &dx) || !fast::number(w[2], &dy)) { return fail("not a distance"); }
        int steps = 12;
        int button = ImGuiMouseButton_Left;
        for (size_t i = 3; i < w.size(); ++i) {
            if (w[i] == "middle") { button = ImGuiMouseButton_Middle; }
            else if (w[i] == "right") { button = ImGuiMouseButton_Right; }
            else { steps = std::max(1, std::atoi(w[i].c_str())); }
        }
        Step down; down.kind = Step::Button; down.button = button; down.down = true; add(down);
        for (int i = 1; i <= steps; ++i) {
            // Each step moves by its share, from wherever the pointer then is.
            Step move; move.kind = Step::ScreenMove;
            move.x = dx / static_cast<float>(steps);
            move.y = dy / static_cast<float>(steps);
            add(move);
        }
        Step up = down; up.down = false; add(up);
    } else if (verb == "wheel" && w.size() == 2) {
        Step s; s.kind = Step::Wheel;
        if (!fast::number(w[1], &s.y)) { return fail("not a number"); }
        add(s);
    } else if ((verb == "hold" || verb == "let") && w.size() == 2) {
        Step s; s.kind = Step::Key; s.key = keyNamed(w[1]); s.down = verb == "hold";
        if (s.key == ImGuiKey_None) { return fail("not a key"); }
        add(s);
    } else if (verb == "key" && w.size() == 2) {
        // ctrl+shift+z: modifiers down, the key down, then all up.
        std::vector<ImGuiKey> keys;
        std::string chord = w[1];
        size_t start = 0;
        while (start <= chord.size()) {
            size_t plus = chord.find('+', start);
            // A chord ending in '+' names the plus key; there is none here.
            const std::string part = chord.substr(start, plus == std::string::npos ? std::string::npos
                                                                                   : plus - start);
            const ImGuiKey key = keyNamed(part);
            if (key == ImGuiKey_None) { return fail("not a key"); }
            keys.push_back(key);
            if (plus == std::string::npos) { break; }
            start = plus + 1;
        }
        for (ImGuiKey key : keys) {
            Step s; s.kind = Step::Key; s.key = key; s.down = true; add(s);
        }
        Step pause; pause.kind = Step::Wait; add(pause);
        for (size_t i = keys.size(); i-- > 0;) {
            Step s; s.kind = Step::Key; s.key = keys[i]; s.down = false; add(s);
        }
    } else if ((verb == "click-on" || verb == "right-click-on" || verb == "double-click-on") &&
               w.size() >= 2) {
        Step at; at.kind = Step::Widget; at.text = line.substr(line.find(verb) + verb.size() + 1);
        splitIndex(&at.text, &at.index);
        const int clicks = verb == "double-click-on" ? 2 : 1;
        at.skip = clicks * 2;
        add(at);
        for (int c = 0; c < clicks; ++c) {
            Step down; down.kind = Step::Button; down.down = true;
            down.button = verb == "right-click-on" ? ImGuiMouseButton_Right : ImGuiMouseButton_Left;
            add(down);
            Step up = down; up.down = false; add(up);
        }
    } else if (verb == "menu" && w.size() >= 2) {
        // A path through the menus, `menu File > Export PNG > 2x`: each label
        // clicked in turn, and two frames after the last for the menus to close --
        // a File clicked while the menu is still open would shut it.
        std::string rest = line.substr(line.find(verb) + verb.size() + 1);
        size_t start = 0;
        while (start <= rest.size()) {
            size_t arrow = rest.find(" > ", start);
            std::string label = rest.substr(start, arrow == std::string::npos ? std::string::npos
                                                                               : arrow - start);
            while (!label.empty() && label.back() == ' ') { label.pop_back(); }
            if (label.empty()) { return fail("an empty step in the menu path"); }
            Step at; at.kind = Step::Widget; at.text = label;
            splitIndex(&at.text, &at.index);
            // The menu bar is drawn before the panels, some of which have a button
            // of the same name; a menu's is the first, unless the path says otherwise.
            if (at.index == 0) { at.index = 1; }
            at.skip = 2;
            add(at);
            Step down; down.kind = Step::Button; down.down = true; down.button = ImGuiMouseButton_Left;
            add(down);
            Step up = down; up.down = false; add(up);
            if (arrow == std::string::npos) { break; }
            start = arrow + 3;
        }
        Step settle; settle.kind = Step::Wait;
        add(settle);
        add(settle);
    } else if (verb == "hover-on" && w.size() >= 2) {
        // The pointer onto a widget and left there, with no click.
        Step at; at.kind = Step::Widget; at.text = line.substr(line.find(verb) + verb.size() + 1);
        splitIndex(&at.text, &at.index);
        at.skip = 0;
        add(at);
    } else if (verb == "drag-onto" && line.find("=>") != std::string::npos) {
        const std::string rest = line.substr(line.find(verb) + verb.size() + 1);
        const size_t arrow = rest.find("=>");
        Step from; from.kind = Step::Widget; from.text = rest.substr(0, arrow);
        splitIndex(&from.text, &from.index);
        Step onto; onto.kind = Step::Widget; onto.text = rest.substr(arrow + 2);
        while (!onto.text.empty() && onto.text.front() == ' ') { onto.text.erase(0, 1); }
        splitIndex(&onto.text, &onto.index);
        from.skip = 6;
        add(from);
        Step down; down.kind = Step::Button; down.button = ImGuiMouseButton_Left; down.down = true;
        add(down);
        // Far enough to count as a drag before heading for the target.
        Step nudge; nudge.kind = Step::ScreenMove; nudge.x = 0.f; nudge.y = 4.f;
        add(nudge);
        add(nudge);
        onto.skip = 2;
        add(onto);
        Step settle; settle.kind = Step::Wait; add(settle);
        Step up = down; up.down = false; add(up);
    } else if (verb == "drag-from" && line.find(" by ") != std::string::npos) {
        // A widget picked up and dragged by a distance: a panel's tab out of
        // its dock, the line between two panels.
        const std::string rest = line.substr(line.find(verb) + verb.size() + 1);
        const size_t by = rest.rfind(" by ");
        Step from; from.kind = Step::Widget; from.text = rest.substr(0, by);
        splitIndex(&from.text, &from.index);
        std::istringstream distance(rest.substr(by + 4));
        float dx = 0.f, dy = 0.f;
        if (!(distance >> dx >> dy)) { return fail("not a distance"); }
        constexpr int kSteps = 16;
        from.skip = kSteps + 3;
        add(from);
        Step down; down.kind = Step::Button; down.button = ImGuiMouseButton_Left; down.down = true;
        add(down);
        for (int i = 0; i < kSteps; ++i) {
            Step move; move.kind = Step::ScreenMove;
            move.x = dx / static_cast<float>(kSteps);
            move.y = dy / static_cast<float>(kSteps);
            add(move);
        }
        Step settle; settle.kind = Step::Wait; add(settle);
        Step up = down; up.down = false; add(up);
    } else if (verb == "type" && w.size() >= 2) {
        Step s; s.kind = Step::Text; s.text = line.substr(line.find("type") + 5); add(s);
    } else if (verb == "pause" && w.size() == 2) {
        Step p; p.kind = Step::Pause;
        if (!fast::number(w[1], &p.x) || p.x < 0.f || p.x > 20000.f) {
            return fail("not a time in milliseconds");
        }
        add(p);
    } else if (verb == "wait" && w.size() == 2) {
        const int frames = std::max(1, std::atoi(w[1].c_str()));
        for (int i = 0; i < frames; ++i) {
            Step s; s.kind = Step::Wait; add(s);
        }
    } else if (verb == "expect" && w.size() >= 3) {
        Step s; s.kind = Step::Expect; s.text = w[1];
        s.args.assign(w.begin() + 2, w.end());
        if (s.text == "item" || s.text == "missing") {
            // A label is the rest of the line, spaces and all.
            std::string label = line.substr(line.find(s.text) + s.text.size() + 1);
            while (!label.empty() && label.back() == ' ') { label.pop_back(); }
            s.args = { label };
        }
        // One settled frame first, so the last input has been acted on.
        Step settle; settle.kind = Step::Wait; add(settle);
        add(s);
    } else if (verb == "shot" && w.size() == 2) {
        Step s; s.kind = Step::Shot; s.text = w[1]; add(s);
    } else {
        return fail("not a command");
    }
    return true;
}

namespace {

// What ImGui says about how the interface was built -- a cursor moved without an
// item after it, a Push without its Pop, an id used twice. Counted, so that a
// script fails on it; the first message is kept to say what it was.
int gInterfaceErrors = 0;
std::string gFirstInterfaceError;

void onInterfaceError(ImGuiContext*, void*, const char* message) {
    if (gInterfaceErrors++ == 0) {
        gFirstInterfaceError = message != nullptr ? message : "";
    }
}

} // namespace

void watchInterfaceErrors() {
    ImGuiIO& io = ImGui::GetIO();
    // ImGui can draw each mistake as a red box and a tooltip, and stop on it. That
    // is for the person building the interface; one using it should see neither.
    io.ConfigErrorRecoveryEnableTooltip = false;
    io.ConfigErrorRecoveryEnableAssert = false;
    ImGui::GetCurrentContext()->ErrorCallback = onInterfaceError;
}

void UiScript::feed(Editor& editor, CanvasView& canvas) {
    if (!active()) {
        return;
    }
    if (gInterfaceErrors > 0 && !reportedInterfaceError_) {
        reportedInterfaceError_ = true;
        const int line = next_ > 0 && next_ <= steps_.size() ? steps_[next_ - 1].line : 0;
        std::printf("FAIL script:%d: ImGui reported an error in how the interface was built: "
                    "%s\n", line, gFirstInterfaceError.c_str());
        ++failures_;
        ++checks_;
    }
    // The frame just drawn is the one a label is looked for in.
    ImGui::GetCurrentContext()->TestEngineHookItems = true;
    gDrawn.swap(gDrawing);
    gDrawing.clear();
    gEarlyLabels.clear();
    if (warmup_ > 0) {
        --warmup_;
        return;
    }
    ImGuiIO& io = ImGui::GetIO();
    const auto screen = [&](float x, float y) {
        const ImVec2 origin = canvas.artworkOrigin();
        return ImVec2(origin.x + (x + 0.5f) * canvas.zoom(), origin.y + (y + 0.5f) * canvas.zoom());
    };
    // The backend queues the real pointer each frame while the window has the
    // focus; this comes after it, so the script's pointer is the one that counts.
    if (pointerSet_) {
        const ImVec2 at = pointerOnWindow_ ? ImVec2(pointerX_, pointerY_)
                                           : screen(pointerX_, pointerY_);
        io.AddMousePosEvent(at.x, at.y);
    }
    // Steps that take no frame run at once; the first that does ends the frame.
    while (next_ < steps_.size()) {
        const Step& step = steps_[next_++];
        switch (step.kind) {
            case Step::Reference: {
                std::string error;
                if (!importReferenceToPick(editor, canvas, step.text, &error)) {
                    std::printf("FAIL script:%d: could not import %s: %s\n", step.line,
                                step.text.c_str(), error.c_str());
                    ++failures_;
                    ++checks_;
                }
                return;
            }
            case Step::Open:
                // Opening a path needs no window; only the dialogs do.
                requestAction(editor, canvas, nullptr, PendingAction::OpenPath, step.text);
                return;
            case Step::Start:
                openStartScreen(editor);
                return;
            case Step::ZoomTo:
                canvas.setZoom(step.x);
                return;
            case Step::Role: {
                Token token;
                Rgba colour;
                if (!parseColour(step.args[0], &colour)) {
                    std::printf("FAIL script:%d: not a colour\n", step.line);
                    ++failures_;
                    ++checks_;
                    return;
                }
                if (tokenFromName(step.text, &token)) {
                    editRole(editor, token, colour);
                } else if (findExtraRole(step.text) != nullptr) {
                    editExtraRole(editor, step.text, colour);
                } else {
                    std::printf("FAIL script:%d: no role called %s\n", step.line,
                                step.text.c_str());
                    ++failures_;
                    ++checks_;
                }
                return;
            }
            case Step::UseTheme: {
                Theme chosen;
                if (!findTheme(themeFolderFor(editor), step.text, &chosen)) {
                    std::printf("FAIL script:%d: no theme called %s\n", step.line,
                                step.text.c_str());
                    ++failures_;
                    ++checks_;
                    return;
                }
                selectTheme(editor, chosen);
                return;
            }
            case Step::UseScale:
                editor.prefs.uiScale = static_cast<int>(step.x);
                theme::setScale(step.x / 100.f);
                return;
            case Step::UseLanguage:
                editor.prefs.language = step.text == "en" ? std::string() : step.text;
                if (!applyLanguage(editor.prefs.language)) {
                    std::printf("FAIL script:%d: no language called %s\n", step.line,
                                step.text.c_str());
                    ++failures_;
                    ++checks_;
                }
                return;
            case Step::UseLayout:
                useLayoutPreset(editor, step.text == "animation" ? LayoutPreset::Animation
                                        : step.text == "compact" ? LayoutPreset::Compact
                                                                 : LayoutPreset::Drawing);
                return;
            case Step::DemoRoles:
                addExtraRoles(demoRoles());
                return;
            case Step::Pause: {
                // Real time with the frames running, a frame at a time.
                const uint64_t now = SDL_GetTicks();
                if (pauseUntil_ == 0) {
                    pauseUntil_ = now + static_cast<uint64_t>(step.x) + 1;
                }
                if (now < pauseUntil_) {
                    --next_;
                    return;
                }
                pauseUntil_ = 0;
                return;
            }
            case Step::Answer: {
                // What a native dialog's callback would have left, put where it
                // would have left it.
                using Kind = DialogResult::Kind;
                const std::string& which = step.args[0];
                const Kind kind = dialogFromName(which);
                if (kind == Kind::None) {
                    std::printf("FAIL script:%d: no dialog called %s\n", step.line, which.c_str());
                    ++failures_;
                    ++checks_;
                    return;
                }
                SDL_LockMutex(editor.files.dialog.mutex);
                editor.files.dialog.kind = kind;
                editor.files.dialog.path = step.text;
                editor.files.dialog.cancelled = false;
                editor.files.dialog.ready = true;
                SDL_UnlockMutex(editor.files.dialog.mutex);
                return;
            }
            case Step::Remove:
                deleteFile(step.text);
                continue;
            case Step::Tool: {
                Tool tool;
                if (toolFromName(step.text, &tool)) {
                    editor.tool = tool;
                } else {
                    std::printf("script:%d: no tool called %s\n", step.line, step.text.c_str());
                    ++failures_;
                }
                continue;
            }
            case Step::Colour: {
                ls::Color c;
                hexColour(step.text, &c);
                editor.color[0] = static_cast<float>(c.r) / 255.f;
                editor.color[1] = static_cast<float>(c.g) / 255.f;
                editor.color[2] = static_cast<float>(c.b) / 255.f;
                editor.color[3] = static_cast<float>(c.a) / 255.f;
                editor.inkRole = ls::kColorRoleNone;
                continue;
            }
            case Step::Expect:
                expect(step, editor, canvas);
                continue;
            case Step::Shot:
                shot_ = step.text;
                continue;
            case Step::ScreenMove: {
                // From the pointer's place on screen, which from now on is
                // held in window coordinates: the canvas may move under it.
                const ImVec2 from = pointerOnWindow_ ? ImVec2(pointerX_, pointerY_)
                                                     : screen(pointerX_, pointerY_);
                pointerSet_ = true;
                pointerOnWindow_ = true;
                pointerX_ = from.x + step.x;
                pointerY_ = from.y + step.y;
                io.AddMousePosEvent(pointerX_, pointerY_);
                return;
            }
            case Step::Wheel:
                io.AddMouseWheelEvent(0.f, step.y);
                return;
            case Step::Widget: {
                ImVec2 centre;
                if (!findDrawn(step.text, step.index, &centre)) {
                    // A window opened by the last click may take a frame or
                    // two to be drawn: wait for it, as a person would.
                    if (++waitedFor_ < 30) {
                        --next_;
                        return;
                    }
                    waitedFor_ = 0;
                    std::printf("FAIL script:%d: nothing called \"%s\" on screen\n", step.line,
                                step.text.c_str());
                    ++failures_;
                    ++checks_;
                    // What would have acted on it has nothing to act on.
                    next_ += static_cast<size_t>(step.skip);
                    return;
                }
                waitedFor_ = 0;
                pointerSet_ = true;
                pointerOnWindow_ = true;
                pointerX_ = centre.x;
                pointerY_ = centre.y;
                io.AddMousePosEvent(centre.x, centre.y);
                return;
            }
            case Step::Pos: {
                // A canvas pixel the view does not show is somewhere else on
                // screen -- a ruler, a panel -- and a press there would do
                // that thing instead. Said, rather than clicked into.
                const ImVec2 on = screen(step.x, step.y);
                const ImVec2 min = canvas.viewTopLeft();
                const ImVec2 size = canvas.viewSize();
                if (on.x < min.x || on.y < min.y || on.x >= min.x + size.x ||
                    on.y >= min.y + size.y) {
                    std::printf("FAIL script:%d: pixel %g, %g is not in view -- fit the view "
                                "first (key ctrl+0)\n", step.line,
                                static_cast<double>(step.x), static_cast<double>(step.y));
                    ++failures_;
                    ++checks_;
                }
                pointerSet_ = true;
                pointerOnWindow_ = false;
                pointerX_ = step.x;
                pointerY_ = step.y;
                const ImVec2 at = screen(step.x, step.y);
                io.AddMousePosEvent(at.x, at.y);
                return;
            }
            case Step::Button:
                io.AddMouseButtonEvent(step.button, step.down);
                return;
            case Step::Key:
                io.AddKeyEvent(step.key, step.down);
                return;
            case Step::Text:
                io.AddInputCharactersUTF8(step.text.c_str());
                return;
            case Step::Wait:
                return;
        }
    }
    finished_ = true;
    std::printf("script: %d check(s), %d failed\n", checks_, failures_);
}

void UiScript::finishEarly() {
    if (!active()) {
        return;
    }
    finished_ = true;
    std::printf("script: %d check(s), %d failed (the program quit at step %zu of %zu)\n",
                checks_, failures_, next_, steps_.size());
}

std::string UiScript::takeShot() {
    std::string out;
    out.swap(shot_);
    return out;
}

void UiScript::expect(const Step& step, Editor& editor, CanvasView& canvas) {
    ++checks_;
    const auto failed = [&](const std::string& what) {
        std::printf("FAIL script:%d: expect %s -- %s\n", step.line, step.text.c_str(), what.c_str());
        ++failures_;
    };
    // What the canvas shows: the frame, or a puppet being posed.
    const auto picture = [&](ls::RasterBuffer* out) {
        if (const ls::SpriteId root = canvas.assemblyShown(); root.valid()) {
            auto size = editor.doc.engine().getCanvasSize(editor.doc.id());
            auto compiled = size.ok() ? editor.doc.engine().compileAssembly(
                root, compileProfile(ls::CompileProfileType::Export,
                                     static_cast<uint32_t>(size.value.x),
                                     static_cast<uint32_t>(size.value.y)))
                                      : ls::Result<ls::CompileResult>::err(ls::LSError::InvalidId);
            if (compiled.fail()) {
                return false;
            }
            *out = std::move(compiled.value.raster);
            return true;
        }
        return compileForExport(editor.doc, editor.sprite, 1, out, nullptr);
    };
    const std::vector<std::string>& a = step.args;
    if (step.text == "drawn") {
        ls::RasterBuffer raster;
        if (!picture(&raster)) { failed("the frame did not compile"); return; }
        int drawn = 0;
        for (size_t i = 3; i < raster.pixels.size(); i += 4) {
            drawn += raster.pixels[i] != 0 ? 1 : 0;
        }
        const int least = std::atoi(a[0].c_str());
        const int most = a.size() > 1 ? std::atoi(a[1].c_str()) : -1;
        if (drawn < least || (most >= 0 && drawn > most)) {
            failed(std::to_string(drawn) + " pixels drawn");
        }
    } else if (step.text == "pixel" && a.size() == 3) {
        ls::RasterBuffer raster;
        if (!picture(&raster)) { failed("the frame did not compile"); return; }
        const int x = std::atoi(a[0].c_str());
        const int y = std::atoi(a[1].c_str());
        if (x < 0 || y < 0 || x >= static_cast<int>(raster.width) ||
            y >= static_cast<int>(raster.height)) {
            failed("off the canvas");
            return;
        }
        const ls::Color got = ls::readPixel(raster, x, y);
        char seen[16];
        std::snprintf(seen, sizeof(seen), "%02x%02x%02x%02x", got.r, got.g, got.b, got.a);
        if (a[2] == "empty") {
            if (got.a != 0) { failed(std::string("found ") + seen); }
            return;
        }
        ls::Color want;
        if (!hexColour(a[2], &want) || got.r != want.r || got.g != want.g || got.b != want.b ||
            got.a != want.a) {
            failed(std::string("found ") + seen);
        }
    } else if (step.text == "elements" && a.size() == 2) {
        PaintLayer* layer = editor.active();
        int count = 0;
        if (layer != nullptr) {
            for (const Element& element : elementsOf(editor.doc, layer->layer)) {
                count += a[0] == elementKindName(element.kind) ? 1 : 0;
            }
        }
        if (count != std::atoi(a[1].c_str())) {
            failed(std::to_string(count) + " found");
        }
    } else if (step.text == "selected") {
        const int64_t count = ls::geom::pixelCount(editor.selection.mask);
        const int64_t least = std::atoll(a[0].c_str());
        const int64_t most = a.size() > 1 ? std::atoll(a[1].c_str()) : least;
        if (count < least || count > most) {
            failed(std::to_string(count) + " selected");
        }
    } else if (step.text == "layers") {
        const size_t count = layerOrder(editor.doc, editor.sprite).size();
        if (count != static_cast<size_t>(std::atoi(a[0].c_str()))) {
            failed(std::to_string(count) + " layers");
        }
    } else if (step.text == "frames") {
        const size_t count = readFrames(editor.doc).size();
        if (count != static_cast<size_t>(std::atoi(a[0].c_str()))) {
            failed(std::to_string(count) + " frames");
        }
    } else if (step.text == "item") {
        ImVec2 centre;
        std::string label = a[0];
        int index = 0;
        splitIndex(&label, &index);
        if (!findDrawn(label, index, &centre)) {
            failed("nothing called \"" + a[0] + "\" on screen");
        }
    } else if (step.text == "missing") {
        ImVec2 centre;
        std::string label = a[0];
        int index = 0;
        splitIndex(&label, &index);
        if (findDrawn(label, index, &centre)) {
            failed("\"" + a[0] + "\" is on screen");
        }
    } else if (step.text == "docked" && a.size() == 2) {
        // A panel by its ID (see layout.h): docked, or floating.
        const ImGuiWindow* window = ImGui::FindWindowByName(("###" + a[0]).c_str());
        const bool docked = window != nullptr && window->DockIsActive;
        if (window == nullptr || docked != (std::atoi(a[1].c_str()) != 0)) {
            failed(window == nullptr ? "no panel " + a[0]
                                     : a[0] + (docked ? " is docked" : " is floating"));
        }
    } else if (step.text == "width" && a.size() == 3) {
        const ImGuiWindow* window = ImGui::FindWindowByName(("###" + a[0]).c_str());
        const float width = window != nullptr ? window->Size.x : -1.f;
        if (width < std::atof(a[1].c_str()) || width > std::atof(a[2].c_str())) {
            failed(a[0] + " is " + std::to_string(width) + " wide");
        }
    } else if (step.text == "height" && a.size() == 3) {
        const ImGuiWindow* window = ImGui::FindWindowByName(("###" + a[0]).c_str());
        const float height = window != nullptr ? window->Size.y : -1.f;
        if (height < std::atof(a[1].c_str()) || height > std::atof(a[2].c_str())) {
            failed(a[0] + " is " + std::to_string(height) + " tall");
        }
    } else if (step.text == "fits" && a.size() == 1) {
        // Nothing in a panel wider than the panel: a label or a button cut off
        // at its edge is content past the edge, which is what lets a panel
        // scroll sideways (layout.cpp's panelFlags). `fits all` asks it of every
        // panel that is showing, `fits layers` of one.
        static const char* const kAll[] = { "tool", "palette", "layers", "elements", "properties",
                                            "transform", "puppet", "references", "timeline",
                                            "timelinedrawer" };
        std::string wide;
        for (const char* id : kAll) {
            if (a[0] != "all" && a[0] != id) {
                continue;
            }
            const ImGuiWindow* window = ImGui::FindWindowByName(("###" + std::string(id)).c_str());
            if (window == nullptr || !window->Active || window->Hidden || window->SkipItems) {
                continue;
            }
            // Any at all: a sliver of overflow still draws the scrollbar. And a
            // scrollbar with nothing to scroll is as wrong as the overflow.
            if (window->ScrollMax.x > 0.f) {
                wide += std::string(wide.empty() ? "" : ", ") + id + " by " +
                        std::to_string(window->ScrollMax.x).substr(0, 5) + " px";
            } else if (window->ScrollbarX) {
                wide += std::string(wide.empty() ? "" : ", ") + id + " (a sideways scrollbar "
                        "with nothing to scroll)";
            }
        }
        if (!wide.empty()) {
            static const char* const kLayouts[] = { "drawing", "animation", "compact" };
            const int preset = std::clamp(editor.panels.preset, 0, 2);
            failed("wider than the panel: " + wide + "  [" +
                   std::to_string(static_cast<int>(std::lround(theme::scale() * 100.f))) + "%, " +
                   kLayouts[preset] + ", " +
                   (editor.prefs.language.empty() ? "en" : editor.prefs.language) + ", " +
                   std::to_string(static_cast<int>(editor.tool)) + "]");
        }
    } else if (step.text == "scrolls" && a.size() == 1) {
        // The other half of `fits`: a panel that is too narrow for what it holds
        // has a scrollbar along its bottom, and something to scroll to.
        const ImGuiWindow* window = ImGui::FindWindowByName(("###" + a[0]).c_str());
        if (window == nullptr || !window->Active) {
            failed("no panel " + a[0]);
        } else if (!(window->ScrollMax.x > 0.f)) {
            failed(a[0] + " holds nothing wider than it is");
        } else if (!window->ScrollbarX) {
            failed(a[0] + " is too narrow for what it holds, and has no scrollbar");
        }
    } else if (step.text == "guide" && a.size() == 1) {
        // The user guide is open at a topic, by its id: `expect guide drawing`; or
        // `expect guide closed`.
        const std::vector<GuideTopic>& topics = userGuide();
        if (a[0] == "closed") {
            if (editor.guideOpen) { failed("the guide is open"); }
        } else if (!editor.guideOpen) {
            failed("the guide is closed");
        } else if (editor.guideTopic < 0 || editor.guideTopic >= static_cast<int>(topics.size()) ||
                   topics[static_cast<size_t>(editor.guideTopic)].id != a[0]) {
            failed("the guide shows " + (editor.guideTopic >= 0 &&
                                         editor.guideTopic < static_cast<int>(topics.size())
                                             ? topics[static_cast<size_t>(editor.guideTopic)].id
                                             : std::string("nothing")));
        }
    } else if (step.text == "dialog" && a.size() == 1) {
        // The native dialog the last click asked for (none was shown): `expect dialog
        // export-png`, or `expect dialog none`. Reading it clears it.
        SDL_LockMutex(editor.files.dialog.mutex);
        const DialogResult::Kind asked = editor.files.dialog.asked;
        editor.files.dialog.asked = DialogResult::Kind::None;
        SDL_UnlockMutex(editor.files.dialog.mutex);
        if (dialogName(asked) != a[0]) {
            failed("the dialog asked for is " + dialogName(asked));
        }
    } else if (step.text == "flag" && a.size() == 2) {
        // A switch in the menus, on or off: `expect flag rulers on`.
        TileGrid& tiles = canvas.tileGrid();
        const struct { const char* name; bool value; } flags[] = {
            { "rulers",           editor.rulersOn },
            { "guides",           editor.guidesShown },
            { "grid",             canvas.gridVisible() },
            { "snap",             editor.snapToGrid },
            { "symmetry-across",  editor.symmetryAcross },
            { "symmetry-down",    editor.symmetryDown },
            { "preview",          editor.preview.visible },
            { "tile-grid",        tiles.visible },
            { "isometric",        tiles.isometric },
            { "autosave",         editor.autosaveOn },
            { "history",          editor.historyOpen },
            { "slices",           editor.slicesOpen },
            { "library",          editor.libraryOpen },
            { "kit",              editor.kitOpen },
            { "preferences",      editor.preferencesOpen },
            { "modified",         editor.doc.modified() },
            { "tracks",           tracksOn(editor.doc) },
            { "onion",            editor.timeline.onion },
            { "timeline",         editor.timeline.visible },
            { "pinned",           editor.timeline.pinned },
        };
        bool known = false;
        for (const auto& flag : flags) {
            if (a[0] == flag.name) {
                known = true;
                if (flag.value != (a[1] == "on")) {
                    failed(a[0] + " is " + (flag.value ? "on" : "off"));
                }
            }
        }
        if (!known) {
            failed("no switch called " + a[0]);
        }
    } else if (step.text == "tiled" && a.size() == 1) {
        const char* names[] = { "off", "across", "down", "both" };
        const int mode = static_cast<int>(canvas.tiledMode());
        if (mode < 0 || mode > 3 || a[0] != names[mode]) {
            failed("tiled mode is " + std::string(mode >= 0 && mode <= 3 ? names[mode] : "?"));
        }
    } else if (step.text == "theme" && a.size() == 1) {
        if (theme::current().id != a[0]) {
            failed("the theme is " + theme::current().id);
        }
    } else if (step.text == "colour" && a.size() == 2) {
        // A role of the theme in force: `expect colour panel 1d2229`, or a
        // program's own by its id.
        Token token;
        Rgba want;
        if (!parseColour(a[1], &want)) {
            failed("not a colour");
        } else if (tokenFromName(a[0], &token)) {
            if (theme::current()[token] != want) {
                failed(a[0] + " is " + colourText(theme::current()[token]));
            }
        } else if (findExtraRole(a[0]) != nullptr) {
            if (theme::current().extraColour(a[0]) != want) {
                failed(a[0] + " is " + colourText(theme::current().extraColour(a[0])));
            }
        } else {
            failed("no role called " + a[0]);
        }
    } else if (step.text == "uiscale" && a.size() == 1) {
        const int percent = static_cast<int>(std::lround(theme::scale() * 100.f));
        if (percent != std::atoi(a[0].c_str())) {
            failed("the interface is at " + std::to_string(percent) + "%");
        }
    } else if ((step.text == "file" || step.text == "nofile") && a.size() == 1) {
        const bool there = fileExists(a[0]);
        if (there != (step.text == "file")) {
            failed(a[0] + (there ? " exists" : " is not there"));
        }
    } else if (step.text == "canvas" && a.size() == 2) {
        auto size = editor.doc.engine().getCanvasSize(editor.doc.id());
        if (size.fail() || size.value.x != std::atoi(a[0].c_str()) ||
            size.value.y != std::atoi(a[1].c_str())) {
            failed(size.ok() ? "the canvas is " + std::to_string(size.value.x) + " x " +
                                   std::to_string(size.value.y)
                             : std::string("no canvas"));
        }
    } else if (step.text == "tabs") {
        const int count = static_cast<int>(tabCount(editor));
        if (count != std::atoi(a[0].c_str())) {
            failed(std::to_string(count) + " tabs");
        }
    } else if (step.text == "frame") {
        if (editor.timeline.activeFrame + 1 != std::atoi(a[0].c_str())) {
            failed("frame " + std::to_string(editor.timeline.activeFrame + 1) + " is showing");
        }
    } else if (step.text == "playing") {
        if (editor.timeline.playing != (a[0] == "yes")) {
            failed(editor.timeline.playing ? "it is playing" : "it is not playing");
        }
    } else if (step.text == "pointer" && a.size() == 2) {
        const ls::Vec2i at = canvas.pointerPixel();
        if (at.x != std::atoi(a[0].c_str()) || at.y != std::atoi(a[1].c_str())) {
            failed("the pointer is over " + std::to_string(at.x) + ", " + std::to_string(at.y));
        }
    } else if (step.text == "guides") {
        const size_t count = readGuides(editor.doc).size();
        if (count != static_cast<size_t>(std::atoi(a[0].c_str()))) {
            failed(std::to_string(count) + " guides");
        }
    } else if (step.text == "slices") {
        const size_t count = readSlices(editor.doc).size();
        if (count != static_cast<size_t>(std::atoi(a[0].c_str()))) {
            failed(std::to_string(count) + " slices");
        }
    } else if (step.text == "zoom") {
        // One number is exact; two are a range, for a fit, which depends on how
        // much room the layout leaves the canvas.
        const float low = static_cast<float>(std::atof(a[0].c_str()));
        const float high = a.size() > 1 ? static_cast<float>(std::atof(a[1].c_str())) : low;
        if (canvas.zoom() < low || canvas.zoom() > high) {
            failed("zoom is " + std::to_string(canvas.zoom()));
        }
    } else if (step.text == "section" && a.size() == 4) {
        const Reference* shown = activeReference(editor);
        if (shown == nullptr) {
            failed("no reference is picked");
        } else {
            char seen[64];
            std::snprintf(seen, sizeof(seen), "%u %u %u %u", shown->shownX(), shown->shownY(),
                          shown->shownWidth(), shown->shownHeight());
            const std::string wanted = a[0] + " " + a[1] + " " + a[2] + " " + a[3];
            if (wanted != seen) {
                failed(std::string("the section is ") + seen);
            }
        }
    } else if (step.text == "rotation" || step.text == "scale" || step.text == "offset") {
        // The active layer's last Rotate, Scale or Offset (the Transform
        // panel's).
        const bool rotation = step.text == "rotation";
        const TransformKind kind = rotation ? TransformKind::Rotate
                                 : step.text == "scale" ? TransformKind::Scale
                                                        : TransformKind::Offset;
        const TransformEntry* last = nullptr;
        std::vector<TransformEntry> entries;
        if (const PaintLayer* layer = editor.active()) {
            entries = listTransforms(editor.doc, layer->layer);
        }
        for (const TransformEntry& entry : entries) {
            if (entry.kind == kind) {
                last = &entry;
            }
        }
        const auto near = [](float a, float b) { return std::fabs(a - b) < 1e-3f; };
        if (last == nullptr) {
            failed("the layer has no " + step.text);
        } else if (kind == TransformKind::Offset) {
            if (a.size() < 2 || !near(last->delta.x, static_cast<float>(std::atof(a[0].c_str()))) ||
                !near(last->delta.y, static_cast<float>(std::atof(a[1].c_str())))) {
                failed("the offset is " + std::to_string(last->delta.x) + ", " +
                       std::to_string(last->delta.y));
            }
        } else if (rotation) {
            if (!near(last->angleDegrees, static_cast<float>(std::atof(a[0].c_str())))) {
                failed("the rotation is " + std::to_string(last->angleDegrees));
            }
        } else if (a.size() < 2 ||
                   !near(last->factor.x, static_cast<float>(std::atof(a[0].c_str()))) ||
                   !near(last->factor.y, static_cast<float>(std::atof(a[1].c_str())))) {
            failed("the scale is " + std::to_string(last->factor.x) + " x " +
                   std::to_string(last->factor.y));
        }
    } else if (step.text == "brush") {
        if (editor.brush.size != std::atoi(a[0].c_str())) {
            failed("brush is " + std::to_string(editor.brush.size));
        }
    } else if (step.text == "tool") {
        Tool want;
        if (!toolFromName(a[0], &want) || editor.tool != want) {
            failed("another tool is in hand");
        }
    } else {
        failed("not something to expect");
    }
}

} // namespace fast
