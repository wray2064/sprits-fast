// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors

// panels.cpp -- the undo brackets every panel's live controls use. The panels
// themselves are in panel_*.cpp; see panels_internal.h.

#include "ui/panels_internal.h"

namespace fast {

void bracketDrag(Editor& editor, bool& flag, const char* label) {
    if (ImGui::IsItemActivated() && !flag) {
        editor.doc.beginAction(label);
        flag = true;
    }
    if (flag && ImGui::IsItemDeactivated()) {
        editor.doc.endAction();
        flag = false;
    }
}

void singleAction(Editor& editor, const char* label) {
    editor.doc.beginAction(label);
    editor.doc.endAction();
}

void holdForUndo(Editor& editor, bool& flag, const char* label) {
    bracketDrag(editor, flag, label);
}

} // namespace fast
