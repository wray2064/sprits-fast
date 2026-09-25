// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

// keymap_tests.cpp — keys a person can choose.
//
// The promises: chords round-trip through text, punctuation keys included; a
// command answers to its defaults until given others; only overrides are
// saved, and a default set back is no longer an override; a file with an
// unknown command or a chord that does not parse loses only that line; a
// chord's users can be listed before it is given away; and no two defaults
// collide.

#include "app/keymap.h"

#include <cstdio>
#include <set>
#include <string>

static int failures = 0;

#define CHECK(...)                                                            \
    do {                                                                      \
        if (!(__VA_ARGS__)) {                                                 \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #__VA_ARGS__);\
            ++failures;                                                       \
        }                                                                     \
    } while (0)

namespace {

using namespace fast;

void testChordsReadAndWrite() {
    Chord chord;
    CHECK(parseChord("Ctrl+Shift+Z", &chord) && chord.ctrl && chord.shift && chord.key == "Z");
    CHECK(chordText(chord) == "Ctrl+Shift+Z");
    CHECK(parseChord("shift + ]", &chord) && chord.shift && chord.key == "]");
    CHECK(parseChord("Ctrl+=", &chord) && chord.key == "=");
    CHECK(parseChord(",", &chord) && chord.key == ",");
    CHECK(parseChord("f5", &chord) && chord.key == "F5");
    CHECK(!parseChord("Ctrl+Nope", &chord));
    CHECK(!parseChord("", &chord));
}

void testOverridesAndDefaults() {
    Keymap keys;
    CHECK(keys.chordsFor("tool.pencil").size() == 1 && keys.chordsFor("tool.pencil")[0].key == "B");
    Chord n;
    parseChord("N", &n);
    keys.setChords("tool.pencil", { n });
    CHECK(keys.chordsFor("tool.pencil")[0].key == "N" && !keys.isDefault("tool.pencil"));
    const std::string saved = keys.save();
    CHECK(saved.find("tool.pencil = N") != std::string::npos);
    CHECK(saved.find("tool.eraser") == std::string::npos);        // defaults are not written

    Keymap reread;
    reread.load(saved + "no.such.command = A\ntool.eraser = Ctrl+Bogus\nanim.next = , | Ctrl+.\n");
    CHECK(reread.chordsFor("tool.pencil")[0].key == "N");
    CHECK(reread.chordsFor("tool.eraser").empty());               // unparsed: no key, not a crash
    CHECK(reread.chordsFor("anim.next").size() == 2 &&
          reread.chordsFor("anim.next")[0].key == "," && reread.chordsFor("anim.next")[1].ctrl);

    // Set back to the default, it stops being an override.
    Chord b;
    parseChord("B", &b);
    keys.setChords("tool.pencil", { b });
    CHECK(keys.isDefault("tool.pencil"));

    Chord z;
    parseChord("Ctrl+Z", &z);
    CHECK(keys.commandsUsing(z) == std::vector<std::string>{ "edit.undo" });
}

void testNoDefaultsCollide() {
    Keymap keys;
    std::set<std::string> seen;
    for (const CommandInfo& info : commandList()) {
        for (const Chord& chord : info.defaults) {
            const std::string text = chordText(chord);
            CHECK(seen.insert(text).second);
            if (!seen.count(text)) {
                std::printf("  collides: %s\n", text.c_str());
            }
        }
    }
}

} // namespace

int main() {
    testChordsReadAndWrite();
    testOverridesAndDefaults();
    testNoDefaultsCollide();
    if (failures == 0) {
        std::printf("keymap: all passed\n");
        return 0;
    }
    std::printf("keymap: %d failure(s)\n", failures);
    return 1;
}
