// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

#include "app/vector_icon.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <map>

namespace fast {

namespace {

constexpr float kPi = 3.14159265358979f;

using Point = std::pair<float, float>;

// --------------------------------------------------------------- numbers ----

struct Cursor {
    const std::string& text;
    size_t at = 0;

    bool done() const { return at >= text.size(); }

    void skipSeparators() {
        while (at < text.size() &&
               (text[at] == ' ' || text[at] == ',' || text[at] == '\t' || text[at] == '\n' ||
                text[at] == '\r')) {
            ++at;
        }
    }

    // A number as SVG writes them, where one may run straight into the next:
    // "1.5.5" is 1.5 and .5, and "3-2" is 3 and -2.
    bool number(float* out) {
        skipSeparators();
        const size_t start = at;
        size_t i = at;
        if (i < text.size() && (text[i] == '+' || text[i] == '-')) {
            ++i;
        }
        bool digits = false;
        while (i < text.size() && text[i] >= '0' && text[i] <= '9') {
            ++i;
            digits = true;
        }
        if (i < text.size() && text[i] == '.') {
            ++i;
            while (i < text.size() && text[i] >= '0' && text[i] <= '9') {
                ++i;
                digits = true;
            }
        }
        if (!digits) {
            return false;
        }
        if (i < text.size() && (text[i] == 'e' || text[i] == 'E')) {
            size_t j = i + 1;
            if (j < text.size() && (text[j] == '+' || text[j] == '-')) {
                ++j;
            }
            if (j < text.size() && text[j] >= '0' && text[j] <= '9') {
                while (j < text.size() && text[j] >= '0' && text[j] <= '9') {
                    ++j;
                }
                i = j;
            }
        }
        *out = static_cast<float>(std::strtod(text.substr(start, i - start).c_str(), nullptr));
        at = i;
        return true;
    }

