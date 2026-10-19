// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

// theme_tokens_tests.cpp — the colours of the interface, and that they can be
// read.

#include "app/theme_tokens.h"

#include <cmath>
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

bool near(double a, double b, double tolerance = 0.01) {
    return std::fabs(a - b) <= tolerance;
}

void testTheArithmeticIsWcag() {
    const fast::Rgba black{ 0, 0, 0, 255 };
    const fast::Rgba white{ 255, 255, 255, 255 };
    CHECK(near(fast::luminance(black), 0.0));
    CHECK(near(fast::luminance(white), 1.0));
    CHECK(near(fast::contrast(white, black), 21.0));
    CHECK(near(fast::contrast(black, white), 21.0));
    CHECK(near(fast::contrast(white, white), 1.0));
    // A translucent colour is measured as it will look over its ground: half
    // white over black is mid grey.
    const fast::Rgba half{ 255, 255, 255, 128 };
    const fast::Rgba grey = fast::composite(half, black);
    CHECK(grey.r == 128 && grey.g == 128 && grey.b == 128 && grey.a == 255);
    CHECK(near(fast::contrast(half, black), fast::contrast(grey, black)));
    // #777 on white is the classic 4.48 that just misses AA.
    CHECK(near(fast::contrast({ 0x77, 0x77, 0x77, 255 }, white), 4.48, 0.02));
    CHECK(fast::mix(black, white, 0.5f).r == 128);
    CHECK(fast::mix(black, white, -3.f) == black);
    CHECK(fast::mix(black, white, 3.f) == white);
}

void testColoursParseAndPrint() {
    fast::Rgba c;
    CHECK(fast::parseColour("#ff9e26", &c) && c == fast::Rgba({ 255, 158, 38, 255 }));
    CHECK(fast::parseColour("FF9E26", &c) && c == fast::Rgba({ 255, 158, 38, 255 }));
    CHECK(fast::parseColour("#ff9e2680", &c) && c == fast::Rgba({ 255, 158, 38, 128 }));
    CHECK(!fast::parseColour("", &c));
    CHECK(!fast::parseColour("#fff", &c));
    CHECK(!fast::parseColour("#gg0000", &c));
    CHECK(!fast::parseColour("#ff9e26ff00", &c));
    CHECK(fast::colourText({ 255, 158, 38, 255 }) == "#ff9e26");
    CHECK(fast::colourText({ 255, 158, 38, 128 }) == "#ff9e2680");
}

void testTokenNamesRoundTrip() {
    std::set<std::string> names;
    for (size_t i = 0; i < fast::kTokenCount; ++i) {
        const fast::Token token = static_cast<fast::Token>(i);
        const std::string name = fast::tokenName(token);
        CHECK(!name.empty());
        CHECK(names.insert(name).second);
        fast::Token back;
        CHECK(fast::tokenFromName(name, &back) && back == token);
    }
    fast::Token none;
    CHECK(!fast::tokenFromName("nonsense", &none));
    CHECK(!fast::tokenFromName("", &none));
}

void testTheBuiltInsAreAsPromised() {
    const std::vector<fast::Theme>& themes = fast::builtinThemes();
    CHECK(themes.size() == 9);
    std::set<std::string> ids;
    int dark = 0;
    int light = 0;
    for (const fast::Theme& theme : themes) {
        CHECK(!theme.id.empty() && !theme.name.empty());
        CHECK(ids.insert(theme.id).second);
        CHECK(fast::findBuiltinTheme(theme.id) == &theme);
        (theme.dark ? dark : light)++;
    }
    CHECK(dark == 5 && light == 4);
    CHECK(fast::findBuiltinTheme("slate") != nullptr);
    CHECK(fast::findBuiltinTheme("paper") != nullptr);
    CHECK(fast::findBuiltinTheme("oled") != nullptr);
    CHECK(fast::findBuiltinTheme("nope") == nullptr);

    // OLED is what it says: black where black is free.
    const fast::Theme* oled = fast::findBuiltinTheme("oled");
    CHECK(oled != nullptr && (*oled)[fast::Token::App] == fast::Rgba({ 0, 0, 0, 255 }));
    CHECK(oled != nullptr && (*oled)[fast::Token::Canvas] == fast::Rgba({ 0, 0, 0, 255 }));
    // ...and nothing large is lit: no pure white anywhere in its text.
    CHECK(oled != nullptr && (*oled)[fast::Token::TextStrong].r < 255);
}

