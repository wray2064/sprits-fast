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
#include <map>
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
    // The roles a program built on Fast adds (see ExtraRole), by id, where this
    // theme says what they are. A role it does not mention has the colour its
    // owner gave for a theme of this polarity.
    std::map<std::string, Rgba> extra;

    const Rgba& operator[](Token token) const { return colour[static_cast<size_t>(token)]; }
    Rgba&       operator[](Token token)       { return colour[static_cast<size_t>(token)]; }

    // An extra role as this theme has it: set outright, or the registered
    // default; an id nobody registered is magenta, so that a misspelt one is seen.
    Rgba extraColour(const std::string& roleId) const;
};

// The themes that ship, in the order the chooser lists them: dark first. A
// program built on Fast may add its own (addBuiltinThemes) before anything
// reads them; Fast's come first.
const std::vector<Theme>& builtinThemes();
const Theme* findBuiltinTheme(const std::string& id);

// --- what a program built on Fast adds ----------------------------------------------
//
// Pract draws things Fast does not have -- timeline tracks, reference tints --
// and they must not be literals in its code: they are roles like Fast's, named
// and themable, so one theme file styles both programs and the Appearance tab
// lists them. A program registers them once at start (AppExtension::roles),
// and a theme that does not mention one has the default given here for a dark
// or a light theme. Nothing is patched in Fast: it only reads the registry.

struct ExtraRole {
    std::string id;           // "pract.track": the owner, a dot, a name -- see validExtraId
    std::string label;        // "Timeline track", for a person
    std::string group;        // "Pract": what the editor lists it under
    Rgba        dark;         // in a theme that is dark and does not say
    Rgba        light;        // ... and in one that is light
    bool        alpha = false;                  // has an opacity worth editing
    // How it is read, against a surface of Fast's: 0 for no check. A track
    // colour is an indicator (3 and 4.5), a label colour text (4.5 and 7).
    Token       against = Token::Panel;
    double      standard = 0.0;
    double      enhanced = 0.0;
};

// Lower-case letters, digits and dashes, one dot in the middle: the part before
// is the program, the part after the role. At most 48 characters, because it
// is a key in a file.
bool validExtraId(const std::string& id);

// Registers roles. A role whose id is not valid or is already taken is skipped;
// at most 64 in all. Returns how many were taken. Call before themes are read.
int addExtraRoles(const std::vector<ExtraRole>& roles);
const std::vector<ExtraRole>& extraRoles();
const ExtraRole* findExtraRole(const std::string& id);
// For a test that registers its own and wants a clean registry after.
void clearExtraRoles();

// Adds built-in themes, each a `.lstheme` text (the same files a person writes,
// starting from one of Fast's with `base =`). A theme that does not parse, whose
// id is taken, or that would make more than 16 added, is skipped; the rest are
// kept in the dark-then-light order of the chooser. Returns how many were taken.
// Call before anything holds a pointer into builtinThemes().
int addBuiltinThemes(const std::vector<std::string>& themeFiles);

// Whether two themes are the same to the eye: every role of Fast's, and every
// extra one registered or set, resolved.
bool sameColours(const Theme& a, const Theme& b);

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
