// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

#include "app/palette_io.h"
#include "app/file_io.h"
#include "app/image_io.h"
#include "app/palette_tools.h"

#include <algorithm>
#include <cstdio>

namespace fast {
namespace {

std::string trimmed(const std::string& text) {
    size_t start = 0;
    while (start < text.size() && (text[start] == ' ' || text[start] == '\t' ||
                                   text[start] == '\r')) {
        ++start;
    }
    size_t end = text.size();
    while (end > start && (text[end - 1] == ' ' || text[end - 1] == '\t' ||
                           text[end - 1] == '\r')) {
        --end;
    }
    return text.substr(start, end - start);
}

std::vector<std::string> lines(const std::string& text) {
    std::vector<std::string> out;
    std::string current;
    for (char c : text) {
        if (c == '\n') {
            out.push_back(current);
            current.clear();
        } else {
            current.push_back(c);
        }
    }
    out.push_back(current);
    return out;
}

int hexValue(char c) {
    if (c >= '0' && c <= '9') { return c - '0'; }
    if (c >= 'a' && c <= 'f') { return c - 'a' + 10; }
    if (c >= 'A' && c <= 'F') { return c - 'A' + 10; }
    return -1;
}

// "rrggbb" or "#rrggbb", and Lospec's occasional "rrggbbaa". Anything else is
// not a colour and says so.
bool parseHexColor(const std::string& raw, ls::Color* out) {
    std::string text = trimmed(raw);
    if (!text.empty() && text[0] == '#') {
        text = text.substr(1);
    }
    if (text.size() != 6 && text.size() != 8) {
        return false;
    }
    uint8_t bytes[4] = { 0, 0, 0, 255 };
    for (size_t i = 0; i < text.size() / 2; ++i) {
        const int high = hexValue(text[i * 2]);
        const int low  = hexValue(text[i * 2 + 1]);
        if (high < 0 || low < 0) {
            return false;
        }
        bytes[i] = static_cast<uint8_t>(high * 16 + low);
    }
    *out = { bytes[0], bytes[1], bytes[2], bytes[3] };
    return true;
}

// A whole number in 0..255, or nothing. Out of range is refused rather than
// clamped: a colour somebody did not mean is worse than one that is missing.
bool parseByte(const std::string& text, uint8_t* out) {
    if (text.empty() || text.size() > 3) {
        return false;
    }
    int value = 0;
    for (char c : text) {
        if (c < '0' || c > '9') {
            return false;
        }
        value = value * 10 + (c - '0');
    }
    if (value > 255) {
        return false;
    }
    *out = static_cast<uint8_t>(value);
    return true;
}

// A .gpl colour line: "R G B" then optionally a name. Whitespace-separated,
// with any run of spaces or tabs counting as one separator, because the files
// in the wild are column-aligned by hand.
bool parseGplLine(const std::string& raw, ls::Color* color, std::string* label) {
    const std::string text = trimmed(raw);
    if (text.empty() || text[0] == '#') {
        return false;
    }
    std::vector<std::string> fields;
    std::string current;
    for (char c : text) {
        if (c == ' ' || c == '\t') {
            if (!current.empty()) {
                fields.push_back(current);
                current.clear();
            }
            // The name is everything after the third field, spaces and all.
            if (fields.size() == 3) {
                break;
            }
        } else {
            current.push_back(c);
        }
    }
    if (!current.empty() && fields.size() < 3) {
        fields.push_back(current);
    }
    if (fields.size() < 3) {
        return false;
    }
    uint8_t r = 0, g = 0, b = 0;
    if (!parseByte(fields[0], &r) || !parseByte(fields[1], &g) || !parseByte(fields[2], &b)) {
        return false;
    }
    *color = { r, g, b, 255 };

    // The name: what follows the third number, trimmed.
    size_t at = 0;
    int seen = 0;
    while (at < text.size() && seen < 3) {
        while (at < text.size() && (text[at] == ' ' || text[at] == '\t')) { ++at; }
        while (at < text.size() && text[at] != ' ' && text[at] != '\t') { ++at; }
        ++seen;
    }
    *label = trimmed(text.substr(at));
    return true;
}

} // namespace

bool parsePalette(const std::string& text, PaletteFile* out, std::string* error) {
    if (out == nullptr) {
        return false;
    }
    *out = PaletteFile{};

    const std::vector<std::string> all = lines(text);
    const bool gpl = !all.empty() && trimmed(all.front()).rfind("GIMP Palette", 0) == 0;
    // JASC's header is "JASC-PAL", a version, and a count; after it, the
    // colour lines read exactly as .gpl's do.
    const bool jasc = !all.empty() && trimmed(all.front()).rfind("JASC-PAL", 0) == 0;

    PaletteFile file;
    size_t skipped = 0;
    size_t lineNumber = 0;
    for (const std::string& raw : all) {
        ++lineNumber;
        if (file.entries.size() >= kMaxPaletteEntries) {
            break;
        }
        const std::string line = trimmed(raw);
        if (line.empty()) {
            continue;
        }
        if (jasc) {
            if (lineNumber <= 3) {
                continue;                 // the header: name, version, count
            }
            PaletteEntry entry;
            if (!parseGplLine(line, &entry.color, &entry.label)) {
                ++skipped;
                continue;
            }
            entry.label.clear();
            entry.role = static_cast<ls::ColorRole>(file.entries.size());
            file.entries.push_back(entry);
            continue;
        }
        if (gpl) {
            // Header lines carry no colour. Name: is worth keeping.
            if (line.rfind("GIMP Palette", 0) == 0 || line.rfind("Columns:", 0) == 0) {
                continue;
            }
            if (line.rfind("Name:", 0) == 0) {
                file.name = trimmed(line.substr(5));
                continue;
            }
            if (line[0] == '#') {
                continue;
            }
            PaletteEntry entry;
            if (!parseGplLine(line, &entry.color, &entry.label)) {
                ++skipped;
                continue;
            }
            entry.role = static_cast<ls::ColorRole>(file.entries.size());
            file.entries.push_back(entry);
        } else {
            if (line[0] == ';' || (line[0] == '/' && line.size() > 1 && line[1] == '/')) {
                continue;             // a comment, in the two styles seen
            }
            PaletteEntry entry;
            if (!parseHexColor(line, &entry.color)) {
                ++skipped;
                continue;
            }
            entry.role = static_cast<ls::ColorRole>(file.entries.size());
            file.entries.push_back(entry);
        }
    }

    if (file.entries.empty()) {
        if (error != nullptr) {
            *error = skipped > 0
                ? "nothing in the file was a colour this understands"
                : "the file is empty";
        }
        return false;
    }
    *out = std::move(file);
    return true;
}

std::string toGpl(const std::string& name, const std::vector<PaletteEntry>& entries) {
    std::string out = "GIMP Palette\n";
    out += "Name: " + (name.empty() ? std::string("palette") : name) + "\n";
    out += "Columns: 8\n#\n";
    char buffer[64];
    for (const PaletteEntry& entry : entries) {
        std::snprintf(buffer, sizeof(buffer), "%3u %3u %3u\t",
                      entry.color.r, entry.color.g, entry.color.b);
        out += buffer;
        // A name with a newline in it would be two lines; keep it one.
        for (char c : entry.label) {
            out.push_back(c == '\n' || c == '\r' ? ' ' : c);
        }
        if (entry.label.empty()) {
            out += "slot " + std::to_string(entry.role);
        }
        out += '\n';
    }
    return out;
}

std::string toHex(const std::vector<PaletteEntry>& entries) {
    std::string out;
    char buffer[16];
    for (const PaletteEntry& entry : entries) {
        std::snprintf(buffer, sizeof(buffer), "%02x%02x%02x\n",
                      entry.color.r, entry.color.g, entry.color.b);
        out += buffer;
    }
    return out;
}

std::string toJasc(const std::vector<PaletteEntry>& entries) {
    std::string out = "JASC-PAL\r\n0100\r\n" + std::to_string(entries.size()) + "\r\n";
    char buffer[32];
    for (const PaletteEntry& entry : entries) {
        std::snprintf(buffer, sizeof(buffer), "%u %u %u\r\n",
                      entry.color.r, entry.color.g, entry.color.b);
        out += buffer;
    }
    return out;
}

std::vector<uint8_t> toAct(const std::vector<PaletteEntry>& entries) {
    std::vector<uint8_t> out(772, 0);
    const size_t count = std::min<size_t>(entries.size(), 256);
    for (size_t i = 0; i < count; ++i) {
        out[i * 3] = entries[i].color.r;
        out[i * 3 + 1] = entries[i].color.g;
        out[i * 3 + 2] = entries[i].color.b;
    }
    // How many are used, then "no transparent entry", both big-endian.
    out[768] = static_cast<uint8_t>(count >> 8);
    out[769] = static_cast<uint8_t>(count & 0xFF);
    out[770] = 0xFF;
    out[771] = 0xFF;
    return out;
}

bool parseAct(const std::vector<uint8_t>& bytes, PaletteFile* out, std::string* error) {
    if (out == nullptr) {
        return false;
    }
    if (bytes.size() != 768 && bytes.size() != 772) {
        if (error != nullptr) {
            *error = "a colour table is 768 or 772 bytes, and this is " +
                     std::to_string(bytes.size());
        }
        return false;
    }
    size_t count = 256;
    if (bytes.size() == 772) {
        const size_t used = static_cast<size_t>(bytes[768]) << 8 | bytes[769];
        // Zero, or more than there are, means the whole table.
        if (used > 0 && used <= 256) {
            count = used;
        }
    }
    PaletteFile file;
    for (size_t i = 0; i < count; ++i) {
        PaletteEntry entry;
        entry.role = static_cast<ls::ColorRole>(i);
        entry.color = { bytes[i * 3], bytes[i * 3 + 1], bytes[i * 3 + 2], 255 };
        file.entries.push_back(entry);
    }
    *out = std::move(file);
    return true;
}

bool paletteFromImage(const std::vector<uint8_t>& bytes, PaletteFile* out, std::string* error) {
    if (out == nullptr) {
        return false;
    }
    ls::RasterBuffer image;
    if (!decodeImage(bytes, &image, error)) {
        return false;
    }
    PaletteFile file;
    for (uint32_t y = 0; y < image.height; ++y) {
        const uint8_t* row = image.row(y);
        for (uint32_t x = 0; x < image.width; ++x) {
            const uint8_t* p = row + static_cast<size_t>(x) * 4u;
            if (p[3] == 0) {
                continue;
            }
            const ls::Color colour { p[0], p[1], p[2], p[3] };
            bool known = false;
            for (const PaletteEntry& entry : file.entries) {
                if (entry.color.r == colour.r && entry.color.g == colour.g &&
                    entry.color.b == colour.b && entry.color.a == colour.a) {
                    known = true;
                    break;
                }
            }
            if (known) {
                continue;
            }
            if (file.entries.size() >= kMaxPaletteEntries) {
                if (error != nullptr) {
                    *error = "the image has more than " + std::to_string(kMaxPaletteEntries) +
                             " colours, more than a palette holds";
                }
                return false;
            }
            PaletteEntry entry;
            entry.role = static_cast<ls::ColorRole>(file.entries.size());
            entry.color = colour;
            file.entries.push_back(entry);
        }
    }
    if (file.entries.empty()) {
        if (error != nullptr) { *error = "the image has no colours in it"; }
        return false;
    }
    *out = std::move(file);
    return true;
}

bool applyPaletteFile(Document& doc, ls::SpriteId sprite, const PaletteFile& file,
                      int* dropped) {
    if (file.entries.empty() || !ensurePalette(doc, sprite)) {
        return false;
    }
    const ls::PaletteId palette = paletteFor(doc, sprite);
    const std::vector<PaletteEntry> before = paletteEntries(doc, palette);

    doc.beginAction("Load palette");

    // Roles the file does not provide are removed; every layer that painted
    // through one falls back to its literal. Counted so the interface can say.
    int lost = 0;
    for (const PaletteEntry& old : before) {
        if (old.role >= file.entries.size()) {
            if (paletteRoleInUse(doc, old.role)) {
                ++lost;
            }
            removePaletteEntry(doc, palette, old.role);
        }
    }
    for (const PaletteEntry& entry : file.entries) {
        if (!setPaletteEntry(doc, palette, entry.role, entry.color)) {
            doc.abandonAction();
            return false;
        }
        setPaletteLabel(doc, palette, entry.role, entry.label);
    }
    if (!file.name.empty()) {
        renamePalette(doc, palette, file.name);
    }
    // Shown in the file's order, whatever order the slots had before.
    std::vector<ls::ColorRole> order;
    for (const PaletteEntry& entry : file.entries) {
        order.push_back(entry.role);
    }
    setSlotOrder(doc, palette, order);

    doc.endAction();
    if (dropped != nullptr) {
        *dropped = lost;
    }
    return true;
}

bool importPaletteFile(Document& doc, ls::SpriteId sprite, const std::string& path,
                       int* dropped, std::string* error) {
    std::vector<uint8_t> bytes;
    if (!readFile(path, bytes, error)) {
        return false;
    }
    // By content where the content says -- an image, a colour table -- and
    // as text otherwise, which parsePalette sorts out itself.
    PaletteFile file;
    const bool parsed = looksLikeImageName(path)
        ? paletteFromImage(bytes, &file, error)
        : hasExtension(path, ".act")
            ? parseAct(bytes, &file, error)
            : parsePalette(std::string(bytes.begin(), bytes.end()), &file, error);
    if (!parsed) {
        return false;
    }
    if (!applyPaletteFile(doc, sprite, file, dropped)) {
        if (error != nullptr) { *error = "the palette could not be applied"; }
        return false;
    }
    return true;
}

bool exportPaletteFile(Document& doc, ls::SpriteId sprite, const std::string& path,
                       std::string* error) {
    const std::vector<PaletteEntry> entries = paletteEntries(doc, paletteFor(doc, sprite));
    if (entries.empty()) {
        if (error != nullptr) { *error = "the document has no palette"; }
        return false;
    }
    // The extension decides the format, .gpl unless it says otherwise.
    if (hasExtension(path, ".act")) {
        return writeFileAtomic(path, toAct(entries), error);
    }
    const std::string text = hasExtension(path, ".hex") ? toHex(entries)
                           : hasExtension(path, ".pal") ? toJasc(entries)
                                                        : toGpl(fileStem(path), entries);
    return writeFileAtomic(path, std::vector<uint8_t>(text.begin(), text.end()), error);
}

} // namespace fast
