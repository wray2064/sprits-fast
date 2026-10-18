// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

#include "app/theme_tokens.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <utility>

namespace fast {

namespace {

constexpr Rgba c(int r, int g, int b, int a = 255) {
    return Rgba{ static_cast<uint8_t>(r), static_cast<uint8_t>(g), static_cast<uint8_t>(b),
                 static_cast<uint8_t>(a) };
}

struct TokenName {
    Token       token;
    const char* name;
};

// The names files use, in enum order.
const TokenName kNames[] = {
    { Token::App, "app" }, { Token::Panel, "panel" }, { Token::Raised, "raised" },
    { Token::Inset, "inset" }, { Token::Canvas, "canvas" },
    { Token::TextStrong, "textStrong" }, { Token::TextPrimary, "textPrimary" },
    { Token::TextSecondary, "textSecondary" }, { Token::TextMuted, "textMuted" },
    { Token::TextDisabled, "textDisabled" }, { Token::TextOnAccent, "textOnAccent" },
    { Token::Separator, "separator" }, { Token::Border, "border" }, { Token::Focus, "focus" },
    { Token::Control, "control" }, { Token::ControlHover, "controlHover" },
    { Token::ControlPressed, "controlPressed" }, { Token::Selected, "selected" },
    { Token::SelectedText, "selectedText" },
    { Token::Accent, "accent" }, { Token::AccentText, "accentText" },
    { Token::AccentHover, "accentHover" }, { Token::AccentPressed, "accentPressed" },
    { Token::AccentSubtle, "accentSubtle" },
    { Token::Success, "success" }, { Token::Warning, "warning" }, { Token::Danger, "danger" },
    { Token::Info, "info" },
    { Token::SuccessSubtle, "successSubtle" }, { Token::WarningSubtle, "warningSubtle" },
    { Token::DangerSubtle, "dangerSubtle" }, { Token::InfoSubtle, "infoSubtle" },
    { Token::CheckerA, "checkerA" }, { Token::CheckerB, "checkerB" },
    { Token::GridLine, "gridLine" }, { Token::SelectionLine, "selectionLine" },
    { Token::Guide, "guide" }, { Token::OnionPast, "onionPast" },
    { Token::OnionFuture, "onionFuture" },
};
static_assert(sizeof(kNames) / sizeof(kNames[0]) == kTokenCount,
              "every token has a name, in order");

// What a theme says outright. The roles that follow from the accent, and the
// state tints, are derived from these, so a theme is a short table.
struct Seed {
    const char* id;
    const char* name;
    bool        dark;
    bool        enhanced;
    Rgba app, panel, raised, inset, canvas;
    Rgba textStrong, textPrimary, textSecondary, textMuted, textDisabled;
    Rgba separator, border;
    Rgba control, hover, pressed;
    Rgba selected, selectedText;
    Rgba accent, accentText;
    Rgba success, warning, danger, info;
    Rgba checkerA, checkerB;
    Rgba gridLine, selectionLine, guide, onionPast, onionFuture;
};

Theme build(const Seed& s) {
    Theme t;
    t.id = s.id;
    t.name = s.name;
    t.dark = s.dark;
    t.enhanced = s.enhanced;
    t[Token::App] = s.app;
    t[Token::Panel] = s.panel;
    t[Token::Raised] = s.raised;
    t[Token::Inset] = s.inset;
    t[Token::Canvas] = s.canvas;
    t[Token::TextStrong] = s.textStrong;
    t[Token::TextPrimary] = s.textPrimary;
    t[Token::TextSecondary] = s.textSecondary;
    t[Token::TextMuted] = s.textMuted;
    t[Token::TextDisabled] = s.textDisabled;
    t[Token::Separator] = s.separator;
    t[Token::Border] = s.border;
    t[Token::Control] = s.control;
    t[Token::ControlHover] = s.hover;
    t[Token::ControlPressed] = s.pressed;
    t[Token::Selected] = s.selected;
    t[Token::SelectedText] = s.selectedText;
    t[Token::Accent] = s.accent;
    t[Token::Success] = s.success;
    t[Token::Warning] = s.warning;
    t[Token::Danger] = s.danger;
    t[Token::Info] = s.info;
    t[Token::CheckerA] = s.checkerA;
    t[Token::CheckerB] = s.checkerB;
    t[Token::GridLine] = s.gridLine;
    t[Token::SelectionLine] = s.selectionLine;
    t[Token::Guide] = s.guide;
    t[Token::OnionPast] = s.onionPast;
    t[Token::OnionFuture] = s.onionFuture;
    deriveAccentRoles(&t);
    deriveStateTints(&t);
    // A light theme's accent as a fill is lighter than the accent as text.
    t[Token::AccentText] = s.accentText;
    return t;
}

// ---------------------------------------------------------------------- themes --
//
// Slate and Paper are the two the program began with, kept as they were where
// they read well and moved only where a measured pair missed its target. The
// rest follow the same arrangement: the chrome is quiet and low in chroma, one
// accent runs through it, and what is drawn on the canvas is the brightest
// thing on screen.

std::vector<Theme> makeThemes() {
    std::vector<Theme> themes;

    // Slate: cold desaturated blue-grey with amber. The greys are tinted a few
    // points only, so they read as slate and do not argue with the canvas.
    themes.push_back(build(Seed{
        "slate", "Slate", true, false,
        c(20, 24, 29), c(29, 34, 41), c(37, 43, 52), c(22, 26, 32), c(14, 17, 21),
        c(232, 240, 248), c(200, 211, 222), c(160, 174, 189), c(134, 149, 165), c(100, 112, 126),
        c(42, 50, 60), c(52, 62, 74),
        c(43, 51, 61), c(57, 68, 81), c(70, 83, 98),
        c(86, 62, 34), c(250, 240, 226),
        c(255, 158, 38), c(255, 158, 38),
        c(92, 196, 124), c(236, 194, 74), c(244, 116, 100), c(100, 174, 244),
        c(56, 64, 74), c(44, 51, 60),
        c(255, 255, 255, 26), c(255, 255, 255), c(255, 96, 96, 210), c(255, 90, 90, 92), c(90, 160, 255, 92) }));

    // Graphite: neutral grey with no blue cast, for people who want the chrome
    // to take no colour at all. Its accent is blue.
    themes.push_back(build(Seed{
        "graphite", "Graphite", true, false,
        c(24, 24, 24), c(36, 36, 36), c(46, 46, 46), c(28, 28, 28), c(18, 18, 18),
        c(240, 240, 240), c(212, 212, 212), c(174, 174, 174), c(152, 152, 152), c(114, 114, 114),
        c(50, 50, 50), c(66, 66, 66),
        c(54, 54, 54), c(68, 68, 68), c(82, 82, 82),
        c(38, 82, 140), c(240, 246, 255),
        c(86, 156, 255), c(86, 156, 255),
        c(92, 196, 124), c(236, 194, 74), c(244, 116, 100), c(100, 174, 244),
        c(62, 62, 62), c(50, 50, 50),
        c(255, 255, 255, 26), c(255, 255, 255), c(255, 96, 96, 210), c(255, 90, 90, 92), c(90, 160, 255, 92) }));

    // Midnight: a deeper blue-black that lets the canvas glow.
    themes.push_back(build(Seed{
        "midnight", "Midnight", true, false,
        c(6, 8, 13), c(13, 16, 24), c(22, 26, 38), c(9, 11, 17), c(3, 4, 7),
        c(234, 240, 250), c(200, 210, 226), c(158, 172, 196), c(130, 146, 174), c(96, 108, 130),
        c(28, 34, 48), c(40, 48, 68),
        c(30, 36, 52), c(42, 50, 70), c(56, 66, 90),
        c(78, 58, 32), c(250, 240, 226),
        c(255, 158, 38), c(255, 158, 38),
        c(92, 196, 124), c(236, 194, 74), c(244, 116, 100), c(100, 174, 244),
        c(34, 40, 56), c(24, 29, 42),
        c(255, 255, 255, 26), c(255, 255, 255), c(255, 96, 96, 210), c(255, 90, 90, 92), c(90, 160, 255, 92) }));

    // OLED: the app and the canvas are true black, so a pixel that is not
    // lit is off and the panel edge is the only seam. Panels are a step above
    // black, nothing large is bright (no pure-white text, no wide light
    // fills), and the accent stays small. Light is rationed to what matters.
    themes.push_back(build(Seed{
        "oled", "OLED", true, false,
        c(0, 0, 0), c(8, 8, 10), c(20, 20, 24), c(3, 3, 4), c(0, 0, 0),
        c(236, 236, 242), c(204, 204, 212), c(164, 164, 176), c(136, 136, 150), c(98, 98, 108),
        c(30, 30, 36), c(46, 46, 56),
        c(32, 32, 38), c(46, 46, 54), c(60, 60, 70),
        c(70, 50, 26), c(248, 238, 224),
        c(255, 158, 38), c(255, 158, 38),
        c(92, 196, 124), c(236, 194, 74), c(244, 116, 100), c(100, 174, 244),
        c(30, 30, 36), c(18, 18, 22),
        c(255, 255, 255, 26), c(255, 255, 255), c(255, 96, 96, 210), c(255, 90, 90, 92), c(90, 160, 255, 92) }));

    // High contrast dark: black, white and one yellow. Every pair clears the
    // enhanced targets, and the borders are drawn, not implied.
    themes.push_back(build(Seed{
        "high-contrast-dark", "High contrast dark", true, true,
        c(0, 0, 0), c(10, 10, 10), c(22, 22, 22), c(0, 0, 0), c(0, 0, 0),
        c(255, 255, 255), c(255, 255, 255), c(236, 236, 236), c(214, 214, 214), c(170, 170, 170),
        c(160, 160, 160), c(255, 255, 255),
        c(52, 52, 52), c(78, 78, 78), c(108, 108, 108),
        c(20, 70, 150), c(255, 255, 255),
        c(255, 214, 10), c(255, 214, 10),
        c(96, 255, 140), c(255, 214, 10), c(255, 132, 120), c(120, 210, 255),
        c(70, 70, 70), c(40, 40, 40),
        c(255, 255, 255, 64), c(255, 255, 255), c(255, 120, 110, 255), c(255, 110, 110, 128), c(110, 190, 255, 128) }));

    // Paper: the same arrangement turned over for a bright room. The amber is
    // darkened so that it still reads as the one colour on a light ground.
    themes.push_back(build(Seed{
        "paper", "Paper", false, false,
        c(214, 219, 225), c(233, 236, 240), c(246, 248, 250), c(223, 227, 233), c(196, 202, 210),
        c(8, 12, 16), c(38, 44, 52), c(76, 86, 98), c(92, 102, 114), c(124, 134, 146),
        c(204, 211, 220), c(168, 177, 189),
        c(216, 221, 228), c(202, 209, 218), c(188, 197, 208),
        c(238, 196, 134), c(30, 20, 8),
        c(170, 86, 0), c(148, 74, 0),
        c(18, 110, 52), c(138, 92, 0), c(176, 40, 30), c(20, 88, 168),
        c(238, 240, 243), c(214, 218, 224),
        c(0, 0, 0, 30), c(0, 0, 0), c(210, 40, 40, 220), c(220, 60, 60, 96), c(40, 110, 230, 96) }));

    // Warm paper: a soft sepia light theme, gentler than white for long
    // sessions.
    themes.push_back(build(Seed{
        "warm-paper", "Warm paper", false, false,
        c(226, 216, 200), c(243, 235, 222), c(250, 245, 236), c(234, 225, 210), c(214, 203, 185),
        c(30, 22, 12), c(52, 42, 30), c(92, 78, 60), c(108, 94, 74), c(138, 124, 102),
        c(220, 208, 190), c(184, 170, 148),
        c(228, 217, 200), c(214, 202, 183), c(200, 187, 165),
        c(230, 188, 146), c(40, 28, 16),
        c(176, 84, 20), c(160, 70, 10),
        c(22, 104, 50), c(132, 88, 0), c(170, 40, 30), c(24, 84, 160),
        c(246, 240, 228), c(228, 218, 202),
        c(0, 0, 0, 30), c(0, 0, 0), c(200, 40, 40, 220), c(210, 60, 60, 96), c(40, 100, 220, 96) }));

    // Famicom: the cream plastic and deep red of a Family Computer -- warm
    // cream surfaces, a near-black brown for text (the controller's buttons),
    // and one crimson accent. A light theme, with the red kept dark enough to
    // carry white text and to read as text itself. The selected fill is the
    // console's gold lettering turned down to a tint.
    themes.push_back(build(Seed{
        "famicom", "Famicom", false, false,
        c(212, 203, 176), c(236, 229, 208), c(246, 241, 226), c(226, 218, 194), c(190, 182, 158),
        c(34, 22, 20), c(56, 40, 36), c(96, 72, 58), c(112, 88, 72), c(140, 118, 98),
        c(214, 202, 176), c(190, 176, 148),
        c(222, 211, 186), c(208, 196, 168), c(192, 179, 150),
        c(222, 188, 140), c(40, 20, 16),
        c(170, 30, 40), c(170, 30, 40),
        c(22, 104, 50), c(132, 88, 0), c(176, 46, 0), c(24, 84, 160),
        c(240, 234, 214), c(222, 213, 188),
        c(0, 0, 0, 30), c(0, 0, 0), c(170, 30, 40, 220), c(200, 60, 60, 96), c(40, 90, 200, 96) }));

    // High contrast light: white, black and one blue.
    themes.push_back(build(Seed{
        "high-contrast-light", "High contrast light", false, true,
        c(255, 255, 255), c(255, 255, 255), c(255, 255, 255), c(255, 255, 255), c(255, 255, 255),
        c(0, 0, 0), c(0, 0, 0), c(30, 30, 30), c(56, 56, 56), c(104, 104, 104),
        c(110, 110, 110), c(0, 0, 0),
        c(230, 230, 230), c(205, 205, 205), c(176, 176, 176),
        c(190, 215, 255), c(0, 0, 0),
        c(0, 70, 200), c(0, 70, 200),
        c(0, 92, 24), c(110, 64, 0), c(150, 0, 0), c(0, 56, 150),
        c(235, 235, 235), c(210, 210, 210),
        c(0, 0, 0, 90), c(0, 0, 0), c(190, 0, 0, 255), c(200, 0, 0, 128), c(0, 70, 220, 128) }));

    return themes;
}

// ----------------------------------------------------------------- the checks --

// Whichever of two colours is lighter, over the darker: WCAG's ratio for two
// opaque colours, which does not care which is the text.
double ratio(Rgba a, Rgba b) {
    const double la = luminance(a);
    const double lb = luminance(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

struct Pair {
    Token       foreground;
    Token       background;
    double      standard;
    double      enhanced;
    const char* kind;       // "text", "indicator" or "surface": how it is read
};

constexpr double kTextStd = 4.5;
constexpr double kTextEnh = 7.0;

} // namespace

const char* tokenName(Token token) {
    const size_t index = static_cast<size_t>(token);
    return index < kTokenCount ? kNames[index].name : "";
}

bool tokenFromName(const std::string& name, Token* out) {
    for (const TokenName& entry : kNames) {
        if (name == entry.name) {
            if (out != nullptr) {
                *out = entry.token;
            }
            return true;
        }
    }
    return false;
}

const std::vector<Theme>& builtinThemes() {
    static const std::vector<Theme> themes = makeThemes();
    return themes;
}

const Theme* findBuiltinTheme(const std::string& id) {
    for (const Theme& theme : builtinThemes()) {
        if (theme.id == id) {
            return &theme;
        }
    }
    return nullptr;
}

// ------------------------------------------------------------ colour arithmetic --

Rgba composite(Rgba fg, Rgba bg) {
    const float a = static_cast<float>(fg.a) / 255.f;
    const auto channel = [a](uint8_t f, uint8_t b) {
        return static_cast<uint8_t>(std::lround(static_cast<float>(f) * a +
                                                static_cast<float>(b) * (1.f - a)));
    };
    return Rgba{ channel(fg.r, bg.r), channel(fg.g, bg.g), channel(fg.b, bg.b), 255 };
}

Rgba mix(Rgba a, Rgba b, float t) {
    t = std::clamp(t, 0.f, 1.f);
    const auto channel = [t](uint8_t x, uint8_t y) {
        return static_cast<uint8_t>(std::lround(static_cast<float>(x) * (1.f - t) +
                                                static_cast<float>(y) * t));
    };
    return Rgba{ channel(a.r, b.r), channel(a.g, b.g), channel(a.b, b.b), channel(a.a, b.a) };
}

double luminance(Rgba colour) {
    const auto linear = [](uint8_t v) {
        const double s = static_cast<double>(v) / 255.0;
        return s <= 0.03928 ? s / 12.92 : std::pow((s + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * linear(colour.r) + 0.7152 * linear(colour.g) + 0.0722 * linear(colour.b);
}

double contrast(Rgba fg, Rgba bg) {
    return ratio(composite(fg, bg), composite(bg, Rgba{ 0, 0, 0, 255 }));
}

bool parseColour(const std::string& text, Rgba* out) {
    size_t at = 0;
    if (at < text.size() && text[at] == '#') {
        ++at;
    }
    const size_t digits = text.size() - at;
    if (digits != 6 && digits != 8) {
        return false;
    }
    unsigned long value = 0;
    for (size_t i = at; i < text.size(); ++i) {
        const char ch = text[i];
        const int digit = ch >= '0' && ch <= '9' ? ch - '0'
                        : ch >= 'a' && ch <= 'f' ? ch - 'a' + 10
                        : ch >= 'A' && ch <= 'F' ? ch - 'A' + 10 : -1;
        if (digit < 0) {
            return false;
        }
        value = value * 16 + static_cast<unsigned long>(digit);
    }
    if (out != nullptr) {
        if (digits == 6) {
            *out = Rgba{ static_cast<uint8_t>(value >> 16), static_cast<uint8_t>(value >> 8),
                         static_cast<uint8_t>(value), 255 };
        } else {
            *out = Rgba{ static_cast<uint8_t>(value >> 24), static_cast<uint8_t>(value >> 16),
                         static_cast<uint8_t>(value >> 8), static_cast<uint8_t>(value) };
        }
    }
    return true;
}

std::string colourText(Rgba colour) {
    char text[16];
    if (colour.a == 255) {
        std::snprintf(text, sizeof text, "#%02x%02x%02x", colour.r, colour.g, colour.b);
    } else {
        std::snprintf(text, sizeof text, "#%02x%02x%02x%02x", colour.r, colour.g, colour.b,
                      colour.a);
    }
    return text;
}

// ---------------------------------------------------------------- derived roles --

void deriveAccentRoles(Theme* theme) {
    const Rgba accent = (*theme)[Token::Accent];
    const Rgba white = c(255, 255, 255);
    const Rgba black = c(0, 0, 0);
    const bool dark = theme->dark;
    (*theme)[Token::AccentHover] = dark ? mix(accent, white, 0.14f) : mix(accent, black, 0.10f);
    (*theme)[Token::AccentPressed] = dark ? mix(accent, black, 0.14f) : mix(accent, black, 0.20f);
    (*theme)[Token::AccentSubtle] = Rgba{ accent.r, accent.g, accent.b,
                                          static_cast<uint8_t>(dark ? 56 : 48) };
    (*theme)[Token::Focus] = accent;
    (*theme)[Token::AccentText] = accent;
    // The text on the accent is whichever of near-black and white reads better.
    const Rgba ink = c(20, 14, 6);
    (*theme)[Token::TextOnAccent] = ratio(ink, accent) >= ratio(white, accent) ? ink : white;
}

void deriveStateTints(Theme* theme) {
    const std::pair<Token, Token> tints[] = {
        { Token::Success, Token::SuccessSubtle }, { Token::Warning, Token::WarningSubtle },
        { Token::Danger, Token::DangerSubtle },   { Token::Info, Token::InfoSubtle },
    };
    for (const auto& tint : tints) {
        const Rgba base = (*theme)[tint.first];
        (*theme)[tint.second] = Rgba{ base.r, base.g, base.b, 46 };
    }
}

// ------------------------------------------------------------------------ files --

namespace {

constexpr size_t kMaxLines = 512;
constexpr size_t kMaxLineLength = 200;
constexpr size_t kMaxNameLength = 48;

std::string trimmed(const std::string& text) {
    size_t a = 0;
    size_t b = text.size();
    while (a < b && (text[a] == ' ' || text[a] == '\t' || text[a] == '\r')) { ++a; }
    while (b > a && (text[b - 1] == ' ' || text[b - 1] == '\t' || text[b - 1] == '\r')) { --b; }
    return text.substr(a, b - a);
}

// A name that is safe as a file name and as an id: letters, digits, dashes.
std::string idFrom(const std::string& name) {
    std::string id;
    for (char ch : name) {
        if ((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9')) {
            id += ch;
        } else if (ch >= 'A' && ch <= 'Z') {
            id += static_cast<char>(ch - 'A' + 'a');
        } else if (!id.empty() && id.back() != '-') {
            id += '-';
        }
    }
    while (!id.empty() && id.back() == '-') {
        id.pop_back();
    }
    return id.empty() ? std::string("custom") : id.substr(0, kMaxNameLength);
}

} // namespace

bool parseTheme(const std::string& text, Theme* out, ThemeReport* report) {
    ThemeReport local;
    ThemeReport& r = report != nullptr ? *report : local;
    r = ThemeReport{};
    if (out == nullptr) {
        r.error = "nowhere to put the theme";
        return false;
    }
    const std::string header = "lstheme 1";
    if (text.compare(0, header.size(), header) != 0) {
        r.error = "not a theme file";
        return false;
    }

    // First pass: the keys that choose the starting point.
    std::string base;
    std::string name;
    bool dark = true;
    bool darkGiven = false;
    bool enhanced = false;
    bool enhancedGiven = false;
    std::vector<std::pair<std::string, std::string>> lines;
    size_t start = text.find('\n');
    while (start != std::string::npos && start < text.size() && lines.size() < kMaxLines) {
        const size_t from = start + 1;
        size_t end = text.find('\n', from);
        const std::string line = trimmed(text.substr(from, (end == std::string::npos
                                                              ? text.size() : end) - from));
        start = end;
        ++r.lines;
        if (line.empty() || line[0] == '#' || line.size() > kMaxLineLength) {
            continue;
        }
        const size_t equals = line.find('=');
        if (equals == std::string::npos) {
            ++r.unknownKeys;
            continue;
        }
        const std::string key = trimmed(line.substr(0, equals));
        const std::string value = trimmed(line.substr(equals + 1));
        if (key == "base") {
            base = value;
        } else if (key == "name") {
            name = value.substr(0, kMaxNameLength);
        } else if (key == "dark") {
            dark = value != "0";
            darkGiven = true;
        } else if (key == "enhanced") {
            enhanced = value == "1";
            enhancedGiven = true;
        } else {
            lines.emplace_back(key, value);
        }
    }

    const Theme* start_from = findBuiltinTheme(base);
    if (start_from == nullptr) {
        start_from = findBuiltinTheme(darkGiven && !dark ? "paper" : "slate");
    }
    Theme theme = *start_from;
    theme.dark = darkGiven ? dark : start_from->dark;
    theme.enhanced = enhancedGiven ? enhanced : start_from->enhanced;
    theme.name = name.empty() ? std::string("Custom") : name;
    theme.id = idFrom(theme.name);

    std::array<bool, kTokenCount> given {};
    for (const auto& line : lines) {
        Token token;
        if (!tokenFromName(line.first, &token)) {
            ++r.unknownKeys;
            continue;
        }
        Rgba colour;
        if (!parseColour(line.second, &colour)) {
            ++r.badColours;
            continue;
        }
        theme[token] = colour;
        given[static_cast<size_t>(token)] = true;
    }

    // A changed accent brings its other roles with it, unless the file set
    // them: a person who picks an amber should not have to pick its hover too.
    if (given[static_cast<size_t>(Token::Accent)]) {
        Theme derived = theme;
        deriveAccentRoles(&derived);
        for (Token role : { Token::AccentHover, Token::AccentPressed, Token::AccentSubtle,
                            Token::Focus, Token::AccentText, Token::TextOnAccent }) {
            if (!given[static_cast<size_t>(role)]) {
                theme[role] = derived[role];
            }
        }
    }
    for (const auto& pair : std::initializer_list<std::pair<Token, Token>>{
             { Token::Success, Token::SuccessSubtle }, { Token::Warning, Token::WarningSubtle },
             { Token::Danger, Token::DangerSubtle }, { Token::Info, Token::InfoSubtle } }) {
        if (given[static_cast<size_t>(pair.first)] && !given[static_cast<size_t>(pair.second)]) {
            const Rgba base_colour = theme[pair.first];
            theme[pair.second] = Rgba{ base_colour.r, base_colour.g, base_colour.b, 46 };
        }
    }
    *out = theme;
    return true;
}

std::string serializeTheme(const Theme& theme) {
    std::string out = "lstheme 1\n";
    out += "name = " + theme.name + "\n";
    out += std::string("dark = ") + (theme.dark ? "1" : "0") + "\n";
    out += std::string("enhanced = ") + (theme.enhanced ? "1" : "0") + "\n";
    out += std::string("base = ") + (theme.dark ? "slate" : "paper") + "\n";
    for (const TokenName& entry : kNames) {
        out += std::string(entry.name) + " = " + colourText(theme[entry.token]) + "\n";
    }
    return out;
}

// ------------------------------------------------------------------ the checks --

std::vector<ContrastIssue> checkTheme(const Theme& theme) {
    // How each pair is read decides its target. Text must be readable;
    // indicators (the focus ring, the accent as a mark) must be seen; the
    // surfaces of controls must be told apart, which needs far less.
    static const Pair kPairs[] = {
        // Text on the surfaces it is drawn on.
        { Token::TextStrong, Token::App, kTextStd, kTextEnh, "text" },
        { Token::TextStrong, Token::Panel, kTextStd, kTextEnh, "text" },
        { Token::TextStrong, Token::Raised, kTextStd, kTextEnh, "text" },
        { Token::TextStrong, Token::Inset, kTextStd, kTextEnh, "text" },
        { Token::TextStrong, Token::Control, kTextStd, kTextEnh, "text" },
        { Token::TextPrimary, Token::App, kTextStd, kTextEnh, "text" },
        { Token::TextPrimary, Token::Panel, kTextStd, kTextEnh, "text" },
        { Token::TextPrimary, Token::Raised, kTextStd, kTextEnh, "text" },
        { Token::TextPrimary, Token::Inset, kTextStd, kTextEnh, "text" },
        { Token::TextPrimary, Token::Control, kTextStd, kTextEnh, "text" },
        { Token::TextPrimary, Token::ControlHover, kTextStd, kTextEnh, "text" },
        { Token::TextSecondary, Token::Panel, kTextStd, kTextEnh, "text" },
        { Token::TextSecondary, Token::Raised, kTextStd, kTextEnh, "text" },
        { Token::TextSecondary, Token::Control, kTextStd, kTextEnh, "text" },
        { Token::TextMuted, Token::Panel, kTextStd, kTextEnh, "text" },
        { Token::TextMuted, Token::Raised, kTextStd, kTextEnh, "text" },
        { Token::TextDisabled, Token::Panel, 3.0, 4.5, "text" },
        { Token::TextOnAccent, Token::Accent, kTextStd, kTextEnh, "text" },
        { Token::TextOnAccent, Token::AccentHover, kTextStd, kTextEnh, "text" },
        { Token::TextOnAccent, Token::AccentPressed, kTextStd, kTextEnh, "text" },
        { Token::SelectedText, Token::Selected, kTextStd, kTextEnh, "text" },
        // A selected row keeps the ordinary text colour (ImGui draws it so).
        { Token::TextPrimary, Token::Selected, kTextStd, kTextEnh, "text" },
        { Token::AccentText, Token::Panel, kTextStd, kTextEnh, "text" },
        { Token::AccentText, Token::Raised, kTextStd, kTextEnh, "text" },
        { Token::Success, Token::Panel, kTextStd, kTextEnh, "text" },
        { Token::Success, Token::Raised, kTextStd, kTextEnh, "text" },
        { Token::Warning, Token::Panel, kTextStd, kTextEnh, "text" },
        { Token::Warning, Token::Raised, kTextStd, kTextEnh, "text" },
        { Token::Danger, Token::Panel, kTextStd, kTextEnh, "text" },
        { Token::Danger, Token::Raised, kTextStd, kTextEnh, "text" },
        { Token::Info, Token::Panel, kTextStd, kTextEnh, "text" },
        { Token::Info, Token::Raised, kTextStd, kTextEnh, "text" },
        // Marks that must be seen.
        { Token::Focus, Token::Panel, 3.0, 4.5, "indicator" },
        { Token::Focus, Token::Control, 3.0, 4.5, "indicator" },
        { Token::Accent, Token::Panel, 3.0, 4.5, "indicator" },
        // Surfaces that must be told apart.
        { Token::Control, Token::Panel, 1.15, 1.15, "surface" },
        { Token::ControlHover, Token::Control, 1.1, 1.1, "surface" },
        { Token::ControlPressed, Token::ControlHover, 1.1, 1.1, "surface" },
        { Token::Selected, Token::Panel, 1.3, 1.3, "surface" },
        { Token::Border, Token::Panel, 1.15, 3.0, "surface" },
    };

    std::vector<ContrastIssue> issues;
    for (const Pair& pair : kPairs) {
        const double required = theme.enhanced ? pair.enhanced : pair.standard;
        const double measured = contrast(theme[pair.foreground], theme[pair.background]);
        if (measured + 1e-9 < required) {
            ContrastIssue issue;
            issue.what = std::string(tokenName(pair.foreground)) + " on " +
                         tokenName(pair.background);
            issue.ratio = measured;
            issue.required = required;
            issues.push_back(issue);
        }
    }
    return issues;
}

} // namespace fast
