// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

#include "ui/start_art.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

namespace fast {

namespace {

ImU32 rgb(uint32_t c, int alpha = 255) {
    return IM_COL32((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF, alpha);
}

struct Box {
    ImVec2 from;
    ImVec2 to;
    float w() const { return to.x - from.x; }
    float h() const { return to.y - from.y; }
    // A point at a fraction of the box.
    ImVec2 at(float x, float y) const { return ImVec2(from.x + w() * x, from.y + h() * y); }
};

void rect(ImDrawList* draw, const Box& box, float x0, float y0, float x1, float y1, ImU32 colour,
          float rounding = 0.f) {
    draw->AddRectFilled(box.at(x0, y0), box.at(x1, y1), colour, rounding);
}

void disc(ImDrawList* draw, const Box& box, float x, float y, float radius, ImU32 colour) {
    draw->AddCircleFilled(box.at(x, y), radius * std::min(box.w(), box.h()), colour, 24);
}

// An icon is a heart: the smallest picture that is clearly a picture.
void heart(ImDrawList* draw, const Box& box) {
    const float r = 0.19f;
    const ImU32 red = rgb(0xD9434A);
    disc(draw, box, 0.5f - r * 0.92f, 0.40f, r, red);
    disc(draw, box, 0.5f + r * 0.92f, 0.40f, r, red);
    const ImVec2 left = box.at(0.5f - r * 1.85f, 0.46f);
    const ImVec2 right = box.at(0.5f + r * 1.85f, 0.46f);
    draw->AddTriangleFilled(left, right, box.at(0.5f, 0.82f), red);
    disc(draw, box, 0.5f - r * 1.15f, 0.34f, 0.05f, rgb(0xFFFFFF, 150));
}

// A tile is a brick wall.
void bricks(ImDrawList* draw, const Box& box) {
    rect(draw, box, 0.f, 0.f, 1.f, 1.f, rgb(0x6B3B2E));
    const int rows = 5;
    const float rowH = 1.f / static_cast<float>(rows);
    for (int row = 0; row < rows; ++row) {
        const float y0 = static_cast<float>(row) * rowH;
        const float shift = (row % 2 == 0) ? 0.f : -0.25f;
        for (int col = 0; col < 4; ++col) {
            const float x0 = shift + static_cast<float>(col) * 0.5f;
            const float gap = 0.03f;
            rect(draw, box, x0 + gap, y0 + gap * 0.8f, x0 + 0.5f - gap, y0 + rowH - gap * 0.8f,
                 rgb(0xC0694C));
            rect(draw, box, x0 + gap, y0 + gap * 0.8f, x0 + 0.5f - gap, y0 + rowH * 0.34f,
                 rgb(0xDB8A68));
        }
    }
}

// A character: a head, a body, legs, and a hat to make it somebody.
void figure(ImDrawList* draw, const Box& box) {
    rect(draw, box, 0.34f, 0.62f, 0.46f, 0.88f, rgb(0x2E3A59));          // legs
    rect(draw, box, 0.54f, 0.62f, 0.66f, 0.88f, rgb(0x2E3A59));
    rect(draw, box, 0.30f, 0.38f, 0.70f, 0.66f, rgb(0x3A7BD5), 2.f);       // body
    rect(draw, box, 0.22f, 0.40f, 0.30f, 0.58f, rgb(0xF0C8A0));            // arms
    rect(draw, box, 0.70f, 0.40f, 0.78f, 0.58f, rgb(0xF0C8A0));
    rect(draw, box, 0.34f, 0.12f, 0.66f, 0.40f, rgb(0xF0C8A0), 3.f);       // head
    rect(draw, box, 0.30f, 0.07f, 0.70f, 0.19f, rgb(0xC0392B), 2.f);       // hat
    disc(draw, box, 0.43f, 0.28f, 0.025f, rgb(0x2B2B33));
    disc(draw, box, 0.57f, 0.28f, 0.025f, rgb(0x2B2B33));
}

// A portrait is a face and shoulders.
void face(ImDrawList* draw, const Box& box) {
    rect(draw, box, 0.f, 0.f, 1.f, 1.f, rgb(0x34495E));
    disc(draw, box, 0.5f, 1.02f, 0.40f, rgb(0xC0392B));                    // shoulders
    rect(draw, box, 0.44f, 0.58f, 0.56f, 0.72f, rgb(0xE6B994));            // neck
    disc(draw, box, 0.5f, 0.42f, 0.22f, rgb(0xF0C8A0));                    // head
    disc(draw, box, 0.5f, 0.28f, 0.19f, rgb(0x5A3A2A));                    // hair
    disc(draw, box, 0.5f, 0.38f, 0.19f, rgb(0xF0C8A0));
    disc(draw, box, 0.42f, 0.40f, 0.022f, rgb(0x2B2B33));
    disc(draw, box, 0.58f, 0.40f, 0.022f, rgb(0x2B2B33));
    draw->AddLine(box.at(0.44f, 0.50f), box.at(0.56f, 0.50f), rgb(0xA0522D),
                  std::max(1.f, box.h() * 0.012f));
}

// A scene is a sky, a sun and some hills.
void landscape(ImDrawList* draw, const Box& box) {
    rect(draw, box, 0.f, 0.f, 1.f, 0.55f, rgb(0x6FA8DC));
    rect(draw, box, 0.f, 0.55f, 1.f, 0.70f, rgb(0x9CC8EC));
    disc(draw, box, 0.78f, 0.28f, 0.11f, rgb(0xF6D55C));
    // Hills: big discs, cut off where the canvas ends.
    draw->AddCircleFilled(box.at(0.28f, 1.12f), box.h() * 0.62f, rgb(0x3F7D46), 40);
    draw->AddCircleFilled(box.at(0.78f, 1.2f), box.h() * 0.66f, rgb(0x5C9E5A), 40);
    rect(draw, box, 0.485f, 0.52f, 0.515f, 0.72f, rgb(0x5A3A2A));           // a tree
    draw->AddTriangleFilled(box.at(0.44f, 0.56f), box.at(0.56f, 0.56f), box.at(0.50f, 0.34f),
                            rgb(0x2D6A3A));
}

// A handheld is a screen, a pad and two buttons.
void handheld(ImDrawList* draw, const Box& box) {
    rect(draw, box, 0.f, 0.f, 1.f, 1.f, rgb(0x8D8D96), 3.f);
    rect(draw, box, 0.12f, 0.08f, 0.88f, 0.52f, rgb(0x2B2F3A), 2.f);
    rect(draw, box, 0.17f, 0.13f, 0.83f, 0.47f, rgb(0x9BB67E));
    rect(draw, box, 0.26f, 0.22f, 0.40f, 0.38f, rgb(0x4E6B3A));            // a sprite on it
    rect(draw, box, 0.50f, 0.30f, 0.70f, 0.38f, rgb(0x4E6B3A));
    rect(draw, box, 0.16f, 0.72f, 0.36f, 0.78f, rgb(0x2B2F3A));            // the pad
    rect(draw, box, 0.23f, 0.64f, 0.29f, 0.86f, rgb(0x2B2F3A));
    disc(draw, box, 0.66f, 0.78f, 0.05f, rgb(0xB03A48));                   // the buttons
    disc(draw, box, 0.80f, 0.70f, 0.05f, rgb(0xB03A48));
}

} // namespace

void drawPresetArt(ImDrawList* draw, const std::string& name, ImVec2 from, ImVec2 to) {
    const Box box { from, to };
    draw->PushClipRect(from, to, true);
    if (name == "Icon") {
        heart(draw, box);
    } else if (name == "Tile") {
        bricks(draw, box);
    } else if (name == "Character") {
        figure(draw, box);
    } else if (name == "Portrait") {
        face(draw, box);
    } else if (name == "Scene") {
        landscape(draw, box);
    } else if (name == "Handheld") {
        handheld(draw, box);
    }
    draw->PopClipRect();
}

} // namespace fast
