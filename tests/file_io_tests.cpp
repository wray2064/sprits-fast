// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

// file_io_tests.cpp — paths and saving.
//
// The first test here is the reason this module exists. std::ofstream on Windows
// reads a const char* path in the active code page, so a UTF-8 path with an
// accent in it does not fail -- it silently writes a file with a mangled name,
// which the user then cannot find. Round-tripping inside one program hides it
// completely, because the same wrong name is used both times.

#include "app/document.h"
#include "app/file_io.h"

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

#define REQUIRE(...) do { if (!(__VA_ARGS__)) { CHECK(__VA_ARGS__); return; } } while (0)

namespace {

// Written as explicit bytes so this file's own encoding cannot change what is
// being tested: "sprit´s" with U+00B4, and 日本 (U+65E5 U+672C).
const std::string kAccented = "sprit\xC2\xB4s_\xE6\x97\xA5\xE6\x9C\xAC";

std::vector<uint8_t> bytesOf(const std::string& text) {
    return std::vector<uint8_t>(text.begin(), text.end());
}

void testUtf8PathsSurviveTheFilesystem() {
    const std::string path = kAccented + ".bin";
    const std::vector<uint8_t> payload = bytesOf("hello");

    std::string error;
    REQUIRE(fast::writeFileAtomic(path, payload, &error));

    // Written under the name asked for, not a mangled one. fileExists goes
    // through the same wide API, so this is consistent by construction -- what
    // makes it meaningful is that a directory listing outside this program shows
    // the accented name too.
    CHECK(fast::fileExists(path));

    std::vector<uint8_t> read;
    REQUIRE(fast::readFile(path, read, &error));
    CHECK(read == payload);

    CHECK(fast::deleteFile(path));
    CHECK(!fast::fileExists(path));
}

// A save must never destroy what is already on disk. If writing the new version
// fails, the old one is still there.
void testSavingIsAtomic() {
    const std::string path = "atomic_test.bin";
    std::string error;

    REQUIRE(fast::writeFileAtomic(path, bytesOf("original"), &error));
    REQUIRE(fast::writeFileAtomic(path, bytesOf("replacement"), &error));

    std::vector<uint8_t> read;
    REQUIRE(fast::readFile(path, read, &error));
    CHECK(read == bytesOf("replacement"));

    // No leftover working file beside it.
    CHECK(!fast::fileExists(path + ".saving"));

    // A path that cannot be written fails without touching the original.
    const std::string impossible = "no_such_directory_here/nested/deeper/file.bin";
    CHECK(!fast::writeFileAtomic(impossible, bytesOf("x"), &error));
    CHECK(!error.empty());

    REQUIRE(fast::readFile(path, read, &error));
    CHECK(read == bytesOf("replacement"));

    fast::deleteFile(path);
}

void testReadingWhatIsNotThere() {
    std::vector<uint8_t> read;
    std::string error;
    CHECK(!fast::readFile("definitely_not_here.bin", read, &error));
    CHECK(!error.empty());
    CHECK(!fast::fileExists("definitely_not_here.bin"));
    CHECK(!fast::deleteFile("definitely_not_here.bin"));
}

void testEmptyFilesRoundTrip() {
    const std::string path = "empty_test.bin";
    std::string error;
    REQUIRE(fast::writeFileAtomic(path, {}, &error));
    CHECK(fast::fileExists(path));

    std::vector<uint8_t> read;
    REQUIRE(fast::readFile(path, read, &error));
    CHECK(read.empty());
    fast::deleteFile(path);
}

void testPathPieces() {
    CHECK(fast::fileName("art/hero.lsprite") == "hero.lsprite");
    CHECK(fast::fileName("art\\hero.lsprite") == "hero.lsprite");
    CHECK(fast::fileName("hero.lsprite") == "hero.lsprite");
    CHECK(fast::fileName("") == "");

    CHECK(fast::fileStem("art/hero.lsprite") == "hero");
    CHECK(fast::fileStem("hero") == "hero");
    CHECK(fast::fileStem("archive.tar.gz") == "archive.tar");
    CHECK(fast::fileStem(".hidden") == ".hidden");   // a leading dot is a name

    CHECK(fast::directoryOf("art/hero.lsprite") == "art");
    CHECK(fast::directoryOf("hero.lsprite") == "");

    CHECK(fast::hasExtension("hero.lsprite", ".lsprite"));
    CHECK(fast::hasExtension("HERO.LSPRITE", ".lsprite"));
    CHECK(!fast::hasExtension("hero.png", ".lsprite"));
    CHECK(!fast::hasExtension("lsprite", ".lsprite"));

    CHECK(fast::withExtension("hero", ".lsprite") == "hero.lsprite");
    CHECK(fast::withExtension("hero.lsprite", ".lsprite") == "hero.lsprite");
    CHECK(fast::withExtension("hero.LSPRITE", ".lsprite") == "hero.LSPRITE");

    // Path pieces on a non-ASCII path must not cut a character in half.
    const std::string accented = "art/" + kAccented + ".lsprite";
    CHECK(fast::fileName(accented) == kAccented + ".lsprite");
    CHECK(fast::fileStem(accented) == kAccented);
    CHECK(fast::directoryOf(accented) == "art");
}

// The real thing: a document saved and reopened through a path with an accent.
void testDocumentRoundTripsThroughAnAccentedPath() {
    const std::string path = kAccented + ".lsprite";

    fast::Document doc;
    REQUIRE(doc.create("accented", 8, 8));
    const ls::SpriteId sprite = doc.engine().createSprite(doc.id()).value;
    const ls::LayerId layer = doc.engine().createLayer(sprite, {"main"}).value;
    const ls::GeometryId rect =
        doc.engine().createRect(doc.id(), {{2.f, 2.f}, 4.f, 4.f, 0.f}).value;
    ls::FillSolidOp fill;
    fill.targetRegion = doc.engine().createRegionFromGeometry(rect).value;
    doc.engine().addOperation(layer, fill);

    std::string error;
    REQUIRE(doc.save(path, &error));
    CHECK(fast::fileExists(path));

    fast::Document reopened;
    REQUIRE(reopened.open(path, &error));
    auto info = reopened.engine().getDocumentInfo(reopened.id());
    REQUIRE(info.ok());
    CHECK(info.value.canvasWidth == 8);
    CHECK(!info.value.sprites.empty());

    fast::deleteFile(path);
}

void testPreferencesDirectoryIsUsable() {
    const std::string directory = fast::preferencesDirectory();
    REQUIRE(!directory.empty());

    const std::string probe = directory + "/probe.bin";
    std::string error;
    CHECK(fast::writeFileAtomic(probe, bytesOf("ok"), &error));
    CHECK(fast::fileExists(probe));
    fast::deleteFile(probe);
}

} // namespace

int main() {
    testUtf8PathsSurviveTheFilesystem();
    testSavingIsAtomic();
    testReadingWhatIsNotThere();
    testEmptyFilesRoundTrip();
    testPathPieces();
    testDocumentRoundTripsThroughAnAccentedPath();
    testPreferencesDirectoryIsUsable();

    if (failures == 0) {
        std::printf("fast_file_io: all checks passed\n");
        return 0;
    }
    std::printf("fast_file_io: %d check(s) failed\n", failures);
    return 1;
}