    // An arc's flag: one character, 0 or 1, which may be written with no
    // separator before the next number ("011.704" is 0, 1, 1.704).
    bool flag(bool* out) {
        skipSeparators();
        if (at < text.size() && (text[at] == '0' || text[at] == '1')) {
            *out = text[at] == '1';
            ++at;
            return true;
        }
        return false;
    }
};

// ---------------------------------------------------------------- curves ----

void addCubic(std::vector<Point>* out, Point p0, Point p1, Point p2, Point p3) {
    constexpr int kSteps = 14;
    for (int i = 1; i <= kSteps; ++i) {
        const float t = static_cast<float>(i) / kSteps;
        const float u = 1.f - t;
        out->push_back({ u * u * u * p0.first + 3 * u * u * t * p1.first +
                             3 * u * t * t * p2.first + t * t * t * p3.first,
                         u * u * u * p0.second + 3 * u * u * t * p1.second +
                             3 * u * t * t * p2.second + t * t * t * p3.second });
    }
}

void addQuadratic(std::vector<Point>* out, Point p0, Point p1, Point p2) {
    constexpr int kSteps = 10;
    for (int i = 1; i <= kSteps; ++i) {
        const float t = static_cast<float>(i) / kSteps;
        const float u = 1.f - t;
        out->push_back({ u * u * p0.first + 2 * u * t * p1.first + t * t * p2.first,
                         u * u * p0.second + 2 * u * t * p1.second + t * t * p2.second });
    }
}

// An elliptical arc by its endpoints (the SVG spec's F.6.5), as points.
void addArc(std::vector<Point>* out, Point from, float rx, float ry, float rotation, bool large,
            bool sweep, Point to) {
    if (from == to) {
        return;
    }
    rx = std::fabs(rx);
    ry = std::fabs(ry);
    if (rx < 1e-6f || ry < 1e-6f) {
        out->push_back(to);                 // a zero radius is a straight line
        return;
    }
    const float phi = rotation * kPi / 180.f;
    const float cosPhi = std::cos(phi);
    const float sinPhi = std::sin(phi);
    const float dx = (from.first - to.first) * 0.5f;
    const float dy = (from.second - to.second) * 0.5f;
    const float x1 = cosPhi * dx + sinPhi * dy;
    const float y1 = -sinPhi * dx + cosPhi * dy;
    // Radii too small to reach are grown until they just do.
    const float lambda = (x1 * x1) / (rx * rx) + (y1 * y1) / (ry * ry);
    if (lambda > 1.f) {
        const float scale = std::sqrt(lambda);
        rx *= scale;
        ry *= scale;
    }
    const float numerator = rx * rx * ry * ry - rx * rx * y1 * y1 - ry * ry * x1 * x1;
    const float denominator = rx * rx * y1 * y1 + ry * ry * x1 * x1;
    float coefficient = denominator > 0.f ? std::sqrt(std::max(0.f, numerator / denominator)) : 0.f;
    if (large == sweep) {
        coefficient = -coefficient;
    }
    const float cxp = coefficient * (rx * y1 / ry);
    const float cyp = coefficient * -(ry * x1 / rx);
    const float cx = cosPhi * cxp - sinPhi * cyp + (from.first + to.first) * 0.5f;
    const float cy = sinPhi * cxp + cosPhi * cyp + (from.second + to.second) * 0.5f;

    const auto angle = [](float ux, float uy, float vx, float vy) {
        const float dot = ux * vx + uy * vy;
        const float length = std::sqrt(ux * ux + uy * uy) * std::sqrt(vx * vx + vy * vy);
        float a = std::acos(std::clamp(length > 0.f ? dot / length : 1.f, -1.f, 1.f));
        if (ux * vy - uy * vx < 0.f) {
            a = -a;
        }
        return a;
    };
    const float start = angle(1.f, 0.f, (x1 - cxp) / rx, (y1 - cyp) / ry);
    float delta = angle((x1 - cxp) / rx, (y1 - cyp) / ry, (-x1 - cxp) / rx, (-y1 - cyp) / ry);
    if (!sweep && delta > 0.f) {
        delta -= 2.f * kPi;
    } else if (sweep && delta < 0.f) {
        delta += 2.f * kPi;
    }
    const int steps = std::max(4, static_cast<int>(std::ceil(std::fabs(delta) / (kPi / 14.f))));
    for (int i = 1; i <= steps; ++i) {
        const float a = start + delta * static_cast<float>(i) / static_cast<float>(steps);
        const float ex = rx * std::cos(a);
        const float ey = ry * std::sin(a);
        out->push_back({ cosPhi * ex - sinPhi * ey + cx, sinPhi * ex + cosPhi * ey + cy });
    }
    out->back() = to;                       // no drift at the end
}

// ----------------------------------------------------------------- paths ----

// One <path d="...">: its sub-paths as strokes.
void parsePath(const std::string& d, std::vector<IconStroke>* strokes) {
    Cursor cursor { d };
    Point current { 0.f, 0.f };
    Point start { 0.f, 0.f };
    Point lastControl { 0.f, 0.f };
    char previous = 0;                       // for S and T: what the last command was
    IconStroke stroke;
    char command = 0;

    const auto flush = [&](bool closed) {
        if (stroke.points.size() >= 2 || (stroke.points.size() == 1 && !closed)) {
            stroke.closed = closed;
            if (stroke.points.size() == 1) {
                // A lone point (M x y h.01 makes two; a bare M makes one) is a dot.
                stroke.points.push_back(stroke.points.front());
            }
            if (strokes->size() < kMaxIconStrokes) {
                strokes->push_back(stroke);
            }
        }
        stroke = IconStroke{};
    };

    while (true) {
        cursor.skipSeparators();
        if (cursor.done()) {
            break;
        }
        const char c = d[cursor.at];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) {
            command = c;
            ++cursor.at;
            if (command == 'Z' || command == 'z') {
                if (!stroke.points.empty()) {
                    flush(true);
                }
                current = start;
                previous = 'z';
                continue;
            }
        } else if (command == 0 || command == 'Z' || command == 'z') {
            break;                           // numbers with no command
        }
        const bool relative = command >= 'a' && command <= 'z';
        const char upper = static_cast<char>(relative ? command - 'a' + 'A' : command);
        const Point origin = relative ? current : Point{ 0.f, 0.f };
        float v[7] = {};
        bool ok = true;

        const auto read = [&](int count) {
            for (int i = 0; i < count && ok; ++i) {
                ok = cursor.number(&v[i]);
            }
            return ok;
        };
        const auto begin = [&](Point p) {
            if (stroke.points.size() > 0) {
                flush(false);
            }
            stroke.points.push_back(p);
            start = p;
        };
        const auto ensureStarted = [&]() {
            if (stroke.points.empty()) {
                stroke.points.push_back(current);
            }
        };

        if (upper == 'M') {
            if (!read(2)) { break; }
            const Point p { origin.first + v[0], origin.second + v[1] };
            begin(p);
            current = p;
            command = relative ? 'l' : 'L';       // further pairs are line-tos
            previous = 'M';
        } else if (upper == 'L') {
            if (!read(2)) { break; }
            ensureStarted();
            current = { origin.first + v[0], origin.second + v[1] };
            stroke.points.push_back(current);
            previous = 'L';
        } else if (upper == 'H') {
            if (!read(1)) { break; }
            ensureStarted();
            current.first = (relative ? current.first : 0.f) + v[0];
            stroke.points.push_back(current);
            previous = 'H';
        } else if (upper == 'V') {
            if (!read(1)) { break; }
            ensureStarted();
            current.second = (relative ? current.second : 0.f) + v[0];
            stroke.points.push_back(current);
            previous = 'V';
        } else if (upper == 'C') {
            if (!read(6)) { break; }
            ensureStarted();
            const Point p1 { origin.first + v[0], origin.second + v[1] };
            const Point p2 { origin.first + v[2], origin.second + v[3] };
            const Point p3 { origin.first + v[4], origin.second + v[5] };
            addCubic(&stroke.points, current, p1, p2, p3);
            lastControl = p2;
            current = p3;
            previous = 'C';
        } else if (upper == 'S') {
            if (!read(4)) { break; }
            ensureStarted();
            const Point p1 = (previous == 'C' || previous == 'S')
                ? Point{ 2 * current.first - lastControl.first, 2 * current.second - lastControl.second }
                : current;
            const Point p2 { origin.first + v[0], origin.second + v[1] };
            const Point p3 { origin.first + v[2], origin.second + v[3] };
            addCubic(&stroke.points, current, p1, p2, p3);
            lastControl = p2;
            current = p3;
            previous = 'S';
        } else if (upper == 'Q') {
            if (!read(4)) { break; }
            ensureStarted();
            const Point p1 { origin.first + v[0], origin.second + v[1] };
            const Point p2 { origin.first + v[2], origin.second + v[3] };
            addQuadratic(&stroke.points, current, p1, p2);
            lastControl = p1;
            current = p2;
            previous = 'Q';
        } else if (upper == 'T') {
            if (!read(2)) { break; }
            ensureStarted();
            const Point p1 = (previous == 'Q' || previous == 'T')
                ? Point{ 2 * current.first - lastControl.first, 2 * current.second - lastControl.second }
                : current;
            const Point p2 { origin.first + v[0], origin.second + v[1] };
            addQuadratic(&stroke.points, current, p1, p2);
            lastControl = p1;
            current = p2;
            previous = 'T';
        } else if (upper == 'A') {
            float rx = 0.f, ry = 0.f, rotation = 0.f;
            bool large = false, sweep = false;
            float x = 0.f, y = 0.f;
            if (!cursor.number(&rx) || !cursor.number(&ry) || !cursor.number(&rotation) ||
                !cursor.flag(&large) || !cursor.flag(&sweep) || !cursor.number(&x) ||
                !cursor.number(&y)) {
                break;
            }
            ensureStarted();
            const Point to { origin.first + x, origin.second + y };
            addArc(&stroke.points, current, rx, ry, rotation, large, sweep, to);
            current = to;
            previous = 'A';
        } else {
            break;                           // a command this reader does not know
        }
        if (stroke.points.size() > kMaxStrokePoints) {
            break;
        }
    }
    if (!stroke.points.empty()) {
        flush(false);
    }
}

// --------------------------------------------------------------- shapes -----

IconStroke ellipse(float cx, float cy, float rx, float ry) {
    IconStroke stroke;
    constexpr int kSteps = 56;
    for (int i = 0; i < kSteps; ++i) {
        const float a = 2.f * kPi * static_cast<float>(i) / kSteps;
        stroke.points.push_back({ cx + rx * std::cos(a), cy + ry * std::sin(a) });
    }
    stroke.closed = true;
    return stroke;
}

IconStroke roundedRect(float x, float y, float w, float h, float rx, float ry) {
    IconStroke stroke;
    rx = std::min(rx, w * 0.5f);
    ry = std::min(ry, h * 0.5f);
    if (rx <= 0.f || ry <= 0.f) {
        stroke.points = { { x, y }, { x + w, y }, { x + w, y + h }, { x, y + h } };
    } else {
        const auto corner = [&](float cx, float cy, float from) {
            for (int i = 0; i <= 6; ++i) {
                const float a = from + (kPi * 0.5f) * static_cast<float>(i) / 6.f;
                stroke.points.push_back({ cx + rx * std::cos(a), cy + ry * std::sin(a) });
            }
        };
        corner(x + w - rx, y + ry, -kPi * 0.5f);      // top right
        corner(x + w - rx, y + h - ry, 0.f);          // bottom right
        corner(x + rx, y + h - ry, kPi * 0.5f);       // bottom left
        corner(x + rx, y + ry, kPi);                  // top left
    }
    stroke.closed = true;
    return stroke;
}

// -------------------------------------------------------------- the tags ----

struct Tag {
    std::string name;
    std::map<std::string, std::string> attributes;
};

float attribute(const Tag& tag, const char* name, float fallback = 0.f) {
    const auto found = tag.attributes.find(name);
    return found == tag.attributes.end()
               ? fallback
               : static_cast<float>(std::strtod(found->second.c_str(), nullptr));
}

// The tags of a file, in order: a name and its attributes. Comments and
// declarations are skipped; text between tags means nothing here.
std::vector<Tag> tagsOf(const std::string& svg) {
    std::vector<Tag> tags;
    size_t at = 0;
    while (at < svg.size() && tags.size() < 1024) {
        const size_t open = svg.find('<', at);
        if (open == std::string::npos) {
            break;
        }
        if (svg.compare(open, 4, "<!--") == 0) {
            const size_t end = svg.find("-->", open + 4);
            at = end == std::string::npos ? svg.size() : end + 3;
            continue;
        }
        const size_t close = svg.find('>', open);
        if (close == std::string::npos) {
            break;
        }
        at = close + 1;
        if (svg[open + 1] == '/' || svg[open + 1] == '?' || svg[open + 1] == '!') {
            continue;
        }
        Tag tag;
        size_t i = open + 1;
        while (i < close && svg[i] != ' ' && svg[i] != '\n' && svg[i] != '\t' && svg[i] != '\r' &&
               svg[i] != '/') {
            tag.name += svg[i++];
        }
        while (i < close) {
            while (i < close && (svg[i] == ' ' || svg[i] == '\n' || svg[i] == '\t' ||
                                 svg[i] == '\r' || svg[i] == '/')) {
                ++i;
            }
            std::string key;
            while (i < close && svg[i] != '=' && svg[i] != ' ' && svg[i] != '/') {
                key += svg[i++];
            }
            if (i < close && svg[i] == '=') {
                ++i;
                if (i < close && (svg[i] == '"' || svg[i] == '\'')) {
                    const char quote = svg[i++];
                    std::string value;
                    while (i < close && svg[i] != quote) {
                        value += svg[i++];
                    }
                    ++i;
                    if (!key.empty()) {
                        tag.attributes[key] = value;
                    }
                }
            }
        }
        tags.push_back(std::move(tag));
    }
    return tags;
}

} // namespace

