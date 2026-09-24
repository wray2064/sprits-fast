// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

#include "app/reference.h"

#include "app/file_io.h"
#include "app/image_io.h"

#include <algorithm>
#include <cstdlib>

namespace fast {
namespace {

// The same escape the cycles use: only the field separator, the line
// separator and the escape itself can hurt, and everything else -- including
// any UTF-8 -- passes through, because escaping bytes above 127 would mangle
// every name that is not English.
std::string escape(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (char c : text) {
        switch (c) {
            case '|':  out += "%7C"; break;
            case '\n': out += "%0A"; break;
            case '\r': out += "%0D"; break;
            case '%':  out += "%25"; break;
            default:   out.push_back(c); break;
        }
    }
    return out;
}

int hexDigit(char c) {
    if (c >= '0' && c <= '9') { return c - '0'; }
    if (c >= 'A' && c <= 'F') { return c - 'A' + 10; }
    if (c >= 'a' && c <= 'f') { return c - 'a' + 10; }
    return -1;
}

std::string unescape(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '%' || i + 2 >= text.size()) {
            out.push_back(text[i]);
            continue;
        }
        const int high = hexDigit(text[i + 1]);
        const int low  = hexDigit(text[i + 2]);
        if (high < 0 || low < 0) {
            out.push_back(text[i]);
            continue;
        }
        out.push_back(static_cast<char>(high * 16 + low));
        i += 2;
    }
    return out;
}

std::vector<std::string> split(const std::string& line, char separator) {
    std::vector<std::string> out;
    size_t start = 0;
    for (size_t i = 0; i <= line.size(); ++i) {
        if (i == line.size() || line[i] == separator) {
            out.push_back(line.substr(start, i - start));
            start = i + 1;
        }
    }
    return out;
}

float toFloat(const std::string& text, float fallback) {
    if (text.empty() || text.size() > 32) {
        return fallback;
    }
    char* end = nullptr;
    const double value = std::strtod(text.c_str(), &end);
    if (end == text.c_str() || value != value) {        // unparsed, or NaN
        return fallback;
    }
    // A reference placed a million pixels away is a reference nobody can find.
    // Clamped rather than refused: the placement is a convenience, and losing
    // the image because its position was silly would be worse.
    return static_cast<float>(std::clamp(value, -100000.0, 100000.0));
}

uint32_t toUnsigned(const std::string& text, uint32_t fallback) {
    if (text.empty() || text.size() > 12) {
        return fallback;
    }
    char* end = nullptr;
    const long value = std::strtol(text.c_str(), &end, 10);
    if (end == text.c_str() || value < 0) {
        return fallback;
    }
    return static_cast<uint32_t>(std::min<long>(value, kMaxReferenceDimension));
}

// An id is a package entry name, so it is kept to digits: nothing a name can
// carry reaches the path, and a file that says otherwise is refused.
bool plausibleId(const std::string& id) {
    if (id.empty() || id.size() > 8) {
        return false;
    }
    for (char c : id) {
        if (c < '0' || c > '9') {
            return false;
        }
    }
    return true;
}

std::string nextId(const std::vector<Reference>& existing) {
    long highest = 0;
    for (const Reference& reference : existing) {
        highest = std::max(highest, std::strtol(reference.id.c_str(), nullptr, 10));
    }
    return std::to_string(highest + 1);
}

} // namespace

std::string encodeReferences(const std::vector<Reference>& references) {
    std::string out = "lsfast-references 1\n";
    for (const Reference& reference : references) {
        out += reference.id;
        out += '|';
        out += escape(reference.name);
        out += '|';
        out += std::to_string(reference.width);
        out += '|';
        out += std::to_string(reference.height);
        out += '|';
        out += std::to_string(reference.x);
        out += '|';
        out += std::to_string(reference.y);
        out += '|';
        out += std::to_string(reference.scale);
        out += '|';
        out += std::to_string(reference.opacity);
        out += '|';
        out += reference.visible ? '1' : '0';
        out += reference.behind ? '1' : '0';
        out += reference.locked ? '1' : '0';
        out += '\n';
    }
    return out;
}

