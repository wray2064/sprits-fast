// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

#include "ui/user_guide.h"

#include "app/i18n.h"
#include "ui/theme.h"
#include "ui/ui_script.h"
#include "ui/widgets.h"

#include <SDL3/SDL.h>
#include <imgui.h>

#include "help_embedded.h"

#include <algorithm>
#include <string>

namespace fast {

namespace {

// --- the topics ---------------------------------------------------------------------

std::vector<GuideSource> embeddedSources() {
    std::vector<GuideSource> sources;
    for (const EmbeddedHelp& file : kEmbeddedHelp) {
        std::string text(reinterpret_cast<const char*>(file.data), file.size);
        std::string name = file.file;
        if (name.size() > 4 && name.compare(name.size() - 4, 4, ".txt") == 0) {
            // A plain text file (the licences) is a topic that shows it as written.
            const std::string title = topicIdFromFileName(name) == "licences" ? "Licences" : name;
            text = "# " + title + "\n\nThe licence of everything Sprit's'fast is built on, with the "
                   "text each asks to travel with it.\n\n```\n" + text + "\n```\n";
        }
        GuideSource source;
        source.id = topicIdFromFileName(name);
        source.text = text;
        sources.push_back(source);
    }
    return sources;
}

// --- drawing a topic -------------------------------------------------------------------

struct LinkClick {
    std::string target;
};

// Which link of the topic being drawn this is: the same in every frame, so a press
// on one and its release are on the same button. Reset as each frame begins.
int gLinkCounter = 0;

float scaled(float pixels) {
    return pixels * theme::scale();
}

// One word, or one code run, to place.
struct Piece {
    std::string text;
    RunStyle    style = RunStyle::Plain;
    std::string target;
};

std::vector<Piece> piecesOf(const std::vector<Run>& runs) {
    std::vector<Piece> pieces;
    for (const Run& run : runs) {
        if (run.style == RunStyle::Code) {
            Piece piece;
            piece.text = run.text;
            piece.style = RunStyle::Code;
            pieces.push_back(piece);
            continue;
        }
        // Words, each keeping the space after it, so a line can break between them.
        size_t at = 0;
        while (at < run.text.size()) {
            size_t end = run.text.find(' ', at);
            end = end == std::string::npos ? run.text.size() : end + 1;
            Piece piece;
            piece.text = run.text.substr(at, end - at);
            piece.style = run.style;
            piece.target = run.target;
            pieces.push_back(piece);
            at = end;
        }
    }
    return pieces;
}

// Runs of text with styles, laid out in the width there is and broken between
// words. A link that was clicked is reported.
void drawRuns(const std::vector<Run>& runs, LinkClick* click) {
    const theme::Palette& p = theme::palette();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const float width = std::max(40.f, ImGui::GetContentRegionAvail().x);
    float x = 0.f;
    bool first = true;
    for (const Piece& piece : piecesOf(runs)) {
        const bool strong = piece.style == RunStyle::Bold;
        theme::pushText(strong ? theme::TextStyle::Strong : theme::TextStyle::Body);
        const ImVec2 size = ImGui::CalcTextSize(piece.text.c_str());
        const float pad = piece.style == RunStyle::Code ? scaled(4.f) : 0.f;
        const float need = size.x + pad * 2.f;
        const bool wrap = !first && x + need > width;
        if (!first && !wrap) {
            ImGui::SameLine(0.f, 0.f);
        }
        if (wrap) {
            x = 0.f;
        }
        first = false;
        const ImVec2 at = ImGui::GetCursorScreenPos();
        ImVec4 colour = p.text;
        switch (piece.style) {
            case RunStyle::Bold:   colour = p.textBright; break;
            case RunStyle::Italic: colour = p.textDim; break;
            case RunStyle::Code:   colour = p.textBright; break;
            case RunStyle::Link:   colour = p.accentText; break;
            case RunStyle::Plain:  break;
        }
        if (piece.style == RunStyle::Code) {
            // How a key is written: a small raised tag.
            draw->AddRectFilled(ImVec2(at.x, at.y - 1.f), ImVec2(at.x + need, at.y + size.y + 1.f),
                                ImGui::GetColorU32(p.inset), scaled(3.f));
            draw->AddRect(ImVec2(at.x, at.y - 1.f), ImVec2(at.x + need, at.y + size.y + 1.f),
                          ImGui::GetColorU32(p.separator), scaled(3.f));
            ImGui::SetCursorScreenPos(ImVec2(at.x + pad, at.y));
        }
        if (piece.style == RunStyle::Link) {
            // A link is a button the size of its words, so it can be pressed, and a
            // script can find it by what it says.
            const std::string word = piece.text.back() == ' ' ? piece.text.substr(0, piece.text.size() - 1)
                                                               : piece.text;
            const float spaceWidth = piece.text.back() == ' ' ? ImGui::CalcTextSize(" ").x : 0.f;
            const std::string id = "##link" + std::to_string(gLinkCounter++);
            const bool pressed = ImGui::InvisibleButton(id.c_str(), size);
            const bool hovered = ImGui::IsItemHovered();
            nameForScripts(word.c_str());
            draw->AddText(at, ImGui::GetColorU32(hovered ? p.accentHover : colour), piece.text.c_str());
            draw->AddLine(ImVec2(at.x, at.y + size.y - 1.f), ImVec2(at.x + size.x - spaceWidth, at.y + size.y - 1.f),
                          ImGui::GetColorU32(hovered ? p.accentHover : p.accentText));
            if (hovered) {
                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            }
            if (pressed) {
                click->target = piece.target;
            }
        } else {
            ImGui::PushStyleColor(ImGuiCol_Text, colour);
            ImGui::TextUnformatted(piece.text.c_str());
            ImGui::PopStyleColor();
            if (piece.style == RunStyle::Code) {
                ImGui::SameLine(0.f, pad);          // the tag's right edge
            }
        }
        theme::popText();
        x += need;
    }
    if (first) {
        ImGui::NewLine();       // an empty line still takes its height
    }
}

void drawBlock(const Block& block, LinkClick* click) {
    const theme::Palette& p = theme::palette();
    switch (block.kind) {
        case BlockKind::Heading: {
            ImGui::Dummy(ImVec2(0.f, scaled(block.level == 2 ? 10.f : 6.f)));
            theme::pushText(block.level <= 2 ? theme::TextStyle::Title : theme::TextStyle::Strong);
            ImGui::PushStyleColor(ImGuiCol_Text, block.level <= 2 ? p.textBright : p.text);
            std::string text;
            for (const Run& run : block.runs) { text += run.text; }
            ImGui::TextWrapped("%s", text.c_str());
            ImGui::PopStyleColor();
            theme::popText();
            if (block.level == 2) {
                ImGui::Separator();
            }
            ImGui::Dummy(ImVec2(0.f, scaled(2.f)));
            break;
        }
        case BlockKind::Paragraph:
            drawRuns(block.runs, click);
            ImGui::Dummy(ImVec2(0.f, scaled(6.f)));
            break;
        case BlockKind::Bullet:
        case BlockKind::Numbered: {
            const float indent = scaled(block.level == 0 ? 6.f : 26.f);
            ImGui::Indent(indent);
            const ImVec2 at = ImGui::GetCursorScreenPos();
            if (block.kind == BlockKind::Bullet) {
                ImGui::GetWindowDrawList()->AddCircleFilled(
                    ImVec2(at.x + scaled(5.f), at.y + ImGui::GetTextLineHeight() * 0.5f), scaled(2.5f),
                    ImGui::GetColorU32(p.accentText));
            } else {
                const std::string number = std::to_string(block.number) + ".";
                ImGui::GetWindowDrawList()->AddText(ImVec2(at.x, at.y), ImGui::GetColorU32(p.textDim),
                                                    number.c_str());
            }
            ImGui::Indent(scaled(block.kind == BlockKind::Bullet ? 16.f : 24.f));
            drawRuns(block.runs, click);
            ImGui::Unindent(scaled(block.kind == BlockKind::Bullet ? 16.f : 24.f));
            ImGui::Unindent(indent);
            ImGui::Dummy(ImVec2(0.f, scaled(2.f)));
            break;
        }
        case BlockKind::Note: {
            std::string text;
            for (const Run& run : block.runs) { text += run.text; }
            widgets::banner(widgets::Tone::Info, text.c_str());
            ImGui::Dummy(ImVec2(0.f, scaled(4.f)));
            break;
        }
        case BlockKind::Rule:
            ImGui::Dummy(ImVec2(0.f, scaled(4.f)));
            ImGui::Separator();
            ImGui::Dummy(ImVec2(0.f, scaled(4.f)));
            break;
        case BlockKind::Code: {
            const float line = ImGui::GetTextLineHeightWithSpacing();
            const float height = line * static_cast<float>(block.lines.size()) + scaled(10.f);
            const ImVec2 at = ImGui::GetCursorScreenPos();
            const float width = ImGui::GetContentRegionAvail().x;
            ImGui::GetWindowDrawList()->AddRectFilled(at, ImVec2(at.x + width, at.y + height),
                                                      ImGui::GetColorU32(p.inset), scaled(4.f));
            ImGui::Dummy(ImVec2(0.f, scaled(5.f)));
            ImGui::PushStyleColor(ImGuiCol_Text, p.textDim);
            for (const std::string& text : block.lines) {
                ImGui::SetCursorScreenPos(ImVec2(at.x + scaled(8.f), ImGui::GetCursorScreenPos().y));
                ImGui::TextUnformatted(text.empty() ? " " : text.c_str());
            }
            ImGui::PopStyleColor();
            ImGui::Dummy(ImVec2(0.f, scaled(5.f)));
            break;
        }
        case BlockKind::Table: {
            if (block.rows.empty()) {
                break;
            }
            size_t columns = 0;
            for (const auto& row : block.rows) { columns = std::max(columns, row.size()); }
            if (columns == 0 || columns > 8) {
                break;
            }
            if (ImGui::BeginTable("##guidetable", static_cast<int>(columns),
                                  ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                      ImGuiTableFlags_SizingStretchProp)) {
                for (size_t r = 0; r < block.rows.size(); ++r) {
                    ImGui::TableNextRow();
                    for (size_t c = 0; c < columns; ++c) {
                        ImGui::TableSetColumnIndex(static_cast<int>(c));
                        if (c < block.rows[r].size()) {
                            std::vector<Run> runs = block.rows[r][c];
                            if (r == 0) {
                                for (Run& run : runs) {
                                    if (run.style == RunStyle::Plain) { run.style = RunStyle::Bold; }
                                }
                            }
                            drawRuns(runs, click);
                        }
                    }
                }
                ImGui::EndTable();
            }
            ImGui::Dummy(ImVec2(0.f, scaled(6.f)));
            break;
        }
    }
}

bool isHttps(const std::string& target) {
    return target.rfind("https://", 0) == 0 && target.size() < 400;
}

} // namespace

const std::vector<GuideTopic>& userGuide() {
    static const std::vector<GuideTopic> topics = parseGuide(embeddedSources());
    return topics;
}

void openUserGuide(Editor& editor, const char* topicId) {
    // Opened afresh the list is whole, whatever was searched for last time; a link
    // from one topic to another arrives here too.
    editor.guideOpen = true;
    editor.guideSearch[0] = '\0';
    if (topicId != nullptr) {
        const std::vector<GuideTopic>& topics = userGuide();
        for (size_t i = 0; i < topics.size(); ++i) {
            if (topics[i].id == topicId) {
                editor.guideTopic = static_cast<int>(i);
                editor.guideScrollTop = true;
                editor.guideSearch[0] = '\0';
            }
        }
    }
}

void drawUserGuide(Editor& editor) {
    if (!editor.guideOpen) {
        return;
    }
    const std::vector<GuideTopic>& topics = userGuide();
    if (topics.empty()) {
        editor.guideOpen = false;
        return;
    }
    const float k = theme::scale();
    ImGui::SetNextWindowSize(ImVec2(960.f * k, 660.f * k), ImGuiCond_Appearing);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing,
                            ImVec2(0.5f, 0.5f));
    const std::string title = std::string(tr("User guide")) + "###userguide";
    if (!ImGui::Begin(title.c_str(), &editor.guideOpen, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }
    // Escape puts it away, as it does every dialog; while the search box has the
    // keyboard it is the box's own way out first.
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
        !ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        editor.guideOpen = false;
    }
    editor.guideTopic = std::clamp(editor.guideTopic, 0, static_cast<int>(topics.size()) - 1);
    const theme::Palette& p = theme::palette();