// The point of the whole file: every theme that ships can be read.
void testEveryBuiltInThemeIsReadable() {
    for (const fast::Theme& theme : fast::builtinThemes()) {
        const std::vector<fast::ContrastIssue> issues = fast::checkTheme(theme);
        for (const fast::ContrastIssue& issue : issues) {
            std::printf("  %s: %s is %.2f, needs %.1f\n", theme.id.c_str(), issue.what.c_str(),
                        issue.ratio, issue.required);
        }
        CHECK(issues.empty());
    }
}

// The checker has to be able to fail, or passing means nothing.
void testTheCheckerFindsABadTheme() {
    fast::Theme bad = *fast::findBuiltinTheme("slate");
    bad[fast::Token::TextMuted] = bad[fast::Token::Panel];          // invisible
    const std::vector<fast::ContrastIssue> issues = fast::checkTheme(bad);
    CHECK(!issues.empty());
    bool named = false;
    for (const fast::ContrastIssue& issue : issues) {
        named = named || issue.what == "textMuted on panel";
    }
    CHECK(named);

    // The enhanced targets are stricter: Slate's text passes standard and not
    // enhanced.
    fast::Theme strict = *fast::findBuiltinTheme("slate");
    strict.enhanced = true;
    CHECK(!fast::checkTheme(strict).empty());
}

void testTheAccentBringsItsRoles() {
    fast::Theme dark = *fast::findBuiltinTheme("slate");
    dark[fast::Token::Accent] = { 40, 200, 160, 255 };
    fast::deriveAccentRoles(&dark);
    CHECK(dark[fast::Token::Focus] == dark[fast::Token::Accent]);
    CHECK(fast::luminance(dark[fast::Token::AccentHover]) >
          fast::luminance(dark[fast::Token::Accent]));
    CHECK(fast::luminance(dark[fast::Token::AccentPressed]) <
          fast::luminance(dark[fast::Token::Accent]));
    CHECK(dark[fast::Token::AccentSubtle].a > 0 && dark[fast::Token::AccentSubtle].a < 128);
    // Text on the accent is whichever of dark and white reads better.
    CHECK(fast::contrast(dark[fast::Token::TextOnAccent], dark[fast::Token::Accent]) >= 4.5);

    fast::Theme light = *fast::findBuiltinTheme("paper");
    light[fast::Token::Accent] = { 200, 60, 90, 255 };
    fast::deriveAccentRoles(&light);
    // On a light theme hover goes darker, not lighter.
    CHECK(fast::luminance(light[fast::Token::AccentHover]) <
          fast::luminance(light[fast::Token::Accent]));
}

void testAFileInheritsAndOverrides() {
    fast::Theme theme;
    fast::ThemeReport report;
    const std::string file =
        "lstheme 1\n"
        "name = My Night\n"
        "base = graphite\n"
        "panel = #101820\n"
        "accent = #40c8a0\n";
    CHECK(fast::parseTheme(file, &theme, &report));
    CHECK(report.unknownKeys == 0 && report.badColours == 0);
    CHECK(theme.name == "My Night" && theme.id == "my-night");
    CHECK(theme.dark);
    CHECK(theme[fast::Token::Panel] == fast::Rgba({ 0x10, 0x18, 0x20, 255 }));
    // What it did not say comes from its base...
    const fast::Theme* graphite = fast::findBuiltinTheme("graphite");
    CHECK(theme[fast::Token::App] == (*graphite)[fast::Token::App]);
    // ...and the accent it changed brought its roles along.
    CHECK(theme[fast::Token::Accent] == fast::Rgba({ 0x40, 0xc8, 0xa0, 255 }));
    CHECK(theme[fast::Token::Focus] == theme[fast::Token::Accent]);
    CHECK(theme[fast::Token::AccentHover] != (*graphite)[fast::Token::AccentHover]);

    // A role the file sets itself is not overwritten by the derivation.
    CHECK(fast::parseTheme("lstheme 1\naccent = #40c8a0\nfocus = #ffffff\n", &theme, &report));
    CHECK(theme[fast::Token::Focus] == fast::Rgba({ 255, 255, 255, 255 }));

    // A light file with no base starts from paper; a dark one from slate.
    CHECK(fast::parseTheme("lstheme 1\ndark = 0\n", &theme, &report));
    CHECK(!theme.dark && theme[fast::Token::Panel] ==
          (*fast::findBuiltinTheme("paper"))[fast::Token::Panel]);
    CHECK(fast::parseTheme("lstheme 1\n", &theme, &report));
    CHECK(theme.dark && theme[fast::Token::Panel] ==
          (*fast::findBuiltinTheme("slate"))[fast::Token::Panel]);

    // Based on a high-contrast theme, it is held to the high-contrast targets.
    CHECK(fast::parseTheme("lstheme 1\nbase = high-contrast-dark\n", &theme, &report));
    CHECK(theme.enhanced);
}