bool decodeReferences(const std::string& text, std::vector<Reference>* out) {
    if (out == nullptr) {
        return false;
    }
    out->clear();

    const std::string header = "lsfast-references 1";
    if (text.compare(0, header.size(), header) != 0) {
        return false;
    }

    size_t at = text.find('\n');
    size_t guard = 0;
    while (at != std::string::npos && ++guard <= kMaxReferences + 4) {
        const size_t start = at + 1;
        if (start >= text.size()) {
            break;
        }
        at = text.find('\n', start);
        const std::string line = text.substr(start, at == std::string::npos
                                                        ? std::string::npos : at - start);
        if (line.empty()) {
            continue;
        }
        const std::vector<std::string> fields = split(line, '|');
        if (fields.size() < 9 || !plausibleId(fields[0])) {
            continue;                 // a line this build does not understand
        }
        Reference reference;
        reference.id = fields[0];
        reference.name = unescape(fields[1]);
        if (reference.name.size() > 128) {
            reference.name.resize(128);
        }
        reference.width = toUnsigned(fields[2], 0);
        reference.height = toUnsigned(fields[3], 0);
        reference.x = toFloat(fields[4], 0.f);
        reference.y = toFloat(fields[5], 0.f);
        reference.scale = std::clamp(toFloat(fields[6], 1.f), 0.01f, 64.f);
        reference.opacity = std::clamp(toFloat(fields[7], 0.5f), 0.f, 1.f);
        const std::string& flags = fields[8];
        reference.visible = flags.size() > 0 && flags[0] == '1';
        reference.behind  = flags.size() > 1 ? flags[1] == '1' : true;
        reference.locked  = flags.size() > 2 && flags[2] == '1';

        // A reference with no size names no picture that can be drawn.
        if (reference.width == 0 || reference.height == 0) {
            continue;
        }
        out->push_back(std::move(reference));
        if (out->size() >= kMaxReferences) {
            break;
        }
    }
    return true;
}

std::vector<Reference> readReferences(Document& doc) {
    std::vector<Reference> out;
    const std::vector<uint8_t>* stored = doc.companion(kReferenceListEntry);
    if (stored == nullptr || stored->empty()) {
        return out;
    }
    const std::string text(stored->begin(), stored->end());
    std::vector<Reference> listed;
    if (!decodeReferences(text, &listed)) {
        return out;
    }
    // A listed reference whose image is not in the package is dropped: the
    // list is a claim, the entries are the fact.
    for (Reference& reference : listed) {
        if (doc.companion(reference.entryName()) != nullptr) {
            out.push_back(std::move(reference));
        }
    }
    return out;
}

const std::vector<uint8_t>* referenceBytes(Document& doc, const Reference& reference) {
    return doc.companion(reference.entryName());
}

bool writeReferences(Document& doc, const std::vector<Reference>& references) {
    const std::string text = encodeReferences(references);
    return doc.setCompanion(kReferenceListEntry, "text/plain",
                            std::vector<uint8_t>(text.begin(), text.end()));
}

bool updateReference(Document& doc, const Reference& reference) {
    std::vector<Reference> references = readReferences(doc);
    for (Reference& held : references) {
        if (held.id == reference.id) {
            held = reference;
            return writeReferences(doc, references);
        }
    }
    return false;
}

size_t referenceBytesUsed(Document& doc) {
    size_t total = 0;
    for (const Reference& reference : readReferences(doc)) {
        if (const std::vector<uint8_t>* bytes = referenceBytes(doc, reference)) {
            total += bytes->size();
        }
    }
    return total;
}