bool parseVectorIcon(const std::string& svg, VectorIcon* out, std::string* error) {
    const auto fail = [&](const char* why) {
        if (error != nullptr) {
            *error = why;
        }
        return false;
    };
    if (out == nullptr) {
        return fail("nowhere to put the icon");
    }
    if (svg.size() > kMaxIconBytes) {
        return fail("the file is too large for an icon");
    }
    VectorIcon icon;
    for (const Tag& tag : tagsOf(svg)) {
        if (icon.strokes.size() >= kMaxIconStrokes) {
            break;
        }
        if (tag.name == "svg") {
            const auto box = tag.attributes.find("viewBox");
            if (box != tag.attributes.end()) {
                Cursor cursor { box->second };
                float x = 0.f, y = 0.f, w = 0.f, h = 0.f;
                if (cursor.number(&x) && cursor.number(&y) && cursor.number(&w) &&
                    cursor.number(&h) && w > 0.f && h > 0.f && w <= 4096.f && h <= 4096.f) {
                    icon.width = w;
                    icon.height = h;
                }
            }
        } else if (tag.name == "path") {
            const auto d = tag.attributes.find("d");
            if (d != tag.attributes.end()) {
                parsePath(d->second, &icon.strokes);
            }
        } else if (tag.name == "circle") {
            const float r = attribute(tag, "r");
            if (r > 0.f) {
                icon.strokes.push_back(ellipse(attribute(tag, "cx"), attribute(tag, "cy"), r, r));
            }
        } else if (tag.name == "ellipse") {
            const float rx = attribute(tag, "rx");
            const float ry = attribute(tag, "ry");
            if (rx > 0.f && ry > 0.f) {
                icon.strokes.push_back(ellipse(attribute(tag, "cx"), attribute(tag, "cy"), rx, ry));
            }
        } else if (tag.name == "rect") {
            const float w = attribute(tag, "width");
            const float h = attribute(tag, "height");
            if (w > 0.f && h > 0.f) {
                const float rx = attribute(tag, "rx", attribute(tag, "ry"));
                const float ry = attribute(tag, "ry", rx);
                icon.strokes.push_back(
                    roundedRect(attribute(tag, "x"), attribute(tag, "y"), w, h, rx, ry));
            }
        } else if (tag.name == "line") {
            IconStroke stroke;
            stroke.points = { { attribute(tag, "x1"), attribute(tag, "y1") },
                              { attribute(tag, "x2"), attribute(tag, "y2") } };
            icon.strokes.push_back(stroke);
        } else if (tag.name == "polyline" || tag.name == "polygon") {
            const auto list = tag.attributes.find("points");
            if (list != tag.attributes.end()) {
                IconStroke stroke;
                Cursor cursor { list->second };
                float x = 0.f, y = 0.f;
                while (stroke.points.size() < kMaxStrokePoints && cursor.number(&x) &&
                       cursor.number(&y)) {
                    stroke.points.push_back({ x, y });
                }
                stroke.closed = tag.name == "polygon";
                if (stroke.points.size() >= 2) {
                    icon.strokes.push_back(stroke);
                }
            }
        }
    }
    if (icon.strokes.empty()) {
        return fail("there is nothing to draw in it");
    }
    *out = icon;
    return true;
}

void iconBounds(const VectorIcon& icon, float* minX, float* minY, float* maxX, float* maxY) {
    float lowX = 1e9f, lowY = 1e9f, highX = -1e9f, highY = -1e9f;
    for (const IconStroke& stroke : icon.strokes) {
        for (const Point& p : stroke.points) {
            lowX = std::min(lowX, p.first);
            lowY = std::min(lowY, p.second);
            highX = std::max(highX, p.first);
            highY = std::max(highY, p.second);
        }
    }
    if (minX != nullptr) { *minX = lowX; }
    if (minY != nullptr) { *minY = lowY; }
    if (maxX != nullptr) { *maxX = highX; }
    if (maxY != nullptr) { *maxY = highY; }
}

} // namespace fast
