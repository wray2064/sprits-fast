// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

#include "ui/tile_tools.h"

#include "app/i18n.h"
#include "app/ink.h"
#include "app/layers.h"
#include "app/paint.h"
#include "app/tilemap.h"
#include "ui/canvas_view.h"
#include "ui/theme.h"

#include <algorithm>
#include <cstdio>
#include <deque>
#include <map>
#include <set>
#include <string>

namespace fast {

namespace {

bool isDrawingTool(Tool tool) {
    return tool == Tool::Pencil || tool == Tool::Eraser || tool == Tool::Spray ||
           tool == Tool::Bucket || tool == Tool::Rectangle || tool == Tool::Ellipse ||
           tool == Tool::Line || tool == Tool::Gradient || tool == Tool::Text ||
           tool == Tool::Contour || tool == Tool::Polygon || tool == Tool::Curve;
}

// A tile's thumbnail from its layer's texture: the corner the tile is.
void drawTile(Editor& editor, CanvasView& canvas, ImDrawList* draw, ls::SpriteId tileset,
              uint32_t tile, ImVec2 at, float size, ImU32 tint) {
    uint32_t tw = 0;
    uint32_t th = 0;
    if (!tileSizeOf(editor.doc, tileset, &tw, &th)) {
        return;
    }
    const ls::LayerId layer = tileLayer(editor.doc, tileset, tile);
    const FrameCache::Entry* entry =
        layer.valid() ? canvas.frames().entryForLayer(editor.doc, tileset, layer) : nullptr;
    if (entry == nullptr) {
        return;
    }
    const float scale = size / static_cast<float>(std::max(tw, th));
    canvas.drawFramePart(draw, *entry, at, scale, tw, th, tint);
}

void placeAlong(Editor& editor, TilemapLayer& map, ls::Vec2i from, ls::Vec2i to, uint32_t value) {
    std::set<std::pair<int, int>> done;
    for (ls::Vec2i pixel : linePixels(from, to)) {
        ls::Vec2i cell;
        if (cellAt(map, pixel, &cell) && done.insert({ cell.x, cell.y }).second &&
            cellValue(map, cell) != value) {
            setCell(editor.doc, map, cell, value);
        }
    }
}

// The cells alike round `start`, four ways, set to `value`.
int fillCells(Editor& editor, TilemapLayer& map, ls::Vec2i start, uint32_t value) {
    const uint32_t was = cellValue(map, start);
    if (was == value) {
        return 0;
    }
    int filled = 0;
    std::deque<ls::Vec2i> queue { start };
    std::set<std::pair<int, int>> seen { { start.x, start.y } };
    while (!queue.empty()) {
        const ls::Vec2i at = queue.front();
        queue.pop_front();
        if (cellValue(map, at) != was) {
            continue;
        }
        setCell(editor.doc, map, at, value);
        ++filled;
        const ls::Vec2i next[4] = { { at.x + 1, at.y }, { at.x - 1, at.y },
                                    { at.x, at.y + 1 }, { at.x, at.y - 1 } };
        for (ls::Vec2i n : next) {
            if (n.x >= 0 && n.y >= 0 && n.x < static_cast<int>(map.grid.columns) &&
                n.y < static_cast<int>(map.grid.rows) && seen.insert({ n.x, n.y }).second) {
                queue.push_back(n);
            }
        }
    }
    return filled;
}

} // namespace

bool activeTilemap(Editor& editor, TilemapLayer* out) {
    PaintLayer* layer = editor.active();
    return layer != nullptr && readTilemapLayer(editor.doc, layer->layer, out);
}

bool handleTilemapStroke(Editor& editor, CanvasView& canvas, bool overCanvas, ls::Vec2i pixel) {
    TilemapLayer map;
    if (!activeTilemap(editor, &map)) {
        if (editor.placingDrag) {
            editor.placingDrag = false;
            editor.doc.endAction();
        }
        return false;
    }
    const Tool tool = editor.tool;
    const bool pressed = overCanvas && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
                         !ImGui::IsKeyDown(ImGuiKey_Space);

    if (!editor.placingTiles) {
        // The pencil, spray, eraser and picker go on to the stroke code,
        // which draws into the tiles; the rest cannot draw on a grid.
        if (tool == Tool::Pencil || tool == Tool::Eraser || tool == Tool::Spray ||
            tool == Tool::Picker || !isDrawingTool(tool)) {
            return false;
        }
        if (pressed) {
            editor.say("On a tilemap layer, draw into the tiles with the pencil, spray or "
                       "eraser -- or place tiles");
        }
        return true;
    }

    const size_t tiles = tileCount(editor.doc, map.tileset);
    if (tool == Tool::Picker) {
        if (pressed) {
            ls::Vec2i cell;
            const uint32_t value = cellAt(map, pixel, &cell) ? cellValue(map, cell) : 0u;
            if ((value & ls::kTileIndexMask) != 0) {
                editor.brushTile = value & ls::kTileIndexMask;
                editor.brushTurn = value & ~ls::kTileIndexMask;
                editor.say("Picked tile " + std::to_string(editor.brushTile));
            } else {
                editor.say("An empty cell -- nothing to pick");
            }
        }
        return true;
    }
    const auto brushValue = [&]() -> uint32_t {
        if (tiles == 0) {
            return 0;
        }
        const uint32_t tile = std::clamp<uint32_t>(editor.brushTile, 1u, static_cast<uint32_t>(tiles));
        return tile | editor.brushTurn;
    };
    if (tool == Tool::Bucket) {
        if (pressed) {
            ls::Vec2i cell;
            if (tiles == 0) {
                editor.say("No tiles yet -- draw pixels into a cell, or add one in the Element panel");
            } else if (cellAt(map, pixel, &cell)) {
                editor.doc.beginAction("Fill cells");
                const int filled = fillCells(editor, map, cell, brushValue());
                editor.doc.endAction();
                editor.say("Filled " + std::to_string(filled) + " cell(s)");
                canvas.invalidate();
            }
        }
        return true;
    }
    if (tool == Tool::Pencil || tool == Tool::Eraser) {
        const uint32_t value = tool == Tool::Eraser ? 0u : brushValue();
        if (pressed && !editor.placingDrag) {
            if (tool == Tool::Pencil && value == 0) {
                editor.say("No tiles yet -- draw pixels into a cell, or add one in the Element panel");
                return true;
            }
            editor.doc.beginAction(tool == Tool::Eraser ? "Clear cells" : "Place tiles");
            editor.placingDrag = true;
            editor.lastPlaced = pixel;
            placeAlong(editor, map, pixel, pixel, value);
            canvas.invalidate();
            return true;
        }
        if (editor.placingDrag && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            const ls::Vec2i now = canvas.pointerPixel();
            placeAlong(editor, map, editor.lastPlaced, now, value);
            editor.lastPlaced = now;
            canvas.invalidate();
            return true;
        }
        if (editor.placingDrag) {
            editor.placingDrag = false;
            editor.doc.endAction();
            return true;
        }
        return false;
    }
    if (pressed && isDrawingTool(tool)) {
        editor.say("Placing tiles: the pencil places, the eraser clears, the bucket fills, "
                   "the picker picks a tile");
        return true;
    }
    return false;
}

void strokeTiles(Editor& editor, const std::vector<ls::Vec2i>& run) {
    TilemapLayer& map = editor.tileTarget;
    std::map<uint32_t, std::vector<ls::Vec2i>> byTile;
    for (ls::Vec2i pixel : run) {
        uint32_t tile = 0;
        ls::Vec2i local;
        if (!tilePixel(map, pixel, &tile, &local)) {
            ls::Vec2i cell;
            if (editor.tileErasing || !cellAt(map, pixel, &cell)) {
                continue;
            }
            // An empty cell drawn into gets a tile of its own.
            const uint32_t made = addTile(editor.doc, map.tileset);
            if (made == 0 || !setCell(editor.doc, map, cell, made) ||
                !tilePixel(map, pixel, &tile, &local)) {
                continue;
            }
            editor.brushTile = made;
        }
        byTile[tile].push_back(local);
    }
    for (auto& [tile, pixels] : byTile) {
        auto stroke = editor.tileStrokes.find(tile);
        if (stroke == editor.tileStrokes.end()) {
            InkStroke made;
            const ls::LayerId layer = tileLayer(editor.doc, map.tileset, tile);
            const bool ok = editor.tileErasing ? beginEraseStroke(editor.doc, layer, &made)
                                               : beginInkStroke(editor.doc, layer, editor.tileInk, &made);
            if (!ok) {
                continue;
            }
            stroke = editor.tileStrokes.emplace(tile, made).first;
        }
        strokeInk(editor.doc, stroke->second, pixels);
    }
}

void endTileStroke(Editor& editor) {
    for (auto& [tile, stroke] : editor.tileStrokes) {
        (void)tile;
        pruneEmptyInks(editor.doc, stroke.layer);
    }
    editor.tileStrokes.clear();
    editor.tileTarget = TilemapLayer{};
}

void drawTilemapOverlay(Editor& editor, CanvasView& canvas, ImDrawList* draw, ImVec2 origin,
                        float zoom) {
    TilemapLayer map;
    if (!activeTilemap(editor, &map)) {
        return;
    }
    const float tw = static_cast<float>(map.grid.tileWidth) * zoom;
    const float th = static_cast<float>(map.grid.tileHeight) * zoom;
    const ImVec2 o(origin.x + static_cast<float>(map.origin.x) * zoom,
                   origin.y + static_cast<float>(map.origin.y) * zoom);
    const ImVec2 end(o.x + tw * static_cast<float>(map.grid.columns),
                     o.y + th * static_cast<float>(map.grid.rows));
    const ImU32 line = IM_COL32(255, 255, 255, editor.placingTiles ? 70 : 30);
    for (uint32_t c = 0; c <= map.grid.columns; ++c) {
        const float x = o.x + tw * static_cast<float>(c);
        draw->AddLine(ImVec2(x, o.y), ImVec2(x, end.y), line);
    }
    for (uint32_t r = 0; r <= map.grid.rows; ++r) {
        const float y = o.y + th * static_cast<float>(r);
        draw->AddLine(ImVec2(o.x, y), ImVec2(end.x, y), line);
    }
    if (!editor.placingTiles || !editor.canvasHovered) {
        return;
    }
    ls::Vec2i cell;
    if (!cellAt(map, canvas.pointerPixel(), &cell)) {
        return;
    }
    const ImVec2 a(o.x + tw * static_cast<float>(cell.x), o.y + th * static_cast<float>(cell.y));
    const ImVec2 b(a.x + tw, a.y + th);
    // What the pencil would put there, faint, turned as it would be.
    if (editor.tool == Tool::Pencil && tileCount(editor.doc, map.tileset) > 0 &&
        editor.brushTurn == 0) {
        drawTile(editor, canvas, draw, map.tileset, editor.brushTile, a,
                 std::max(tw, th), IM_COL32(255, 255, 255, 140));
    }
    draw->AddRect(a, b, ImGui::GetColorU32(theme::palette().accent), 0.f, 0, 2.f);
}

void drawTilesSection(Editor& editor, CanvasView& canvas) {
    TilemapLayer map;
    if (!activeTilemap(editor, &map)) {
        return;
    }
    theme::sectionHeader("TILES");
    if (ImGui::RadioButton(tr("Draw pixels"), !editor.placingTiles)) {
        editor.placingTiles = false;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton(tr("Place tiles"), editor.placingTiles)) {
        editor.placingTiles = true;
    }
    ImGui::SameLine();
    theme::hint("Drawing pixels draws into the tiles under the pencil: every cell naming a "
                "tile shows the stroke, and an empty cell drawn into gets a new tile. Placing "
                "tiles puts the chosen tile into cells -- the eraser empties them, the bucket "
                "fills, the picker picks.");

    const size_t count = tileCount(editor.doc, map.tileset);
    if (count > 0) {
        editor.brushTile = std::clamp<uint32_t>(editor.brushTile, 1u, static_cast<uint32_t>(count));
    }
    const float size = 30.f;
    const float gap = 4.f;
    const int perRow = std::max(1, static_cast<int>((ImGui::GetContentRegionAvail().x + gap) / (size + gap)));
    ImDrawList* draw = ImGui::GetWindowDrawList();
    for (uint32_t tile = 1; tile <= count; ++tile) {
        if ((tile - 1) % static_cast<uint32_t>(perRow) != 0) {
            ImGui::SameLine(0.f, gap);
        }
        const ImVec2 at = ImGui::GetCursorScreenPos();
        const std::string id = "##tile" + std::to_string(tile);
        if (ImGui::InvisibleButton(id.c_str(), ImVec2(size, size))) {
            editor.brushTile = tile;
            editor.placingTiles = true;
        }
        const bool hovered = ImGui::IsItemHovered();
        draw->AddRectFilled(at, ImVec2(at.x + size, at.y + size),
                            ImGui::GetColorU32(theme::palette().checkerDark));
        drawTile(editor, canvas, draw, map.tileset, tile, at, size, IM_COL32_WHITE);
        const bool chosen = tile == editor.brushTile;
        draw->AddRect(ImVec2(at.x - 1.f, at.y - 1.f), ImVec2(at.x + size + 1.f, at.y + size + 1.f),
                      ImGui::GetColorU32(chosen ? theme::palette().accent
                                                : hovered ? theme::palette().text
                                                          : theme::palette().border),
                      0.f, 0, chosen ? 2.f : 1.f);
        if (hovered) {
            ImGui::SetTooltip("Tile %u -- in %zu cell(s)", tile,
                              cellsNaming(editor.doc, map.tileset, tile));
        }
    }
    if (count == 0) {
        ImGui::PushStyleColor(ImGuiCol_Text, theme::palette().textDim);
        ImGui::TextWrapped("No tiles yet. Draw pixels into a cell and it gets one, or add one here.");
        ImGui::PopStyleColor();
    }

    if (ImGui::SmallButton("+ Tile")) {
        editor.doc.beginAction("New tile");
        const uint32_t made = addTile(editor.doc, map.tileset);
        editor.doc.endAction();
        if (made != 0) {
            editor.brushTile = made;
        }
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(count == 0);
    if (ImGui::SmallButton("Duplicate##tile")) {
        editor.doc.beginAction("Duplicate tile");
        const uint32_t made = duplicateTile(editor.doc, map.tileset, editor.brushTile);
        editor.doc.endAction();
        if (made != 0) {
            editor.brushTile = made;
        }
    }
    // The turn the pencil places with.
    if (ImGui::SmallButton("Flip X")) { editor.brushTurn ^= ls::kTileFlipX; }
    ImGui::SameLine();
    if (ImGui::SmallButton("Flip Y")) { editor.brushTurn ^= ls::kTileFlipY; }
    ImGui::SameLine();
    const bool square = map.grid.tileWidth == map.grid.tileHeight;
    ImGui::BeginDisabled(!square);
    if (ImGui::SmallButton("Turn")) {
        const int clockwise[4] = { 0, -1, 1, 0 };
        editor.brushTurn = turnedCell(1u | editor.brushTurn, clockwise) & ~ls::kTileIndexMask;
    }
    ImGui::EndDisabled();
    ImGui::EndDisabled();
    if (editor.brushTurn != 0) {
        ImGui::SameLine();
        ImGui::TextDisabled("%s%s%s", (editor.brushTurn & ls::kTileFlipD) != 0 ? "turned " : "",
                            (editor.brushTurn & ls::kTileFlipX) != 0 ? "X " : "",
                            (editor.brushTurn & ls::kTileFlipY) != 0 ? "Y" : "");
    }
    ImGui::TextDisabled("%u x %u tiles, %u x %u cells", map.grid.tileWidth, map.grid.tileHeight,
                        map.grid.columns, map.grid.rows);
}

void drawTilemapDialog(Editor& editor, CanvasView& canvas) {
    Editor::TilemapDialog& dialog = editor.tilemapDialog;
    if (!dialog.open) {
        return;
    }
    const std::string title = std::string(tr("New tilemap layer")) + "###tilemap";
    ImGui::OpenPopup(title.c_str());
    if (!ImGui::BeginPopupModal(title.c_str(), &dialog.open, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }
    const std::vector<ls::SpriteId> sets = tilesetsOf(editor.doc);
    const auto nameOf = [&](int index) -> std::string {
        if (index <= 0 || index > static_cast<int>(sets.size())) {
            return tr("A new tileset");
        }
        uint32_t w = 0;
        uint32_t h = 0;
        tileSizeOf(editor.doc, sets[static_cast<size_t>(index - 1)], &w, &h);
        char name[64];
        std::snprintf(name, sizeof(name), "Tileset %d  (%ux%u, %zu tiles)", index, w, h,
                      tileCount(editor.doc, sets[static_cast<size_t>(index - 1)]));
        return name;
    };
    dialog.tileset = std::clamp(dialog.tileset, 0, static_cast<int>(sets.size()));
    ImGui::SetNextItemWidth(260.f);
    if (ImGui::BeginCombo("tileset", nameOf(dialog.tileset).c_str())) {
        for (int i = 0; i <= static_cast<int>(sets.size()); ++i) {
            if (ImGui::Selectable(nameOf(i).c_str(), i == dialog.tileset)) {
                dialog.tileset = i;
            }
        }
        ImGui::EndCombo();
    }
    if (dialog.tileset == 0) {
        ImGui::SetNextItemWidth(100.f);
        ImGui::InputInt("tile width", &dialog.width);
        ImGui::SetNextItemWidth(100.f);
        ImGui::InputInt("tile height", &dialog.height);
        dialog.width = std::clamp(dialog.width, 1, 256);
        dialog.height = std::clamp(dialog.height, 1, 256);
        for (int size : { 8, 16, 24, 32 }) {
            const std::string label = std::to_string(size) + " x " + std::to_string(size);
            if (ImGui::SmallButton(label.c_str())) {
                dialog.width = dialog.height = size;
            }
            ImGui::SameLine();
        }
        ImGui::NewLine();
    }
    ImGui::PushStyleColor(ImGuiCol_Text, theme::palette().textDim);
    ImGui::TextWrapped("A grid of tiles over the canvas. Each tile is drawn once and shows "
                       "wherever it is placed; drawing into one changes it everywhere.");
    ImGui::PopStyleColor();
    ImGui::Dummy(ImVec2(0.f, 6.f));
    if (ImGui::Button("Create", ImVec2(110.f, 0.f))) {
        const ls::SpriteId sprite = editor.activeSprite();
        editor.doc.beginAction("New tilemap layer");
        ls::SpriteId tileset = dialog.tileset > 0 ? sets[static_cast<size_t>(dialog.tileset - 1)]
                                                  : createTileset(editor.doc,
                                                                  static_cast<uint32_t>(dialog.width),
                                                                  static_cast<uint32_t>(dialog.height));
        PaintLayer* active = editor.active();
        const int at = active != nullptr ? indexOfLayer(editor.doc, sprite, active->layer) + 1 : -1;
        const ls::LayerId made = tileset.valid()
            ? createTilemapLayer(editor.doc, sprite, tileset,
                                 "Tilemap " + std::to_string(layerOrder(editor.doc, sprite).size() + 1), at)
            : ls::LayerId{};
        if (made.valid()) {
            editor.doc.endAction();
            resyncLayers(editor);
            selectLayer(editor, made);
            editor.placingTiles = false;
            editor.brushTile = 1;
            editor.brushTurn = 0;
            editor.say("A tilemap layer: draw into a cell to make a tile, then place it anywhere");
            canvas.invalidate();
        } else {
            editor.doc.abandonAction();
            editor.say("Could not make a tilemap layer");
        }
        dialog.open = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(90.f, 0.f))) {
        dialog.open = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

} // namespace fast
