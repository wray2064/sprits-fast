// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors
#pragma once

// keys.h — the keymap, spoken in ImGui's key codes.

#include "app/keymap.h"

#include <imgui.h>

#include <string>

namespace fast {

// A key name's ImGui code, or ImGuiKey_None.
ImGuiKey imguiKeyFor(const std::string& name);

// Whether any of a command's chords was pressed this frame, with exactly its
// modifiers. A command that repeats fires again while its key is held.
bool commandPressed(const Keymap& keys, const std::string& id);

// A command's first chord as text, for a menu: "" when it has none.
std::string keysLabel(const Keymap& keys, const std::string& id);

// The chord pressed this frame, if any key other than a modifier went down --
// what the preferences wait for when a key is being given.
bool chordPressed(Chord* out);

} // namespace fast