    // --- the list ---------------------------------------------------------------------
    const float listWidth = std::min(260.f * k, ImGui::GetContentRegionAvail().x * 0.4f);
    ImGui::BeginChild("##guidelist", ImVec2(listWidth, 0.f), false);
    ImGui::SetNextItemWidth(-1.f);
    ImGui::InputTextWithHint("##guideSearch", tr("Search the guide"), editor.guideSearch,
                             sizeof(editor.guideSearch));
    const std::vector<GuideHit> hits = searchGuide(topics, editor.guideSearch);
    const bool searching = editor.guideSearch[0] != '\0';
    ImGui::Dummy(ImVec2(0.f, scaled(4.f)));
    if (searching) {
        if (hits.empty()) {
            ImGui::TextColored(p.textDim, "%s", tr("No topic has all of those words."));
        }
        for (const GuideHit& hit : hits) {
            ImGui::PushID(static_cast<int>(hit.topic));
            if (ImGui::Selectable(topics[hit.topic].title.c_str(),
                                  static_cast<int>(hit.topic) == editor.guideTopic)) {
                editor.guideTopic = static_cast<int>(hit.topic);
                editor.guideScrollTop = true;
            }
            ImGui::PushStyleColor(ImGuiCol_Text, p.textMuted);
            theme::pushText(theme::TextStyle::Caption);
            ImGui::TextWrapped("%s", hit.snippet.c_str());
            theme::popText();
            ImGui::PopStyleColor();
            ImGui::PopID();
            ImGui::Dummy(ImVec2(0.f, scaled(3.f)));
        }
    } else {
        for (size_t i = 0; i < topics.size(); ++i) {
            if (ImGui::Selectable(topics[i].title.c_str(), static_cast<int>(i) == editor.guideTopic)) {
                editor.guideTopic = static_cast<int>(i);
                editor.guideScrollTop = true;
            }
        }
    }
    ImGui::EndChild();
    ImGui::SameLine();

    // --- the topic ------------------------------------------------------------------------
    ImGui::BeginChild("##guidetopic", ImVec2(0.f, 0.f), true,
                      ImGuiWindowFlags_HorizontalScrollbar);
    if (editor.guideScrollTop) {
        ImGui::SetScrollY(0.f);
        ImGui::SetScrollX(0.f);
        editor.guideScrollTop = false;
    }
    const GuideTopic& topic = topics[static_cast<size_t>(editor.guideTopic)];
    theme::pushText(theme::TextStyle::Display);
    ImGui::TextColored(p.textBright, "%s", topic.title.c_str());
    theme::popText();
    ImGui::Dummy(ImVec2(0.f, scaled(6.f)));
    LinkClick click;
    gLinkCounter = 0;
    for (const Block& block : topic.blocks) {
        drawBlock(block, &click);
    }
    ImGui::Dummy(ImVec2(0.f, scaled(16.f)));
    ImGui::EndChild();
    ImGui::End();

    // A link followed: to another topic, or out to a page.
    if (click.target.rfind("topic:", 0) == 0) {
        openUserGuide(editor, click.target.c_str() + 6);
    } else if (isHttps(click.target)) {
        SDL_OpenURL(click.target.c_str());
    }
}

} // namespace fast
