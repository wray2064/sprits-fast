// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

#include "ui/keys.h"

#include <utility>

namespace fast {

namespace {

const std::pair<const char*, ImGuiKey> kKeys[] = {
    { "A", ImGuiKey_A }, { "B", ImGuiKey_B }, { "C", ImGuiKey_C }, { "D", ImGuiKey_D },
    { "E", ImGuiKey_E }, { "F", ImGuiKey_F }, { "G", ImGuiKey_G }, { "H", ImGuiKey_H },
    { "I", ImGuiKey_I }, { "J", ImGuiKey_J }, { "K", ImGuiKey_K }, { "L", ImGuiKey_L },
    { "M", ImGuiKey_M }, { "N", ImGuiKey_N }, { "O", ImGuiKey_O }, { "P", ImGuiKey_P },
    { "Q", ImGuiKey_Q }, { "R", ImGuiKey_R }, { "S", ImGuiKey_S }, { "T", ImGuiKey_T },
    { "U", ImGuiKey_U }, { "V", ImGuiKey_V }, { "W", ImGuiKey_W }, { "X", ImGuiKey_X },
    { "Y", ImGuiKey_Y }, { "Z", ImGuiKey_Z },
    { "0", ImGuiKey_0 }, { "1", ImGuiKey_1 }, { "2", ImGuiKey_2 }, { "3", ImGuiKey_3 },
    { "4", ImGuiKey_4 }, { "5", ImGuiKey_5 }, { "6", ImGuiKey_6 }, { "7", ImGuiKey_7 },
    { "8", ImGuiKey_8 }, { "9", ImGuiKey_9 },
    { "F1", ImGuiKey_F1 }, { "F2", ImGuiKey_F2 }, { "F3", ImGuiKey_F3 }, { "F4", ImGuiKey_F4 },
    { "F5", ImGuiKey_F5 }, { "F6", ImGuiKey_F6 }, { "F7", ImGuiKey_F7 }, { "F8", ImGuiKey_F8 },
    { "F9", ImGuiKey_F9 }, { "F10", ImGuiKey_F10 }, { "F11", ImGuiKey_F11 },
    { "F12", ImGuiKey_F12 },
    { "[", ImGuiKey_LeftBracket }, { "]", ImGuiKey_RightBracket }, { ",", ImGuiKey_Comma },
    { ".", ImGuiKey_Period }, { "=", ImGuiKey_Equal }, { "-", ImGuiKey_Minus },
    { "/", ImGuiKey_Slash }, { ";", ImGuiKey_Semicolon }, { "'", ImGuiKey_Apostrophe },
    { "`", ImGuiKey_GraveAccent }, { "\\", ImGuiKey_Backslash },
    { "Enter", ImGuiKey_Enter }, { "Tab", ImGuiKey_Tab }, { "Space", ImGuiKey_Space },
    { "Backspace", ImGuiKey_Backspace }, { "Delete", ImGuiKey_Delete },
    { "Insert", ImGuiKey_Insert }, { "Home", ImGuiKey_Home }, { "End", ImGuiKey_End },
    { "PageUp", ImGuiKey_PageUp }, { "PageDown", ImGuiKey_PageDown },
    { "Left", ImGuiKey_LeftArrow }, { "Right", ImGuiKey_RightArrow },
    { "Up", ImGuiKey_UpArrow }, { "Down", ImGuiKey_DownArrow },
};

} // namespace

ImGuiKey imguiKeyFor(const std::string& name) {
    for (const auto& [text, key] : kKeys) {
        if (name == text) {
            return key;
        }
    }
    return ImGuiKey_None;
}

bool commandPressed(const Keymap& keys, const std::string& id) {
    const CommandInfo* info = findCommand(id);
    if (info == nullptr) {
        return false;
    }
    const ImGuiIO& io = ImGui::GetIO();
    for (const Chord& chord : keys.chordsFor(id)) {
        const ImGuiKey key = imguiKeyFor(chord.key);
        if (key == ImGuiKey_None || chord.ctrl != io.KeyCtrl || chord.shift != io.KeyShift ||
            chord.alt != io.KeyAlt) {
            continue;
        }
        // Enter is Enter on either side of the keyboard.
        if (ImGui::IsKeyPressed(key, info->repeats) ||
            (key == ImGuiKey_Enter && ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, info->repeats))) {
            return true;
        }
    }
    return false;
}

std::string keysLabel(const Keymap& keys, const std::string& id) {
    const std::vector<Chord>& chords = keys.chordsFor(id);
    return chords.empty() ? std::string() : chordText(chords.front());
}

bool chordPressed(Chord* out) {
    if (out == nullptr) {
        return false;
    }
    const ImGuiIO& io = ImGui::GetIO();
    for (const auto& [text, key] : kKeys) {
        if (ImGui::IsKeyPressed(key, false)) {
            out->key = text;
            out->ctrl = io.KeyCtrl;
            out->shift = io.KeyShift;
            out->alt = io.KeyAlt;
            return true;
        }
    }
    return false;
}

} // namespace fast
