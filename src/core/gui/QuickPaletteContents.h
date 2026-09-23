/*
 * Xournal++
 *
 * What the quick palette holds (Plan 008, step 1)
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <string>
#include <vector>

#include "control/ToolEnums.h"                 // for ToolType
#include "control/ToolPreset.h"                // for ToolPreset (the shared favourites)
#include "control/gestures/GestureSettings.h"  // for GestureSettings (Plan 008)

namespace xoj::gui {

/**
 * Plan 008: one place in the quick palette.
 *
 * A slot says what it is, not how it looks: the widget layer turns a slot into a button and finds
 * the action or the preset to invoke. That split is what lets the contents be checked without a
 * window and the widget be checked without a document.
 */
struct QuickPaletteSlot {
    enum class Kind {
        CurrentTool,
        PreviousTool,
        Eraser,
        Lasso,
        Hand,
        Undo,
        /// One of the shared favourite presets, in the order the user arranged them.
        Favorite,
    };

    Kind kind = Kind::CurrentTool;
    /// Set for the tool slots.
    ToolType toolType = TOOL_NONE;
    /// Set for Favorite slots: the stable id of the preset, so applying it does not depend on its
    /// name.
    std::string presetId;
    /// A stable identifier for tests and for tooltips: "current-tool", "favorite:<preset id>", ...
    std::string id;
    /// The tool or preset's own name. Translated by the widget layer, which is where the tool
    /// names and the presets live.
    std::string name;
};

/**
 * Plan 008: what the palette is built from.
 *
 * The current and previous tools, and the shared favourites - which are Plan 003's presets, not a
 * second list of colours or tools kept for the palette.
 */
struct QuickPaletteSlotsInput {
    /// The tool the user has now.
    ToolType currentTool = TOOL_NONE;
    /// The tool they had before it, so the palette can offer to go back.
    ToolType previousTool = TOOL_NONE;
    /// The favourites, in display order. More than ToolPresetList::MAX_FAVORITES are ignored.
    const ToolPresetList* favorites = nullptr;
};

/**
 * Plan 008: the palette's contents, in the order the plan gives them.
 *
 * Current and previous tool, eraser, lasso, the shared favourites, undo, and hand. The current tool
 * is always there; the previous one only when there is one to go back to and it is not the current
 * one; the favourites are however many the user has, up to the favourite limit. Nothing here
 * depends on the workspace: the caller decides whether the palette is shown at all.
 */
auto buildQuickPaletteSlots(const QuickPaletteSlotsInput& input) -> std::vector<QuickPaletteSlot>;

/// Whether the quick palette can be summoned at all: the user has bound it, per Plan 008 step 2.
auto quickPaletteAvailable(const xoj::gesture::GestureSettings& settings) -> bool;

}  // namespace xoj::gui
