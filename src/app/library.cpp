// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

#include "app/library.h"

#include "app/file_io.h"
#include "app/image_io.h"

#include <algorithm>

namespace fast {
namespace {

constexpr const char* kDocumentExtension = ".lsprite";

// A package is read whole to find one entry, so a listing must not be pointed
// at something enormous. Well above any sprite and far below what would hurt.
constexpr size_t kMaxDocumentBytes = 64u * 1024u * 1024u;

} // namespace

std::vector<LibraryDocument> listDocuments(const std::string& folder) {
    std::vector<LibraryDocument> out;
    for (const DirectoryEntry& entry : listDirectory(folder)) {
        if (entry.directory || !hasExtension(entry.name, kDocumentExtension)) {
            continue;
        }
        LibraryDocument document;
        document.path = entry.path;
        document.name = fileStem(entry.name);
        document.bytes = entry.bytes;
        document.modifiedSeconds = entry.modifiedSeconds;
        out.push_back(std::move(document));
    }
    return out;
}

std::vector<LibraryImage> listImages(const std::string& folder) {
    std::vector<LibraryImage> out;
    for (const DirectoryEntry& entry : listDirectory(folder)) {
        if (entry.directory || !looksLikeImageName(entry.name)) {
            continue;
        }
        LibraryImage image;
        image.path = entry.path;
        image.name = entry.name;
        image.bytes = entry.bytes;
        out.push_back(std::move(image));
    }
    return out;
}

std::vector<std::string> listFolders(const std::string& folder) {
    std::vector<std::string> out;
    for (const DirectoryEntry& entry : listDirectory(folder)) {
        if (entry.directory) {
            out.push_back(entry.path);
        }
    }
    return out;
}

bool updateThumbnail(Document& doc) {
    auto info = doc.engine().getDocumentInfo(doc.id());
    if (info.fail() || info.value.sprites.empty()) {
        return false;
    }
    auto size = doc.engine().getCanvasSize(doc.id());
    if (size.fail() || size.value.x <= 0 || size.value.y <= 0) {
        return false;
    }

    ls::CompileProfile profile;
    profile.type = ls::CompileProfileType::Export;
    profile.outputWidth = static_cast<uint32_t>(size.value.x);
    profile.outputHeight = static_cast<uint32_t>(size.value.y);
    profile.palette = ls::PalettePolicy::Unconstrained;

    // The first frame, which is the one a person thinks of as the sprite.
    auto compiled = doc.engine().compileSprite(info.value.sprites.front(), profile);
    if (compiled.fail() || compiled.value.raster.empty()) {
        return false;
    }
    const ls::RasterBuffer small = downscaleNearest(compiled.value.raster, kThumbnailSize);

    std::vector<uint8_t> png;
    std::string error;
    if (!encodeImageAsPng(small, &png, &error)) {
        return false;
    }
    return doc.setCompanion(kThumbnailEntry, "image/png", std::move(png));
}

bool readThumbnail(const std::string& utf8Path, std::vector<uint8_t>* out) {
    if (out == nullptr) {
        return false;
    }
    out->clear();

    std::vector<uint8_t> bytes;
    std::string error;
    if (!readFile(utf8Path, bytes, &error) || bytes.size() > kMaxDocumentBytes) {
        return false;
    }

    // The container is read, but no document is built: a listing that opened
    // every file it showed would be a listing that takes a minute on a folder
    // of thirty sprites.
    std::unique_ptr<ls::LSContext> reader = ls::LSContext::create();
    if (!reader) {
        return false;
    }
    ls::SerializedData package;
    package.bytes = std::move(bytes);
    auto contents = reader->readPackage(package);
    if (contents.fail()) {
        return false;
    }
    for (const ls::PackageEntry& entry : contents.value.entries) {
        if (entry.name == kThumbnailEntry) {
            *out = entry.data;
            return !out->empty();
        }
    }
    return false;                 // saved by a build before thumbnails existed
}

// --- the remembered folders ------------------------------------------------

std::string LibraryFolders::storagePath() {
    const std::string directory = preferencesDirectory();
    if (directory.empty()) {
        return std::string();
    }
    return directory + "/library.txt";
}

void LibraryFolders::load() {
    project.clear();
    references.clear();

    const std::string path = storagePath();
    if (path.empty()) {
        return;
    }
    std::vector<uint8_t> bytes;
    std::string error;
    if (!readFile(path, bytes, &error)) {
        return;
    }
    // Two lines, each "key path". Nothing else is read, and a line that is
    // not one of the two is skipped rather than guessed at.
    const std::string text(bytes.begin(), bytes.end());
    size_t start = 0;
    while (start < text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string::npos) {
            end = text.size();
        }
        std::string line = text.substr(start, end - start);
        start = end + 1;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) {
            line.pop_back();
        }
        const size_t space = line.find(' ');
        if (space == std::string::npos || line.size() > 4096) {
            continue;
        }
        const std::string key = line.substr(0, space);
        const std::string value = line.substr(space + 1);
        if (key == "project") {
            project = value;
        } else if (key == "references") {
            references = value;
        }
    }
}

void LibraryFolders::save() const {
    const std::string path = storagePath();
    if (path.empty()) {
        return;
    }
    std::string text;
    if (!project.empty()) {
        text += "project " + project + "\n";
    }
    if (!references.empty()) {
        text += "references " + references + "\n";
    }
    std::string error;
    writeFileAtomic(path, std::vector<uint8_t>(text.begin(), text.end()), &error);
}

} // namespace fast
