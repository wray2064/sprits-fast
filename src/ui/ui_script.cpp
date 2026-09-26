// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

#include "ui/ui_script.h"

#include "app/animation.h"
#include "app/element.h"
#include "app/export_png.h"
#include "app/file_io.h"
#include "app/layers.h"
#include "app/slices.h"
#include "ui/canvas_view.h"
#include "ui/tabs.h"

#include <imgui_internal.h>

#include <algorithm>
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

} // namespace

void ImGuiTestEngineHook_ItemAdd(ImGuiContext* ctx, ImGuiID id, const ImRect& bb,
                                 const ImGuiLastItemData*) {
    const bool shown = ctx == nullptr || ctx->CurrentWindow == nullptr ||
                       ctx->CurrentWindow->ClipRect.Contains(bb.GetCenter());
    gDrawing.push_back({ id, bb, std::string(), shown });
}

void ImGuiTestEngineHook_ItemInfo(ImGuiContext*, ImGuiID id, const char* label,
                                  ImGuiItemStatusFlags) {
    for (size_t i = gDrawing.size(); i-- > 0;) {
        if (gDrawing[i].id == id) {
            gDrawing[i].label = label != nullptr ? label : "";
            return;
        }
    }
}

void ImGuiTestEngineHook_Log(ImGuiContext*, const char*, ...) {}

const char* ImGuiTestEngine_FindItemDebugLabel(ImGuiContext*, ImGuiID) { return nullptr; }

namespace fast {

namespace {

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
    while (std::getline(in, line)) {
        ++number;
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
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
        if (!parse(line, number, error)) {
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
    if (verb == "tool" && w.size() == 2) {
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
    } else if (verb == "type" && w.size() >= 2) {
        Step s; s.kind = Step::Text; s.text = line.substr(line.find("type") + 5); add(s);
    } else if (verb == "wait" && w.size() == 2) {
        const int frames = std::max(1, std::atoi(w[1].c_str()));
        for (int i = 0; i < frames; ++i) {
            Step s; s.kind = Step::Wait; add(s);
        }
    } else if (verb == "expect" && w.size() >= 3) {
        Step s; s.kind = Step::Expect; s.text = w[1];
        s.args.assign(w.begin() + 2, w.end());
        if (s.text == "item") {
            // A label is the rest of the line, spaces and all.
            std::string label = line.substr(line.find("item") + 5);
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

void UiScript::feed(Editor& editor, CanvasView& canvas) {
    if (!active()) {
        return;
    }
    // The frame just drawn is the one a label is looked for in.
    ImGui::GetCurrentContext()->TestEngineHookItems = true;
    gDrawn.swap(gDrawing);
    gDrawing.clear();
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
    const auto picture = [&](ls::RasterBuffer* out) {
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
    } else if (step.text == "slices") {
        const size_t count = readSlices(editor.doc).size();
        if (count != static_cast<size_t>(std::atoi(a[0].c_str()))) {
            failed(std::to_string(count) + " slices");
        }
    } else if (step.text == "zoom") {
        if (canvas.zoom() != static_cast<float>(std::atof(a[0].c_str()))) {
            failed("zoom is " + std::to_string(canvas.zoom()));
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
