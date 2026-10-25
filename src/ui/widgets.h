// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors
#pragma once

// widgets.h — the pieces every panel is made of.
//
// A panel does not style a button or lay out a label and its control; it asks
// for one. That is what keeps forty panels looking like one program, lets a
// theme or a density change reach all of them at once: everything here takes
// its colours from the theme in force
// (ui/theme.h) and its type and spacing from the scales, never from a literal.
//
// Every component has the states a person can see: rest, hover, pressed, focus
// (a ring, always, for the keyboard), selected and disabled. Labels go through
// tr() and are the names UI scripts click by.
//
// The gallery at the end draws every component in every state, so a change of
// look is reviewed in one place (Window > Interface kit, or --kit).

#include <imgui.h>

namespace fast {
namespace widgets {

// --- buttons ----------------------------------------------------------------------

// Primary is the one thing to do on a surface -- Create, Open, Export -- and
// there is one per surface. Secondary is the ordinary button. Ghost sits in
// toolbars and rows without a box until hovered. Danger is for what loses
// work.
enum class ButtonStyle { Secondary, Primary, Ghost, Danger };

bool button(const char* label, ButtonStyle style = ButtonStyle::Secondary,
            ImVec2 size = ImVec2(0.f, 0.f));

// --- choices ---------------------------------------------------------------------

// A switch for something that is on or off now (as opposed to a checkbox, which
// is a choice made with others). The label is to its right.
bool toggle(const char* label, bool* value);

// One of a few, side by side: 1x 2x 3x, round or square. `options` are
// translated; the one at *selected is lit. True when the choice changed.
bool segmented(const char* id, int* selected, const char* const* options, int count);

// A slider that shows how far along it is: a well, the part up to the value
// filled, a thin handle, and the value written in it as `format` says ("size
// %d"). Typing works as in any ImGui slider (Ctrl+click). The same call as
// ImGui's, so a panel swaps one for the other and nothing else.
bool sliderInt(const char* label, int* value, int low, int high, const char* format = "%d",
               ImGuiSliderFlags flags = 0);
bool sliderFloat(const char* label, float* value, float low, float high,
                 const char* format = "%.2f", ImGuiSliderFlags flags = 0);

// --- fields ------------------------------------------------------------------------

// A label in a column on the left, and the control that follows filling the
// rest of the row. Every property row in the program is made this way, so the
// labels line up. Call it, then draw the control (with an "##id" label).
void field(const char* label, float labelWidth = 0.f);
// The width of the label column when none is given: it follows the font.
float fieldLabelWidth();

// What an ImGui control that writes its label after the control (a drag, a
// slider, a combo given a label rather than an "##id") needs to the right of it:
// `SetNextItemWidth(-labelRoom(shown))` leaves it exactly that, whatever the
// language and the interface size make of it. `shown` is the text the control is
// given, translated already when it is to be.
float labelRoom(const char* shown);

// --- rows of buttons ----------------------------------------------------------------
//
// A panel is as wide as the person made it, and a label is as wide as the font
// and the language make it, so a row of buttons cannot be given widths in
// pixels. These take them from the labels, and wrap rather than cut a label.

// What a button needs to show `label` (translated) whole.
float buttonWidth(const char* label);

// Buttons that share the row's width equally: all on one row when the longest
// label fits in its share, and in as few even rows as it takes when it does not.
struct ButtonRow {
    float width = 0.f;      // each button's width
    int perRow = 1;         // how many go on a row
};
ButtonRow buttonRow(const char* const* labels, int count);
// Call before button `index` of a ButtonRow: beside the last one, or the first
// of the next row.
void beside(const ButtonRow& row, int index);

// For items of their own widths: puts the next item beside the last one when
// `width` fits in what is left of the row, and on the line below when it does not.
// `gap` is the space between them (the item spacing when not given). True when it
// stayed on the line, false when it went below.
bool sameLineIfFits(float width, float gap = -1.f);

// --- status ------------------------------------------------------------------------

enum class Tone { Neutral, Accent, Success, Warning, Danger, Info };

// A small tinted label: "new", "locked", "3 missing".
void badge(const char* text, Tone tone = Tone::Neutral);
// A message across the width: a tint, a bar of the tone on the left, text.
void banner(Tone tone, const char* text);
// A key as it is drawn in a shortcut: "Ctrl", "Shift", "E".
void keycap(const char* text);
// What a list or a panel says when it has nothing in it: what it is for, and
// what to do.
void emptyState(const char* title, const char* hint);

// --- dialogs ------------------------------------------------------------------------

// One width scale and one padding for every dialog, so they read as one family.
enum class DialogSize { Small, Medium, Large };

// A modal with the title above the body and no title bar. Call every frame
// while *open; draw the body when it returns true; end with endDialog().
bool beginDialog(const char* title, bool* open, DialogSize size = DialogSize::Medium);
void endDialog();

// The same frame for a modal that is written the older way: a drop-in for
// ImGui::BeginPopupModal(name, open, flags). It supplies the padding, the
// title (the part of `name` before any "###") above a hairline, and Escape when
// `open` is given; the body and the buttons are the caller's, to be moved onto
// dialogButtons as each dialog is next touched. Close with ImGui::EndPopup().
bool beginModal(const char* name, bool* open, ImGuiWindowFlags flags = 0);

// The footer: Cancel (or whatever the second is) beside the primary, the
// primary last, on the right. Returns 1 for the primary, 2 for the other, 0 for
// neither. A null `secondary` leaves one button.
int dialogButtons(const char* primary, const char* secondary = "Cancel",
                  bool primaryEnabled = true);

// For a window that is not modal (History, the shortcut list, the user guide):
// true when Escape was pressed for it -- it, or something in it, has the focus, and
// no text field is taking the key as its own way out. Call between Begin and End.
bool escapePutsAway();

// --- the gallery -----------------------------------------------------------------------

// Every component in every state, in a window of its own.
void drawGallery(bool* open);

} // namespace widgets
} // namespace fast
