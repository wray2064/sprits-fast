// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors
#pragma once

// preferences.h — how a person likes Fast to start.
//
// A handful of choices that belong to the person rather than to any one
// document: what a new document is, how often autosave writes, whether the
// pixel grid shows, how long the history is. Kept in the settings folder as
// "name = value" lines. The file is read as untrusted like every other:
// unknown names are ignored, and every value is clamped into range rather
// than trusted, so a hand-edited file can make Fast odd but never broken.

#include <cstdint>
#include <string>

namespace fast {

struct Preferences {
    uint32_t newWidth = 32;
    uint32_t newHeight = 32;
    int      newBackground = 0;       // 0 transparent, 1 white, 2 black, 3 first colour
    int      newPreset = -1;          // -1 the starter palette
    bool     autosaveOn = true;
    uint32_t autosaveSeconds = 120;
    bool     pixelGrid = true;
    uint32_t historyLimit = 200;
    // How the canvas looks under and over the artwork: the transparency
    // chequer's two colours and square size in screen pixels, and the pixel
    // grid's colour and opacity. Colours are 0xRRGGBB.
    uint32_t checkerLight = 0x38404A;
    uint32_t checkerDark = 0x2C333C;
    int      checkerSize = 8;
    uint32_t gridColour = 0xFFFFFF;
    int      gridOpacity = 16;
    bool     lightTheme = false;
    std::string language;             // a catalogue's name, "es"; empty is English
};

// The canvas colours that suit each theme, for when the theme changes and
// the person had not chosen their own: a chequer and grid made for slate
// vanish on paper.
void canvasDefaultsFor(bool lightTheme, Preferences* preferences);

std::string savePreferences(const Preferences& preferences);
Preferences loadPreferences(const std::string& text);

// The files, in the settings folder. Empty when there is no settings folder.
std::string preferencesPath();
std::string keymapPath();

} // namespace fast
