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

#include "QuickPaletteContents.h"

#include <algorithm>

namespace xoj::gui {

namespace {
auto toolSlot(QuickPaletteSlot::Kind kind, ToolType tool) -> QuickPaletteSlot {
    QuickPaletteSlot slot;
    slot.kind = kind;
    slot.toolType = tool;
    slot.name = std::string(toolTypeToString(tool));
    switch (kind) {
        case QuickPaletteSlot::Kind::CurrentTool:
            slot.id = "current-tool";
            break;
        case QuickPaletteSlot::Kind::PreviousTool:
            slot.id = "previous-tool";
            break;
        case QuickPaletteSlot::Kind::Eraser:
            slot.id = "eraser";
            break;
        case QuickPaletteSlot::Kind::Lasso:
            slot.id = "lasso";
            break;
        case QuickPaletteSlot::Kind::Hand:
            slot.id = "hand";
            break;
        default:
            slot.id = "tool";
            break;
    }
    return slot;
}
}  // namespace

auto buildQuickPaletteSlots(const QuickPaletteSlotsInput& input) -> std::vector<QuickPaletteSlot> {
    std::vector<QuickPaletteSlot> slots;

    if (input.currentTool != TOOL_NONE) {
        slots.push_back(toolSlot(QuickPaletteSlot::Kind::CurrentTool, input.currentTool));
    }
    if (input.previousTool != TOOL_NONE && input.previousTool != input.currentTool) {
        slots.push_back(toolSlot(QuickPaletteSlot::Kind::PreviousTool, input.previousTool));
    }

    slots.push_back(toolSlot(QuickPaletteSlot::Kind::Eraser, TOOL_ERASER));
    slots.push_back(toolSlot(QuickPaletteSlot::Kind::Lasso, TOOL_SELECT_RECT));

    if (input.favorites != nullptr) {
        const std::vector<const ToolPreset*> favorites = input.favorites->getFavorites();
        const std::size_t shown = std::min(favorites.size(), ToolPresetList::MAX_FAVORITES);
        for (std::size_t i = 0; i < shown; i++) {
            QuickPaletteSlot slot;
            slot.kind = QuickPaletteSlot::Kind::Favorite;
            slot.presetId = favorites[i]->id;
            slot.name = favorites[i]->name;
            slot.id = "favorite:" + favorites[i]->id;
            slots.push_back(std::move(slot));
        }
    }

    QuickPaletteSlot undo;
    undo.kind = QuickPaletteSlot::Kind::Undo;
    undo.id = "undo";
    undo.name = "undo";
    slots.push_back(std::move(undo));

    slots.push_back(toolSlot(QuickPaletteSlot::Kind::Hand, TOOL_HAND));

    return slots;
}

auto quickPaletteAvailable(const xoj::gesture::GestureSettings& settings) -> bool {
    return settings.quickPaletteEnabled;
}

}  // namespace xoj::gui
