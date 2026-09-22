/*
 * Xournal++
 *
 * The property panels of the tools that have one today
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include "ToolPropertyProvider.h"  // for ToolPropertyRegistry

class IconNameHelper;

namespace xoj::toolbar {

/**
 * @brief Register the property panels that exist today (Plan 003, step 3).
 *
 * Pen, highlighter and eraser. Shape and selection are deliberately absent: their panels belong to
 * a later plan, and adding one is a new provider plus one line here, not a change to the panel.
 */
void addBuiltInToolPropertyProviders(ToolPropertyRegistry& registry, const IconNameHelper& icons);

}  // namespace xoj::toolbar
