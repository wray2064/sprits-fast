// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors
#pragma once

// keymap.h — which keys do what, and a person's own choices about it.
//
// Every shortcut is a named command with default chords, and a person can
// give any command other chords -- the habits of another editor, a left-hand
// layout, a tablet's express keys. Only what differs from the defaults is
// saved, so a new command in a later version arrives with its default rather
// than being lost to an old file.
//
// A chord matches only with exactly its modifiers: Shift+] is not also ],
// which is the bug hand-written shortcut code keeps having.
//
// No toolkit here. A key is a name ("A", "]", "F5", "Enter"); the interface
// maps names to its own key codes.

#include <map>
#include <string>
#include <vector>

namespace fast {

struct Chord {
    std::string key;           // a key name; empty is no chord
    bool ctrl = false;
    bool shift = false;
    bool alt = false;

    bool valid() const { return !key.empty(); }
    bool operator==(const Chord& other) const {
        return key == other.key && ctrl == other.ctrl && shift == other.shift &&
               alt == other.alt;
    }
};

// "Ctrl+Shift+Z" and back. Modifiers in any order, case-insensitive; the key
// must be one of keyNames(). False for anything else.
std::string chordText(const Chord& chord);
bool parseChord(const std::string& text, Chord* out);

// Every key name a chord may use.
const std::vector<std::string>& keyNames();

struct CommandInfo {
    const char*        id;           // "tool.pencil"
    const char*        label;        // "Pencil"
    const char*        group;        // "Tools"
    std::vector<Chord> defaults;
    bool               repeats = false;  // held down, it fires again
};

// Every command a key can be given, in the order the preferences list them.
const std::vector<CommandInfo>& commandList();
const CommandInfo* findCommand(const std::string& id);

class Keymap {
public:
    // The chords a command answers to: the person's, or the default.
    const std::vector<Chord>& chordsFor(const std::string& id) const;
    void setChords(const std::string& id, const std::vector<Chord>& chords);
    void reset(const std::string& id);
    void resetAll() { overrides_.clear(); }
    bool isDefault(const std::string& id) const { return overrides_.count(id) == 0; }

    // The commands a chord would fire, for a warning before it is given to
    // one more.
    std::vector<std::string> commandsUsing(const Chord& chord) const;

    // Only the overrides, one per line: "tool.pencil = N | Shift+N". A file is
    // untrusted: an unknown command or a chord that does not parse is
    // skipped, and the lines read are bounded.
    std::string save() const;
    void load(const std::string& text);

private:
    std::map<std::string, std::vector<Chord>> overrides_;
};

} // namespace fast
