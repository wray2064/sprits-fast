// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

// theme_extension_tests.cpp — what a program built on Fast adds to its themes:
// roles of its own and built-in themes of its own, through the same types, so
// one theme file styles both and the readability check covers both.

#include "app/theme_tokens.h"

#include <cstdio>
#include <string>
#include <vector>

static int failures = 0;

#define CHECK(...)                                                            \
    do {                                                                      \
        if (!(__VA_ARGS__)) {                                                 \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #__VA_ARGS__);\
            ++failures;                                                       \
        }                                                                     \
    } while (0)

namespace {

using fast::Rgba;

fast::ExtraRole role(const std::string& id, Rgba dark, Rgba light, double standard = 0.0,
                     double enhanced = 0.0) {
    fast::ExtraRole r;
    r.id = id;
    r.label = id;
    r.group = "Test";
    r.dark = dark;
    r.light = light;
    r.standard = standard;
    r.enhanced = enhanced;
    return r;
}

void testIdsAreSafeKeys() {
    CHECK(fast::validExtraId("pract.track"));
    CHECK(fast::validExtraId("pract.track-key-2"));
    CHECK(!fast::validExtraId(""));
    CHECK(!fast::validExtraId("track"));                 // no owner
    CHECK(!fast::validExtraId(".track"));
    CHECK(!fast::validExtraId("pract."));
    CHECK(!fast::validExtraId("a.b.c"));                 // one dot
    CHECK(!fast::validExtraId("Pract.track"));           // lower case only
    CHECK(!fast::validExtraId("pract.tr ack"));
    CHECK(!fast::validExtraId("pract.tr=ack"));
    CHECK(!fast::validExtraId("pract.tr/ack"));
    CHECK(!fast::validExtraId(std::string(60, 'a') + ".b"));
    // None of Fast's own role names has a dot, so an extra id can never be one.
    for (size_t i = 0; i < fast::kTokenCount; ++i) {
        CHECK(!fast::validExtraId(fast::tokenName(static_cast<fast::Token>(i))));
    }
}

void testRolesRegisterOnce() {
    fast::clearExtraRoles();
    const int taken = fast::addExtraRoles({ role("pract.track", { 80, 160, 255, 255 }, { 0, 70, 200, 255 }),
                                            role("bad id", { 0, 0, 0, 255 }, { 0, 0, 0, 255 }),
                                            role("pract.track", { 1, 2, 3, 255 }, { 1, 2, 3, 255 }),
                                            role("pract.tint", { 9, 9, 9, 255 }, { 8, 8, 8, 255 }) });
    CHECK(taken == 2);
    CHECK(fast::extraRoles().size() == 2);
    const fast::ExtraRole* found = fast::findExtraRole("pract.track");
    CHECK(found != nullptr && found->dark == Rgba({ 80, 160, 255, 255 }));   // the first one stands
    CHECK(fast::findExtraRole("bad id") == nullptr);

    // At most 64, however many are offered.
    std::vector<fast::ExtraRole> many;
    for (int i = 0; i < 100; ++i) {
        many.push_back(role("many.r" + std::to_string(i), { 0, 0, 0, 255 }, { 0, 0, 0, 255 }));
    }
    fast::addExtraRoles(many);
    CHECK(fast::extraRoles().size() == 64);
    fast::clearExtraRoles();
    CHECK(fast::extraRoles().empty());
}

void testADefaultFollowsThePolarity() {
    fast::clearExtraRoles();
    fast::addExtraRoles({ role("pract.track", { 80, 160, 255, 255 }, { 0, 70, 200, 255 }) });
    const fast::Theme* slate = fast::findBuiltinTheme("slate");
    const fast::Theme* paper = fast::findBuiltinTheme("paper");
    CHECK(slate != nullptr && paper != nullptr);
    CHECK(slate->extraColour("pract.track") == Rgba({ 80, 160, 255, 255 }));
    CHECK(paper->extraColour("pract.track") == Rgba({ 0, 70, 200, 255 }));
    // A role nobody registered is magenta: a misspelt id is seen, not hidden.
    CHECK(slate->extraColour("pract.nope") == Rgba({ 255, 0, 255, 255 }));
    // A theme that says outright wins over the default.
    fast::Theme own = *slate;
    own.extra["pract.track"] = Rgba{ 1, 2, 3, 255 };
    CHECK(own.extraColour("pract.track") == Rgba({ 1, 2, 3, 255 }));
    fast::clearExtraRoles();
}

void testAFileCarriesTheRolesBothWays() {
    fast::clearExtraRoles();
    fast::addExtraRoles({ role("pract.track", { 80, 160, 255, 255 }, { 0, 70, 200, 255 }),
                          role("pract.tint", { 9, 9, 9, 255 }, { 8, 8, 8, 255 }) });
    fast::Theme theme;
    fast::ThemeReport report;
    CHECK(fast::parseTheme("lstheme 1\nname = Tracks\nbase = midnight\npract.track = #ff0000\n"
                           "pract.other = #00ff00\n",
                           &theme, &report));
    CHECK(report.unknownKeys == 0 && report.badColours == 0);
    CHECK(theme.extraColour("pract.track") == Rgba({ 255, 0, 0, 255 }));
    CHECK(theme.extraColour("pract.tint") == Rgba({ 9, 9, 9, 255 }));   // the default, midnight is dark
    // Written whole: every registered role, and the one nobody registered.
    const std::string text = fast::serializeTheme(theme);
    CHECK(text.find("pract.track = #ff0000") != std::string::npos);
    CHECK(text.find("pract.tint = #090909") != std::string::npos);
    CHECK(text.find("pract.other = #00ff00") != std::string::npos);
    fast::Theme again;
    CHECK(fast::parseTheme(text, &again, nullptr));
    CHECK(fast::sameColours(theme, again));

    // Opened where nothing is registered, saved, and opened again with Pract
    // present: nothing was lost on the way.
    fast::clearExtraRoles();
    fast::Theme bare;
    CHECK(fast::parseTheme(text, &bare, nullptr));
    CHECK(bare.extra.count("pract.track") == 1 && bare.extra.count("pract.tint") == 1);
    const std::string kept = fast::serializeTheme(bare);
    CHECK(kept.find("pract.track = #ff0000") != std::string::npos);
    fast::addExtraRoles({ role("pract.track", { 80, 160, 255, 255 }, { 0, 70, 200, 255 }) });
    fast::Theme back;
    CHECK(fast::parseTheme(kept, &back, nullptr));
    CHECK(back.extraColour("pract.track") == Rgba({ 255, 0, 0, 255 }));
    fast::clearExtraRoles();
}

void testAHostileFileCannotFloodATheme() {
    fast::clearExtraRoles();
    std::string text = "lstheme 1\nname = Flood\n";
    text += "pract.bad = #12345\n";            // not a colour
    text += "Pract.Upper = #123456\n";         // not an id
    text += "a.b.c = #123456\n";               // not an id
    for (int i = 0; i < 300; ++i) {
        text += "x.r" + std::to_string(i) + " = #123456\n";
    }
    fast::Theme theme;
    fast::ThemeReport report;
    CHECK(fast::parseTheme(text, &theme, &report));
    CHECK(theme.extra.size() == 64);            // the rest were counted and dropped
    CHECK(report.badColours == 1);
    CHECK(report.unknownKeys >= 2 + (300 - 64));
}

void testTheCheckCoversAProgramsRoles() {
    fast::clearExtraRoles();
    // A track colour is an indicator: 3 against the panel, 4.5 in a high-contrast theme.
    fast::ExtraRole track = role("pract.track", { 80, 160, 255, 255 }, { 0, 70, 200, 255 }, 3.0, 4.5);
    fast::addExtraRoles({ track });
    for (const fast::Theme& theme : fast::builtinThemes()) {
        // The defaults chosen for a polarity read on the built-ins' panels.
        bool failed = false;
        for (const fast::ContrastIssue& issue : fast::checkTheme(theme)) {
            if (issue.what.compare(0, 11, "pract.track") == 0) {
                failed = true;
            }
        }
        if (theme.id == "high-contrast-dark" || theme.id == "high-contrast-light") {
            continue;           // held to 4.5; the default is for the standard target
        }
        CHECK(!failed);
    }

    // A theme that sets it to something unreadable is reported, and repaired.
    fast::Theme bad = *fast::findBuiltinTheme("slate");
    bad.extra["pract.track"] = bad[fast::Token::Panel];            // invisible on the panel
    std::vector<fast::ContrastIssue> issues = fast::checkTheme(bad);
    bool named = false;
    for (const fast::ContrastIssue& issue : issues) {
        if (issue.what == "pract.track on panel") {
            named = true;
            CHECK(issue.required == 3.0 && issue.ratio < 3.0);
        }
    }
    CHECK(named);
    CHECK(fast::fixContrast(&bad) >= 1);
    CHECK(fast::checkTheme(bad).empty());
    CHECK(bad.extraColour("pract.track") != bad[fast::Token::Panel]);

    // An enhanced theme asks for more.
    fast::Theme enhanced = *fast::findBuiltinTheme("high-contrast-dark");
    enhanced.extra["pract.track"] = Rgba{ 70, 70, 70, 255 };     // about 2:1 on near-black
    bool asksMore = false;
    for (const fast::ContrastIssue& issue : fast::checkTheme(enhanced)) {
        if (issue.what == "pract.track on panel") {
            asksMore = issue.required == 4.5;
        }
    }
    CHECK(asksMore);
    fast::clearExtraRoles();
}

void testBuiltInThemesCanBeAdded() {
    const size_t before = fast::builtinThemes().size();
    const std::string tealDark = "lstheme 1\nname = Test teal\nbase = midnight\ndark = 1\n"
                                 "accent = #28c8b4\n";
    const std::string tealLight = "lstheme 1\nname = Test sand\nbase = paper\ndark = 0\n";
    const int taken = fast::addBuiltinThemes({ tealLight, tealDark, "not a theme",
                                               "lstheme 1\nname = Slate\n" });   // id taken
    CHECK(taken == 2);
    CHECK(fast::builtinThemes().size() == before + 2);
    CHECK(fast::findBuiltinTheme("test-teal") != nullptr);
    CHECK(fast::findBuiltinTheme("test-sand") != nullptr);
    CHECK(fast::findBuiltinTheme("slate") != nullptr);

    // The chooser's order: every dark theme before every light one, Fast's first.
    bool seenLight = false;
    for (const fast::Theme& theme : fast::builtinThemes()) {
        if (!theme.dark) {
            seenLight = true;
        } else {
            CHECK(!seenLight);
        }
    }
    CHECK(fast::builtinThemes().front().id == "slate");
    CHECK(fast::builtinThemes().back().id == "test-sand");

    // Their accents were derived, and they read.
    CHECK(fast::checkTheme(*fast::findBuiltinTheme("test-teal")).empty());

    // And a theme file can start from one of them.
    fast::Theme child;
    CHECK(fast::parseTheme("lstheme 1\nname = Child\nbase = test-teal\n", &child, nullptr));
    CHECK(child[fast::Token::Accent] == (*fast::findBuiltinTheme("test-teal"))[fast::Token::Accent]);

    // No more than 16 are ever added.
    std::vector<std::string> flood;
    for (int i = 0; i < 40; ++i) {
        flood.push_back("lstheme 1\nname = Flood " + std::to_string(i) + "\nbase = slate\n");
    }
    fast::addBuiltinThemes(flood);
    CHECK(fast::builtinThemes().size() <= 9 + 16);
}

} // namespace

int main() {
    testIdsAreSafeKeys();
    testRolesRegisterOnce();
    testADefaultFollowsThePolarity();
    testAFileCarriesTheRolesBothWays();
    testAHostileFileCannotFloodATheme();
    testTheCheckCoversAProgramsRoles();
    testBuiltInThemesCanBeAdded();

    if (failures == 0) {
        std::printf("fast_theme_extension: all checks passed\n");
        return 0;
    }
    std::printf("fast_theme_extension: %d check(s) failed\n", failures);
    return 1;
}
