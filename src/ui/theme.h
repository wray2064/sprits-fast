// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors
#pragma once

// theme.h — what the program looks like, in one place.
//
// Dear ImGui's defaults are built for debug overlays, and they look like it. The
// job here is not decoration: it is to make an editor that a person can work in
// for an afternoon without friction, which mostly means restraint.
//
// Three rules the palette below follows.
//
// **The interface must not compete with the artwork.** Everything here is a low
// saturation neutral. The only saturated colour in the whole interface is the
// single accent used for selection and focus, and the artwork itself. A blue
// panel next to a blue sprite makes the sprite harder to judge, and judging the
// sprite is the entire purpose of the window.
//
// **Contrast where it carries meaning, nowhere else.** Section labels are dim,
// values are bright. Borders are barely visible because they separate rather
// than announce. What is bright is what has been drawn.
//
// **Nothing moves that does not have to.** No animated transitions, no hover
// glow that redraws half the panel. A tool that flickers while you work is a
// tool you stop trusting.

#include <imgui.h>

#include <cstdint>

namespace fast {
namespace theme {

// The palette. Named for what they are for rather than what colour they are, so
// that changing one does not require finding every use of "grey38".
struct Palette {
    ImVec4 windowBackground;
    ImVec4 panelBackground;
    ImVec4 canvasBackground;
    ImVec4 control;             // the resting state of an input
    ImVec4 controlHovered;
    ImVec4 controlActive;
    ImVec4 border;
    ImVec4 text;
    ImVec4 textDim;             // labels, units, things read second
    ImVec4 textBright;          // values, things read first
    ImVec4 accent;              // selection and focus, the only saturated colour
    ImVec4 accentDim;
    ImVec4 danger;              // destructive actions and failures
    ImVec4 checkerLight;        // the transparency checkerboard
    ImVec4 checkerDark;
};

const Palette& palette();

// Spacing, in pixels, so panels line up without magic numbers scattered about.
struct Metrics {
    float rowHeight     = 24.f;
    float panelPadding  = 10.f;
    float itemSpacing   = 6.f;
    float sectionGap    = 14.f;
    float sidebarWidth  = 248.f;
    float toolbarHeight = 40.f;
    float rounding      = 3.f;
};

const Metrics& metrics();

// Applies the palette and the spacing to the current ImGui context. Called once
// after the context is created.
void apply();

// Loads a readable interface font.
//
// ImGui's built-in font is a 13px bitmap face designed for debug output, and it
// is most of why an ImGui program looks like a debug overlay. This tries the
// system's own interface font first and falls back to the built-in one, so the
// program still runs on a machine where nothing is found.
void loadFonts(float scale);

// ---------------------------------------------------------------- widgets --
//
// Small pieces used in more than one panel. They exist so the panels read as
// what they do rather than as a wall of ImGui calls, and so a change of look
// happens in one place.

// A dim, letter-spaced label above a group of controls.
void sectionHeader(const char* label);

// A toolbar button: fixed size, selected state, and a tooltip carrying the
// shortcut. Returns true when clicked.
bool toolButton(const char* glyph, const char* name, const char* shortcutHint,
                bool selected, const char* description = nullptr);

// A colour swatch. `selected` draws the ring that marks the active colour.
bool swatch(const char* id, ImU32 colour, bool selected, float size = 20.f);

// A label and a value on one line, the label dim and the value bright. Used by
// the status bar, where scanning matters more than reading.
void statusItem(const char* label, const char* value, bool bright = true);

// A right-aligned help marker that explains something without taking a row.
void hint(const char* text);

} // namespace theme
} // namespace fast