void testAFileIsSerialisedWhole() {
    for (const fast::Theme& original : fast::builtinThemes()) {
        const std::string text = fast::serializeTheme(original);
        fast::Theme back;
        fast::ThemeReport report;
        CHECK(fast::parseTheme(text, &back, &report));
        CHECK(report.unknownKeys == 0 && report.badColours == 0);
        CHECK(back.dark == original.dark && back.enhanced == original.enhanced);
        CHECK(back.name == original.name);
        for (size_t i = 0; i < fast::kTokenCount; ++i) {
            const fast::Token token = static_cast<fast::Token>(i);
            CHECK(back[token] == original[token]);
        }
    }
}

// A theme file is something a person edits, or is sent, so it is read
// defensively.
void testAHostileFileIsSurvivable() {
    fast::Theme theme;
    fast::ThemeReport report;
    CHECK(!fast::parseTheme("", &theme, &report) && !report.error.empty());
    CHECK(!fast::parseTheme("not a theme", &theme, &report));
    CHECK(!fast::parseTheme("lstheme 2\n", &theme, &report));

    // Bad colours and unknown keys are counted and skipped; the rest is read.
    CHECK(fast::parseTheme("lstheme 1\npanel = nonsense\nflavour = #ffffff\n"
                           "just a line\ncontrol = #123456\n", &theme, &report));
    CHECK(report.badColours == 1 && report.unknownKeys == 2);
    CHECK(theme[fast::Token::Control] == fast::Rgba({ 0x12, 0x34, 0x56, 255 }));
    CHECK(theme[fast::Token::Panel] == (*fast::findBuiltinTheme("slate"))[fast::Token::Panel]);

    // An unknown base falls back; a long name is cut; an odd name is a safe id.
    CHECK(fast::parseTheme("lstheme 1\nbase = ../../etc\nname = " + std::string(500, 'x') + "\n",
                           &theme, &report));
    CHECK(theme.name.size() <= 48 && theme.id.size() <= 48);
    CHECK(fast::parseTheme("lstheme 1\nname = ../../My <Theme>: 1\n", &theme, &report));
    for (char ch : theme.id) {
        CHECK((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '-');
    }
    CHECK(fast::parseTheme("lstheme 1\nname = ???\n", &theme, &report));
    CHECK(!theme.id.empty());

    // Bounded: a million lines are not read.
    std::string many = "lstheme 1\n";
    for (int i = 0; i < 100000; ++i) {
        many += "control = #123456\n";
    }
    CHECK(fast::parseTheme(many, &theme, &report));
    CHECK(report.lines <= 600);
    // And a very long line is skipped, not trusted.
    CHECK(fast::parseTheme("lstheme 1\ncontrol = #" + std::string(5000, 'a') + "\n", &theme, &report));

    // Nowhere to put the result is an error, not a crash.
    CHECK(!fast::parseTheme("lstheme 1\n", nullptr, &report));
    CHECK(fast::parseTheme("lstheme 1\n", &theme, nullptr));
}

// The editor lists every role by group with a label a person can read.
void testEveryRoleIsDescribed() {
    std::set<std::string> groups;
    for (size_t i = 0; i < fast::kTokenCount; ++i) {
        const fast::Token token = static_cast<fast::Token>(i);
        CHECK(std::string(fast::tokenLabel(token)).size() > 0);
        CHECK(std::string(fast::tokenGroup(token)).size() > 0);
        groups.insert(fast::tokenGroup(token));
    }
    CHECK(groups.size() == 7);
}

void testContrastCanBeAdjustedTo() {
    const fast::Rgba panel{ 29, 34, 41, 255 };
    const fast::Rgba dim{ 60, 66, 74, 255 };
    CHECK(fast::contrast(dim, panel) < 4.5);
    const fast::Rgba fixed = fast::adjustForContrast(dim, panel, 4.5);
    CHECK(fast::contrast(fixed, panel) >= 4.5);
    CHECK(fast::luminance(fixed) > fast::luminance(dim));        // lightened on a dark ground

    const fast::Rgba paper{ 236, 229, 208, 255 };
    const fast::Rgba pale{ 220, 200, 120, 255 };
    const fast::Rgba darker = fast::adjustForContrast(pale, paper, 4.5);
    CHECK(fast::contrast(darker, paper) >= 4.5);
    CHECK(fast::luminance(darker) < fast::luminance(pale));      // darkened on a light one

    // Already enough: untouched.
    CHECK(fast::adjustForContrast({ 255, 255, 255, 255 }, panel, 4.5) ==
          fast::Rgba({ 255, 255, 255, 255 }));
}

// Any accent on any theme leaves the theme readable: that is what applyAccent
// is for, and why a person can pick any colour without checking.
void testAnyAccentOnAnyThemeStaysReadable() {
    for (const fast::Theme& original : fast::builtinThemes()) {
        for (const fast::AccentPreset& preset : fast::accentPresets()) {
            fast::Theme theme = original;
            fast::applyAccent(&theme, preset.colour);
            for (const fast::ContrastIssue& issue : fast::checkTheme(theme)) {
                // Only what the accent touches.
                const std::string what = issue.what;
                const bool accentRole = what.find("ccent") != std::string::npos ||
                                        what.find("focus") != std::string::npos;
                if (accentRole) {
                    std::printf("  %s with %s: %s is %.2f, needs %.1f\n", original.id.c_str(),
                                preset.name, what.c_str(), issue.ratio, issue.required);
                }
                CHECK(!accentRole);
            }
        }
    }
    // The pale accent on a light theme becomes a text-grade one.
    fast::Theme paper = *fast::findBuiltinTheme("paper");
    fast::applyAccent(&paper, { 250, 230, 120, 255 });
    CHECK(fast::contrast(paper[fast::Token::AccentText], paper[fast::Token::Panel]) >= 4.5);
    CHECK(paper[fast::Token::Accent].a == 255);
}

void testFixContrastRepairsATheme() {
    fast::Theme bad = *fast::findBuiltinTheme("slate");
    bad[fast::Token::TextMuted] = { 60, 66, 74, 255 };
    bad[fast::Token::Danger] = { 90, 40, 40, 255 };
    CHECK(!fast::checkTheme(bad).empty());
    const int changed = fast::fixContrast(&bad);
    CHECK(changed >= 2);
    CHECK(fast::checkTheme(bad).empty());
    // A good theme is left alone.
    fast::Theme good = *fast::findBuiltinTheme("oled");
    CHECK(fast::fixContrast(&good) == 0);
    CHECK(good[fast::Token::TextMuted] == (*fast::findBuiltinTheme("oled"))[fast::Token::TextMuted]);
}

void testThemeIdsAreSafe() {
    CHECK(fast::themeIdFromName("My Night") == "my-night");
    CHECK(fast::themeIdFromName("../x") == "x");
    CHECK(fast::themeIdFromName("???") == "custom");
    CHECK(fast::themeIdFromName(std::string(100, 'a')).size() == 48);
}

} // namespace

int main() {
    testEveryRoleIsDescribed();
    testContrastCanBeAdjustedTo();
    testAnyAccentOnAnyThemeStaysReadable();
    testFixContrastRepairsATheme();
    testThemeIdsAreSafe();
    testTheArithmeticIsWcag();
    testColoursParseAndPrint();
    testTokenNamesRoundTrip();
    testTheBuiltInsAreAsPromised();
    testEveryBuiltInThemeIsReadable();
    testTheCheckerFindsABadTheme();
    testTheAccentBringsItsRoles();
    testAFileInheritsAndOverrides();
    testAFileIsSerialisedWhole();
    testAHostileFileIsSurvivable();

    if (failures == 0) {
        std::printf("fast_theme_tokens: all checks passed\n");
        return 0;
    }
    std::printf("fast_theme_tokens: %d check(s) failed\n", failures);
    return 1;
}
