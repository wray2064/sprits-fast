// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors
#pragma once

// theme.h — what the program looks like, in one place.
//
// Dear ImGui's defaults are built for debug overlays, and they look like it. The
// job here is not decoration: it is to make an editor that a person can work in
// for an afternoon without friction, which mostly means restraint.
//
// The look is Source-era Valve: a cold, desaturated slate shell with one hot
// amber running through it. They work together because they are opposites --
// the slate is cool and almost colourless, so the amber reads as light rather
// than as another surface.
//
// Three rules the palette below follows.
//
// **The interface must not compete with the artwork.** Every surface here is a
// low-saturation slate. The accent appears in exactly two roles: selection and
// focus, and the section labels that give a panel its structure. Nowhere else.
// A blue panel next to a blue sprite makes the sprite harder to judge, and
// judging the sprite is the entire purpose of the window.
//
// **Contrast where it carries meaning, nowhere else.** Section labels are dim,
// values are bright. Borders are barely visible because they separate rather
// than announce. What is bright is what has been drawn.
//
// **Nothing moves that does not have to.** No animated transitions, no hover
// glow that redraws half the panel. A tool that flickers while you work is a
// tool you stop trusting.

#include "app/theme_tokens.h"

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

    // The roles of app/theme_tokens.h that the panels above did not have names
    // for. New and rebuilt panels use these; the ones above stay as aliases
    // until the last panel has moved.
    ImVec4 raised, inset;
    ImVec4 textMuted, textDisabled, textOnAccent;
    ImVec4 separator, focus;
    ImVec4 selected, selectedText;
    ImVec4 accentText, accentHover, accentPressed;
    ImVec4 success, warning, info;
    ImVec4 successSubtle, warningSubtle, dangerSubtle, infoSubtle;
    ImVec4 gridLine, selectionLine, guide, onionPast, onionFuture;
};

const Palette& palette();

// A role's colour as ImGui draws with it.
ImVec4 toImVec4(Rgba colour);

// The theme in force: its colours are what palette() says.
const Theme& current();
// A role a program built on Fast registered (AppExtension::roles), as the theme
// in force has it: what its windows draw with instead of a literal colour. An
// id that was never registered is magenta.
ImVec4 extra(const char* id);
ImU32 extraU32(const char* id);
// Puts a theme in force and re-applies the ImGui style. The built-in ones are
// in app/theme_tokens.h; a person's come from `*.lstheme` files.
void setTheme(const Theme& theme);

// Slate or Paper, for callers that only know light and dark.
void setLight(bool light);

// Spacing, in pixels, so panels line up without magic numbers scattered about.
// These are the pixels as drawn: the density's steps already multiplied by the
// person's interface size, so a panel may add them to a width ImGui scaled.
struct Metrics {
    float rowHeight     = 24.f;
    float panelPadding  = 10.f;
    float itemSpacing   = 6.f;
    float sectionGap    = 14.f;
    float sidebarWidth  = 276.f;
    float toolbarHeight = 40.f;
    float rounding      = 3.f;
};

const Metrics& metrics();

// How much air there is: the spacing and the height of a control, never the
// type. Comfortable is the default; Compact fits more on a small screen.
enum class Density { Compact = 0, Comfortable = 1, Spacious = 2 };
void setDensity(Density density);
Density density();

// --- type -----------------------------------------------------------------------
//
// One family (Inter) in two weights, and five styles drawn from them. A panel
// asks for a style, not a size: pushText(TextStyle::Title) ... popText().
// Body is what every widget is drawn in unless told otherwise.
struct TypeScale {
    float caption = 12.f;       // units, hints, the status bar
    float body = 14.f;          // labels and values
    float title = 17.f;         // a dialog's or a panel's title
    float display = 22.f;       // the program's name on the start screen
};
enum class TextStyle { Caption, Label, Body, Strong, Title, Display };
void pushText(TextStyle style);
void popText();
const TypeScale& type();

// Applies the palette and the spacing to the current ImGui context. Called once
// after the context is created.
void apply();

// The person's interface scale: 1 is as drawn, 1.5 is half as big again --
// text and every spacing, never the canvas. Applied at once, and kept by
// every later apply() (a change of theme, say).
void setScale(float scale);
float scale();
// A pixel count written for 100%, as it is at the person's interface size. Every
// literal width or height a panel draws with goes through this, or comes from
// the font (CalcTextSize, GetFrameHeight): a bare number is right at 100% and
// clips at 150% (docs/visual-language.md, rule 3).
float px(float at100);

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
// A section header that folds its section away when clicked: the same muted
// label and hairline, with an arrow. Open until folded. True while open.
bool foldingHeader(const char* label);

// ------------------------------------------------------------------ icons --
//
// Vector icons: Lucide's SVGs (assets/icons/lucide, ISC), compiled in and parsed
// at first use into strokes (app/vector_icon), then drawn as round-capped lines
// at any size and in any colour. Never a bitmap or an icon font, so an icon is
// as sharp at 200% as at 100% and takes the accent colour when selected.
// Everything that draws an icon does it through the one drawIcon() call.

enum class Icon { Pencil, Eraser, Bucket, Dropper, Rectangle, Ellipse, Line, Eye, EyeShut,
                  Marquee, EllipseMarquee, Lasso, Wand, Move,
                  Spray, Contour, Hand, Zoom, Gradient, Text, PolygonLasso,
                  Polygon, Curve, Slice };

// Draws `icon` inside a square of `size` at `at`, in `colour`.
void drawIcon(ImDrawList* draw, Icon icon, ImVec2 at, float size, ImU32 colour);

// A toolbar button: fixed size, selected state, and a tooltip carrying the
// shortcut. Returns true when clicked.
bool toolButton(Icon icon, const char* name, const char* shortcutHint,
                bool selected, const char* description = nullptr);

// A visibility toggle: an open eye when `visible`, a shut one otherwise, dim.
// Small, because in a stack it is the thumbnail that identifies a row and
// the eye only has to be findable. Returns true when clicked.
bool eyeToggle(const char* id, bool visible, float size = 16.f);

// A colour swatch. `selected` draws the ring that marks the active colour.
bool swatch(const char* id, ImU32 colour, bool selected, float size = 20.f);

// The side of a tool button, which follows the font and the density, and the
// width of the rail they sit in. A thin line between groups of tools.
float toolSide();
float toolRailWidth();
void railDivider();

// A right-aligned help marker that explains something without taking a row.
void hint(const char* text);
// The room a hint takes beside a control (for `SetNextItemWidth(-hintWidth())`),
// so the control and the mark share a row without the mark falling off it.
float hintWidth();

} // namespace theme
} // namespace fast
