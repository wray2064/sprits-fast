// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

// theme_store_tests.cpp — the themes a person has made, kept as files.

#include "app/file_io.h"
#include "app/theme_store.h"

#include <cstdio>
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

const std::string kFolder = "theme_store_test_folder";

void clean() {
    for (const fast::DirectoryEntry& entry : fast::listDirectory(kFolder)) {
        fast::deleteFile(entry.path);
    }
}

fast::Theme made(const std::string& name) {
    fast::Theme theme = *fast::findBuiltinTheme("graphite");
    theme.name = name;
    theme.id = fast::themeIdFromName(name);
    theme[fast::Token::Panel] = { 16, 24, 32, 255 };
    return theme;
}

void testASavedThemeComesBack() {
    clean();
    fast::createDirectory(kFolder);
    std::string id;
    std::string error;
    CHECK(fast::saveCustomTheme(kFolder, made("My Night"), &id, &error));
    CHECK(id == "my-night");
    CHECK(fast::fileExists(kFolder + "/my-night.lstheme"));

    const std::vector<fast::Theme> themes = fast::loadCustomThemes(kFolder);
    CHECK(themes.size() == 1);
    CHECK(themes[0].id == "my-night" && themes[0].name == "My Night");
    CHECK(themes[0][fast::Token::Panel] == fast::Rgba({ 16, 24, 32, 255 }));

    fast::Theme found;
    CHECK(fast::findTheme(kFolder, "my-night", &found) && found.name == "My Night");
    CHECK(fast::findTheme(kFolder, "slate", &found) && found.name == "Slate");
    CHECK(!fast::findTheme(kFolder, "nope", &found));
    CHECK(!fast::findTheme(kFolder, "../etc", &found));

    // Saving again under the same name replaces the file.
    fast::Theme again = made("My Night");
    again[fast::Token::Panel] = { 40, 40, 40, 255 };
    CHECK(fast::saveCustomTheme(kFolder, again, &id, &error));
    CHECK(fast::loadCustomThemes(kFolder).size() == 1);
    CHECK(fast::loadCustomThemes(kFolder)[0][fast::Token::Panel] == fast::Rgba({ 40, 40, 40, 255 }));
}

void testABuiltInIsNeverReplaced() {
    clean();
    std::string id;
    std::string error;
    CHECK(!fast::saveCustomTheme(kFolder, made("Slate"), &id, &error));
    CHECK(!error.empty());
    CHECK(!fast::fileExists(kFolder + "/slate.lstheme"));

    // A file somebody put there under a built-in's name is not loaded either.
    const std::string text = fast::serializeTheme(made("Slate"));
    fast::writeFileAtomic(kFolder + "/slate.lstheme",
                          std::vector<uint8_t>(text.begin(), text.end()), &error);
    CHECK(fast::loadCustomThemes(kFolder).empty());
    CHECK(!fast::deleteCustomTheme(kFolder, "slate"));
    CHECK(fast::fileExists(kFolder + "/slate.lstheme"));      // not even deleted
    clean();
}

void testIdsAreUnique() {
    clean();
    std::string id;
    std::string error;
    CHECK(fast::uniqueThemeId(kFolder, "Mine") == "mine");
    CHECK(fast::saveCustomTheme(kFolder, made("Mine"), &id, &error));
    CHECK(fast::uniqueThemeId(kFolder, "Mine") == "mine-2");
    CHECK(fast::uniqueThemeId(kFolder, "Slate") == "slate-2");     // the built-in's is taken
    clean();
}

void testDeleting() {
    clean();
    std::string id;
    std::string error;
    CHECK(fast::saveCustomTheme(kFolder, made("Gone Soon"), &id, &error));
    CHECK(!fast::deleteCustomTheme(kFolder, "../gone-soon"));
    CHECK(!fast::deleteCustomTheme(kFolder, "no-such"));
    CHECK(fast::deleteCustomTheme(kFolder, "gone-soon"));
    CHECK(!fast::fileExists(kFolder + "/gone-soon.lstheme"));
    CHECK(fast::loadCustomThemes(kFolder).empty());
    CHECK(!fast::deleteCustomTheme("", "gone-soon"));
}

// The folder is the person's: whatever is in it is read defensively.
void testAHostileFolderIsSurvivable() {
    clean();
    std::string error;
    const auto write = [&](const std::string& name, const std::string& text) {
        fast::writeFileAtomic(kFolder + "/" + name,
                              std::vector<uint8_t>(text.begin(), text.end()), &error);
    };
    write("garbage.lstheme", "not a theme at all");
    write("empty.lstheme", "");
    write("other.txt", fast::serializeTheme(made("Elsewhere")));          // wrong extension
    write("huge.lstheme", "lstheme 1\nname = Huge\n" + std::string(40000, '#'));
    write("fine.lstheme", fast::serializeTheme(made("Fine")));
    const std::vector<fast::Theme> themes = fast::loadCustomThemes(kFolder);
    CHECK(themes.size() == 1 && themes[0].id == "fine");

    // Two files claiming one id: the first only.
    write("a.lstheme", "lstheme 1\nname = Twin\n");
    write("b.lstheme", "lstheme 1\nname = Twin\n");
    int twins = 0;
    for (const fast::Theme& theme : fast::loadCustomThemes(kFolder)) {
        twins += theme.id == "twin" ? 1 : 0;
    }
    CHECK(twins == 1);

    // Bounded.
    for (int i = 0; i < 100; ++i) {
        write("t" + std::to_string(i) + ".lstheme", "lstheme 1\nname = Many " + std::to_string(i) + "\n");
    }
    CHECK(fast::loadCustomThemes(kFolder).size() <= fast::kMaxCustomThemes);

    CHECK(fast::loadCustomThemes("").empty());
    CHECK(fast::loadCustomThemes("no_such_folder_anywhere").empty());
    clean();
}

} // namespace

int main() {
    testASavedThemeComesBack();
    testABuiltInIsNeverReplaced();
    testIdsAreUnique();
    testDeleting();
    testAHostileFolderIsSurvivable();
    clean();

    if (failures == 0) {
        std::printf("fast_theme_store: all checks passed\n");
        return 0;
    }
    std::printf("fast_theme_store: %d check(s) failed\n", failures);
    return 1;
}
