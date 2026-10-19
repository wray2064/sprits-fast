// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors
#pragma once

// user_guide.h — Help > User guide: the manual, inside the program.
//
// The topics are Markdown files under assets/help/ (read by app/guide.h),
// compiled into the program, so the guide is always there, needs no network and
// is the same version as the program it describes. The window is a list of topics
// beside the topic; a search box narrows the list to the topics every word of it
// appears in; a link in a topic goes to another topic, or opens a page in the
// browser (https only).

#include "app/guide.h"
#include "ui/editor.h"

namespace fast {

// Every topic, read once.
const std::vector<GuideTopic>& userGuide();

// Opens the guide, at `topicId` when it names one (otherwise where it was left).
void openUserGuide(Editor& editor, const char* topicId = nullptr);

// Once a frame.
void drawUserGuide(Editor& editor);

} // namespace fast
