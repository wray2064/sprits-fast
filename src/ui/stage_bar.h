// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors
#pragma once

// stage_bar.h — the bar of numbered stages under the menu (AppExtension::stages).
//
// "1 Import > 2 Draw > 3 Animate > 4 Arrange": each a button that puts the dock in that
// stage's arrangement (layout.h useStage), the one in force marked, a line of what the
// stage has so far, and Next to go on. Nothing is locked: any stage opens at any time,
// and Ctrl+1 to Ctrl+9 open them by number.

#include "ui/editor.h"

namespace fast {

// The bar's height, 0 when the window has no stages.
float stageBarHeight(const Editor& editor);

// Draws the bar across the top of the work area, `height` high (stageBarHeight).
void drawStageBar(Editor& editor, float height);

} // namespace fast
