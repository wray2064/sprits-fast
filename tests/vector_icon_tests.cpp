// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

// vector_icon_tests.cpp — an icon read from an SVG as strokes.

#include "app/file_io.h"
#include "app/vector_icon.h"

#include <cmath>
#include <cstdio>
#include <string>

#ifndef ICON_DIR
#error "ICON_DIR is set by CMake to assets/icons/lucide"
#endif

static int failures = 0;

#define CHECK(...)                                                            \
    do {                                                                      \
        if (!(__VA_ARGS__)) {                                                 \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #__VA_ARGS__);\
            ++failures;                                                       \
        }                                                                     \
    } while (0)

namespace {

bool near(float a, float b, float tolerance = 0.05f) {
    return std::fabs(a - b) <= tolerance;
}

std::string svgOf(const std::string& body) {
    return "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 24 24\">" + body + "</svg>";
}

fast::VectorIcon parsed(const std::string& body) {
    fast::VectorIcon icon;
    std::string error;
    const bool ok = fast::parseVectorIcon(svgOf(body), &icon, &error);
    CHECK(ok);
    return icon;
}

void testPathCommands() {
    // Absolute and relative lines, implicit repeats, H and V, and Z closing.
    fast::VectorIcon icon = parsed("<path d=\"M2 2 L10 2 10 10 h-8 v-8z\"/>");
    CHECK(icon.strokes.size() == 1 && icon.strokes[0].closed);
    CHECK(icon.strokes[0].points.size() == 5);
    CHECK(near(icon.strokes[0].points[2].first, 10.f) && near(icon.strokes[0].points[2].second, 10.f));
    CHECK(near(icon.strokes[0].points[3].first, 2.f) && near(icon.strokes[0].points[3].second, 10.f));

    // A moveto followed by more pairs draws lines; two movetos are two strokes.
    icon = parsed("<path d=\"m1 1 5 5 M10 10 l3 0\"/>");
    CHECK(icon.strokes.size() == 2);
    CHECK(icon.strokes[0].points.size() == 2 && near(icon.strokes[0].points[1].first, 6.f));
    CHECK(near(icon.strokes[1].points[1].first, 13.f));

    // Numbers that run into one another: "1.5.5" and "3-2".
    icon = parsed("<path d=\"M0 0 l1.5.5 3-2\"/>");
    CHECK(icon.strokes.size() == 1 && icon.strokes[0].points.size() == 3);
    CHECK(near(icon.strokes[0].points[1].first, 1.5f) && near(icon.strokes[0].points[1].second, 0.5f));
    CHECK(near(icon.strokes[0].points[2].first, 4.5f) && near(icon.strokes[0].points[2].second, -1.5f));

    // A cubic ends where it says; so do a smooth one, a quadratic and its mirror.
    icon = parsed("<path d=\"M0 0 C0 10 10 10 10 0 S20 -10 20 0\"/>");
    CHECK(near(icon.strokes[0].points.back().first, 20.f) && near(icon.strokes[0].points.back().second, 0.f));
    icon = parsed("<path d=\"M0 0 Q5 10 10 0 T20 0\"/>");
    CHECK(near(icon.strokes[0].points.back().first, 20.f) && near(icon.strokes[0].points.back().second, 0.f));

    // A dot: a line of 0.01 is kept, and so is a lone moveto.
    icon = parsed("<path d=\"M3 3h.01\"/>");
    CHECK(icon.strokes.size() == 1 && icon.strokes[0].points.size() == 2);
}

void testArcs() {
    // A half circle: from (0,12) over the top to (24,12), radius 12.
    fast::VectorIcon icon = parsed("<path d=\"M0 12a12 12 0 0 1 24 0\"/>");
    float lowX, lowY, highX, highY;
    fast::iconBounds(icon, &lowX, &lowY, &highX, &highY);
    CHECK(near(lowY, 0.f, 0.1f) && near(lowX, 0.f) && near(highX, 24.f));
    CHECK(near(icon.strokes[0].points.back().first, 24.f) && near(icon.strokes[0].points.back().second, 12.f));

    // The same with the other sweep goes under.
    icon = parsed("<path d=\"M0 12a12 12 0 0 0 24 0\"/>");
    fast::iconBounds(icon, &lowX, &lowY, &highX, &highY);
    CHECK(near(highY, 24.f, 0.1f));

    // Flags run into the next number with no separator, as Lucide's pen-tool
    // writes them: "a4 4 0 00-2.756-3.266" and "a1.205 1.205 0 011.704 0".
    icon = parsed("<path d=\"M10 10a1.205 1.205 0 011.704 0\"/>");
    CHECK(icon.strokes.size() == 1 && near(icon.strokes[0].points.back().first, 11.704f));
    icon = parsed("<path d=\"M10 10a4 4 0 00-2.756-3.266\"/>");
    CHECK(near(icon.strokes[0].points.back().first, 7.244f) && near(icon.strokes[0].points.back().second, 6.734f));

    // Radii too small to reach are grown; a zero radius is a straight line.
    icon = parsed("<path d=\"M0 0a1 1 0 0 1 20 0\"/>");
    CHECK(near(icon.strokes[0].points.back().first, 20.f));
    icon = parsed("<path d=\"M0 0a0 0 0 0 1 20 0\"/>");
    CHECK(icon.strokes[0].points.size() == 2);
}

void testShapes() {
    fast::VectorIcon icon = parsed("<circle cx=\"12\" cy=\"12\" r=\"10\"/>");
    float lowX, lowY, highX, highY;
    fast::iconBounds(icon, &lowX, &lowY, &highX, &highY);
    CHECK(icon.strokes[0].closed && near(lowX, 2.f) && near(highX, 22.f) && near(lowY, 2.f));

    icon = parsed("<rect width=\"18\" height=\"18\" x=\"3\" y=\"3\" rx=\"2\"/>");
    fast::iconBounds(icon, &lowX, &lowY, &highX, &highY);
    CHECK(icon.strokes[0].closed && near(lowX, 3.f) && near(highX, 21.f) && near(highY, 21.f));
    icon = parsed("<rect width=\"4\" height=\"4\" x=\"15\" y=\"5\"/>");
    CHECK(icon.strokes[0].points.size() == 4);

    icon = parsed("<line x1=\"21\" x2=\"16.65\" y1=\"21\" y2=\"16.65\"/>");
    CHECK(icon.strokes[0].points.size() == 2 && near(icon.strokes[0].points[1].first, 16.65f));

    icon = parsed("<polygon points=\"1,1 5,1 5,5\"/><polyline points=\"0 0 3 3\"/>");
    CHECK(icon.strokes.size() == 2 && icon.strokes[0].closed && !icon.strokes[1].closed);

    icon = parsed("<ellipse cx=\"5\" cy=\"5\" rx=\"4\" ry=\"2\"/>");
    CHECK(icon.strokes.size() == 1);

    // A different viewBox is read.
    fast::VectorIcon wide;
    CHECK(fast::parseVectorIcon("<svg viewBox=\"0 0 16 32\"><line x1=\"0\" y1=\"0\" x2=\"1\" y2=\"1\"/></svg>",
                                &wide, nullptr));
    CHECK(near(wide.width, 16.f) && near(wide.height, 32.f));
}

// Every icon the program ships has to read, and stay inside its square.
void testTheShippedIconsRead() {
    const char* const names[] = {
        "pencil", "eraser", "paint-bucket", "pipette", "square", "circle", "slash", "eye",
        "eye-off", "square-dashed", "circle-dashed", "lasso", "wand", "move", "spray-can",
        "spline", "hand", "zoom-in", "blend", "type", "lasso-select", "pentagon", "pen-tool",
        "slice",
    };
    for (const char* name : names) {
        const std::string path = std::string(ICON_DIR) + "/" + name + ".svg";
        std::vector<uint8_t> bytes;
        std::string error;
        CHECK(fast::readFile(path, bytes, &error));
        fast::VectorIcon icon;
        CHECK(fast::parseVectorIcon(std::string(bytes.begin(), bytes.end()), &icon, &error));
        CHECK(icon.width == 24.f && icon.height == 24.f);
        CHECK(!icon.strokes.empty());
        float lowX, lowY, highX, highY;
        fast::iconBounds(icon, &lowX, &lowY, &highX, &highY);
        if (!(lowX >= 0.f && lowY >= 0.f && highX <= 24.f && highY <= 24.f)) {
            std::printf("  %s is outside its square: %.2f %.2f %.2f %.2f\n", name, lowX, lowY, highX, highY);
        }
        CHECK(lowX >= 0.f && lowY >= 0.f && highX <= 24.f && highY <= 24.f);
        // Every point is a number that can be drawn.
        for (const fast::IconStroke& stroke : icon.strokes) {
            CHECK(stroke.points.size() >= 2);
            for (const auto& p : stroke.points) {
                CHECK(std::isfinite(p.first) && std::isfinite(p.second));
            }
        }
    }
    // The licence travels with them.
    CHECK(fast::fileExists(std::string(ICON_DIR) + "/LICENSE"));
}

// An icon set is somebody else's file.
void testAHostileFileIsSurvivable() {
    fast::VectorIcon icon;
    std::string error;
    CHECK(!fast::parseVectorIcon("", &icon, &error) && !error.empty());
    CHECK(!fast::parseVectorIcon("not svg at all", &icon, &error));
    CHECK(!fast::parseVectorIcon("<svg></svg>", &icon, &error));
    CHECK(!fast::parseVectorIcon(svgOf("<path d=\"\"/>"), &icon, &error));
    CHECK(!fast::parseVectorIcon(svgOf("<path d=\"hello\"/>"), &icon, &error));
    CHECK(!fast::parseVectorIcon(svgOf("<path d=\"M\"/>"), &icon, &error));
    CHECK(!fast::parseVectorIcon(svgOf("<path d=\"M 1\"/>"), &icon, &error));
    CHECK(!fast::parseVectorIcon("<svg><path d=\"M0 0 L1 1\"", &icon, &error));      // unterminated
    CHECK(!fast::parseVectorIcon("<svg>" + std::string(40000, 'x') + "</svg>", &icon, &error));
    CHECK(!fast::parseVectorIcon("<svg></svg>", nullptr, &error));

    // What it does not understand is skipped, and the rest is drawn.
    CHECK(fast::parseVectorIcon(svgOf("<text>hi</text><g><path d=\"M0 0 L5 5\"/></g>"), &icon, nullptr));
    CHECK(icon.strokes.size() == 1);
    CHECK(fast::parseVectorIcon(svgOf("<!-- a comment <path d=\"M0 0 L9 9\"/> --><path d=\"M1 1 L2 2\"/>"),
                                &icon, nullptr));
    CHECK(icon.strokes.size() == 1 && near(icon.strokes[0].points[0].first, 1.f));

    // A path that goes bad half way keeps what it had.
    CHECK(fast::parseVectorIcon(svgOf("<path d=\"M0 0 L5 5 L oops\"/>"), &icon, nullptr));
    CHECK(icon.strokes[0].points.size() == 2);
    // An unknown command stops it there.
    CHECK(fast::parseVectorIcon(svgOf("<path d=\"M0 0 L5 5 X 9 9\"/>"), &icon, nullptr));
    CHECK(icon.strokes[0].points.size() == 2);

    // Absurd numbers do not crash or hang.
    CHECK(fast::parseVectorIcon(svgOf("<path d=\"M1e999 -1e999 L1e308 1e308\"/>"), &icon, nullptr) || true);
    CHECK(fast::parseVectorIcon(svgOf("<path d=\"M0 0 a1e30 1e30 0 1 1 5 5\"/>"), &icon, nullptr) || true);
    CHECK(!fast::parseVectorIcon(svgOf("<circle r=\"-4\"/><rect width=\"0\" height=\"3\"/>"), &icon, nullptr));

    // Bounded in strokes and in points.
    std::string many;
    for (int i = 0; i < 700; ++i) {
        many += "<path d=\"M0 0 L1 1\"/>";
    }
    CHECK(fast::parseVectorIcon(svgOf(many), &icon, nullptr));
    CHECK(icon.strokes.size() <= fast::kMaxIconStrokes);
    std::string longPath = "<path d=\"M0 0";
    for (int i = 0; i < 3000; ++i) {
        longPath += " L1 1";
    }
    longPath += "\"/>";
    CHECK(fast::parseVectorIcon(svgOf(longPath), &icon, nullptr));
    CHECK(icon.strokes[0].points.size() <= fast::kMaxStrokePoints + 16);
}

} // namespace

int main() {
    testPathCommands();
    testArcs();
    testShapes();
    testTheShippedIconsRead();
    testAHostileFileIsSurvivable();

    if (failures == 0) {
        std::printf("fast_vector_icon: all checks passed\n");
        return 0;
    }
    std::printf("fast_vector_icon: %d check(s) failed\n", failures);
    return 1;
}
