// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors
#pragma once

// theme_tokens.h — the colours of the interface, as named roles.
//
// Nothing in the window names a colour. It names what the colour is *for*
// (the panel's surface, secondary text, the focus ring, danger) and a theme
// says what each of those is. That is the whole of a theme: a name, whether
// it is light or dark, and one colour per role. Built-in themes are tables in
// theme_tokens.cpp; a person's are text files (`*.lstheme`) that inherit from
// a built-in one and say only what they change.
//
// This is core, not window code, on purpose: the colours are data, and what
// has to hold about them -- that text can be read on what it is drawn on, in
// every theme, including one somebody typed -- is a number, so it is tested
// here without a window. Pract adds roles and themes of its own on the same
// types.
//
// **Readability is checked, not hoped for.** checkTheme measures the pairs
// that matter (WCAG contrast ratios) against targets: 4.5 for text and 3 for
// indicators in every theme, 7 and 4.5 in the high-contrast ones. A built-in
// theme that misses is a failing test; a custom one is reported, never refused.

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace fast {

struct Rgba {
    uint8_t r = 0, g = 0, b = 0, a = 255;
    bool operator==(const Rgba& o) const { return r == o.r && g == o.g && b == o.b && a == o.a; }
    bool operator!=(const Rgba& o) const { return !(*this == o); }
};

// Every role. The order is the order the files and the editor list them in.
enum class Token : int {
    // Surfaces, from the window outward.
    App, Panel, Raised, Inset, Canvas,
    // Text, strongest first.
    TextStrong, TextPrimary, TextSecondary, TextMuted, TextDisabled, TextOnAccent,
    // Lines.
    Separator, Border, Focus,
    // Controls at rest, hovered, pressed; and what is selected.
    Control, ControlHover, ControlPressed, Selected, SelectedText,
    // The accent and its states. AccentText is the accent as text (a header
    // in it must be readable); Accent is the accent as a fill or an indicator.
    Accent, AccentText, AccentHover, AccentPressed, AccentSubtle,
    // State, as text or icon, and each as a tint behind a message.
    Success, Warning, Danger, Info,
    SuccessSubtle, WarningSubtle, DangerSubtle, InfoSubtle,
    // What is drawn over the work.
    CheckerA, CheckerB, GridLine, SelectionLine, Guide, OnionPast, OnionFuture,
    Count
};

constexpr size_t kTokenCount = static_cast<size_t>(Token::Count);

// "textPrimary": the name a theme file uses.
const char* tokenName(Token token);
bool tokenFromName(const std::string& name, Token* out);

struct Theme {
    std::string id;                   // "slate": a file name, and what Preferences keeps
    std::string name;                 // "Slate"
    bool        dark = true;
    bool        enhanced = false;     // held to the high-contrast targets
    std::array<Rgba, kTokenCount> colour {};

    const Rgba& operator[](Token token) const { return colour[static_cast<size_t>(token)]; }
    Rgba&       operator[](Token token)       { return colour[static_cast<size_t>(token)]; }
};

// The themes that ship, in the order the chooser lists them: dark first.
const std::vector<Theme>& builtinThemes();
const Theme* findBuiltinTheme(const std::string& id);

// --- colour arithmetic ---------------------------------------------------------

// `fg` drawn over `bg`, by its own alpha. The result is opaque.
Rgba composite(Rgba fg, Rgba bg);
// Between `a` and `b`; 0 is `a`, 1 is `b`.
Rgba mix(Rgba a, Rgba b, float t);
// WCAG relative luminance, 0 (black) to 1 (white).
double luminance(Rgba colour);
// WCAG contrast ratio of `fg` on `bg`, 1 to 21. `fg` is composited over `bg`
// first, so a translucent colour is measured as it will look.
double contrast(Rgba fg, Rgba bg);
// "#rrggbb" or "#rrggbbaa" (also without the #), and back.
bool parseColour(const std::string& text, Rgba* out);
std::string colourText(Rgba colour);

// --- describing the roles (for the editor) ---------------------------------------

// What a role is called for a person ("Muted text") and what it is grouped
// under ("Text"). Every role has both.
const char* tokenLabel(Token token);
const char* tokenGroup(Token token);

// --- accents ---------------------------------------------------------------------

struct AccentPreset {
    const char* name;
    Rgba        colour;
};
// The accents on offer beside a free picker. Amber is first: the default.
const std::vector<AccentPreset>& accentPresets();

// `colour` moved toward black or white (whichever the background asks for)
// just far enough that it reads on `background` at `target`:1. Unchanged if it
// already does.
Rgba adjustForContrast(Rgba colour, Rgba background, double target);

// Makes `accent` the theme's accent: its other roles are derived, and the
// accent is first nudged where it must be, so that it is seen on the window and
// the panels and, as text, readable on them -- a pale accent on a light theme
// is darkened to a text-grade one rather than left unreadable.
void applyAccent(Theme* theme, Rgba accent);

// Moves each text and indicator role that misses its contrast target to the
// nearest colour that meets it (the surfaces are left alone: those are the
// look). Returns how many roles it changed.
int fixContrast(Theme* theme);

// --- derived roles --------------------------------------------------------------

// Fills the accent's other roles from `Accent` and the theme's light or dark:
// hover and pressed a step lighter or darker, a translucent subtle tint, the
// focus ring, the accent as text, and the text that goes on it (dark or white,
// whichever reads better).
void deriveAccentRoles(Theme* theme);
// Each state colour's subtle tint.
void deriveStateTints(Theme* theme);

// --- files -----------------------------------------------------------------------

// The id a theme of this name has: lower-case letters, digits and single
// dashes, at most 48; "custom" when nothing is left. It is a file name and
// what Preferences keeps, so it is safe as both.
std::string themeIdFromName(const std::string& name);

struct ThemeReport {
    int  unknownKeys = 0;       // lines naming no role: skipped
    int  badColours = 0;        // a colour that did not parse: that role keeps what it had
    int  lines = 0;
    std::string error;          // set when the text is not a theme file at all
};

// Reads a theme file. It is untrusted: bounded, and anything it does not
// understand is skipped and counted rather than trusted or failed on. A file
// names a `base` (a built-in id; slate or paper when it is missing) and sets
// what it changes; a changed accent derives the accent's other roles unless
// the file sets them too. False only when it is not a theme file at all.
bool parseTheme(const std::string& text, Theme* out, ThemeReport* report);

// The whole theme as text, every role, so a file is complete and readable on
// its own and survives a change of the built-ins.
std::string serializeTheme(const Theme& theme);

// --- checking ---------------------------------------------------------------------

struct ContrastIssue {
    std::string what;           // "textMuted on panel"
    double      ratio = 0.0;
    double      required = 0.0;
};

// Every pair that matters and does not reach its target. Empty is a theme that
// can be read. The targets are higher for `enhanced` themes.
std::vector<ContrastIssue> checkTheme(const Theme& theme);

} // namespace fast