void fitReference(Reference& reference, uint32_t canvasWidth, uint32_t canvasHeight) {
    if (reference.width == 0 || reference.height == 0 ||
        canvasWidth == 0 || canvasHeight == 0) {
        return;
    }
    const float byWidth = static_cast<float>(canvasWidth) /
                          static_cast<float>(reference.width);
    const float byHeight = static_cast<float>(canvasHeight) /
                           static_cast<float>(reference.height);
    reference.scale = std::clamp(std::min(byWidth, byHeight), 0.01f, 64.f);
    reference.x = (static_cast<float>(canvasWidth) -
                   static_cast<float>(reference.width) * reference.scale) * 0.5f;
    reference.y = (static_cast<float>(canvasHeight) -
                   static_cast<float>(reference.height) * reference.scale) * 0.5f;
}

bool addReference(Document& doc, const std::string& name,
                  const std::vector<uint8_t>& imageBytes, Reference* out,
                  std::string* error) {
    if (out == nullptr) {
        return false;
    }
    std::vector<Reference> references = readReferences(doc);
    if (references.size() >= kMaxReferences) {
        if (error) {
            *error = "this document already holds " + std::to_string(kMaxReferences) +
                     " references";
        }
        return false;
    }

    // Decoded whether or not it is already a PNG: it is the only way to know
    // the size is real rather than claimed, and the only way to scale a very
    // large one down. Untrusted input is bounded in image_io.
    ls::RasterBuffer raster;
    if (!decodeImage(imageBytes, &raster, error)) {
        return false;
    }
    raster = downscaleNearest(raster, kMaxReferenceSide);

    std::vector<uint8_t> png;
    if (!encodeImageAsPng(raster, &png, error)) {
        return false;
    }
    if (png.size() > kMaxReferenceBytes) {
        if (error) {
            *error = "the image is " + std::to_string(png.size() / (1024 * 1024)) +
                     " MB once stored, which is more than a reference may be";
        }
        return false;
    }
    if (referenceBytesUsed(doc) + png.size() > kMaxReferenceTotalBytes) {
        if (error) {
            *error = "the references would outweigh the artwork; remove one first";
        }
        return false;
    }

    Reference reference;
    reference.id = nextId(references);
    reference.name = fileStem(name).empty() ? name : fileStem(name);
    if (reference.name.size() > 128) {
        reference.name.resize(128);
    }
    reference.width = raster.width;
    reference.height = raster.height;

    auto size = doc.engine().getCanvasSize(doc.id());
    if (size.ok() && size.value.x > 0 && size.value.y > 0) {
        fitReference(reference, static_cast<uint32_t>(size.value.x),
                     static_cast<uint32_t>(size.value.y));
    }

    // One undo step for the pair: the image and the list that names it.
    doc.beginAction("Add reference");
    if (!doc.setCompanion(reference.entryName(), "image/png", std::move(png))) {
        doc.abandonAction();
        if (error) { *error = "the document would not take another entry"; }
        return false;
    }
    references.push_back(reference);
    if (!writeReferences(doc, references)) {
        doc.clearCompanion(reference.entryName());
        doc.abandonAction();
        if (error) { *error = "the reference list could not be written"; }
        return false;
    }
    doc.endAction();
    *out = reference;
    return true;
}

bool importReference(Document& doc, const std::string& utf8Path, Reference* out,
                     std::string* error) {
    std::vector<uint8_t> bytes;
    if (!readFile(utf8Path, bytes, error)) {
        return false;
    }
    return addReference(doc, fileName(utf8Path), bytes, out, error);
}

bool removeReference(Document& doc, const Reference& reference) {
    std::vector<Reference> references = readReferences(doc);
    const size_t before = references.size();
    references.erase(std::remove_if(references.begin(), references.end(),
                                    [&](const Reference& held) {
                                        return held.id == reference.id;
                                    }),
                     references.end());
    if (references.size() == before) {
        return false;
    }
    doc.beginAction("Remove reference");
    doc.clearCompanion(reference.entryName());
    writeReferences(doc, references);
    doc.endAction();
    return true;
}

} // namespace fast
