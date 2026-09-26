// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

// i18n_tests.cpp — the catalogue.
//
// Lines read as "English = Translation"; comments, blanks and lines without
// "=" are skipped; escapes turn into what they mean; a string missing from the
// catalogue stays English; and the catalogue Fast ships parses in full.

#include "app/file_io.h"
#include "app/i18n.h"

#include <cstdio>
#include <cstring>
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

int main(int argc, char** argv) {
    using namespace fast;
    CHECK(std::strcmp(tr("File"), "File") == 0);           // no catalogue: English
    const size_t read = loadCatalogue(
        "# a comment\n"
        "\n"
        "File = Archivo\n"
        "  Save as...   =   Guardar como...  \n"
        "no separator here\n"
        "a \\= b = a igual a b\n"
        "Two\\nlines = Dos\\nlíneas\n"
        " = nothing\n");
    CHECK(read == 4);
    CHECK(std::strcmp(tr("File"), "Archivo") == 0);
    CHECK(std::strcmp(tr("Save as..."), "Guardar como...") == 0);
    CHECK(std::strcmp(tr("a = b"), "a igual a b") == 0);
    CHECK(std::strcmp(tr("Two\nlines"), "Dos\nlíneas") == 0);
    CHECK(std::strcmp(tr("Quit"), "Quit") == 0);           // missing: English
    clearCatalogue();
    CHECK(std::strcmp(tr("File"), "File") == 0);

    // The shipped Spanish catalogue, when the tests are given its folder.
    if (argc > 1) {
        std::vector<uint8_t> bytes;
        std::string error;
        const std::string path = joinPath(argv[1], "es.txt");
        CHECK(readFile(path, bytes, &error));
        const size_t entries = loadCatalogue(std::string(bytes.begin(), bytes.end()));
        CHECK(entries > 150);
        CHECK(std::strcmp(tr("Merge down"), "Combinar hacia abajo") == 0);
        CHECK(languagesIn(argv[1]) == std::vector<std::string>({ "es" }));
        clearCatalogue();
    }

    if (failures == 0) {
        std::printf("i18n: all passed\n");
        return 0;
    }
    std::printf("i18n: %d failure(s)\n", failures);
    return 1;
}
