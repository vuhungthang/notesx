/*
 * Xournal++
 *
 * Plan 008 step 1: what the quick palette holds, checked without a window
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#include <algorithm>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "control/ToolEnums.h"
#include "control/ToolPreset.h"  // for ToolPresetList
#include "control/gestures/GestureSettings.h"
#include "gui/QuickPaletteContents.h"

using namespace xoj::gui;

namespace {
bool hasId(const std::vector<QuickPaletteSlot>& slots, const std::string& id) {
    return std::any_of(slots.begin(), slots.end(), [&id](const QuickPaletteSlot& slot) { return slot.id == id; });
}

std::size_t countFavorites(const std::vector<QuickPaletteSlot>& slots) {
    return static_cast<std::size_t>(std::count_if(slots.begin(), slots.end(), [](const QuickPaletteSlot& slot) {
        return slot.kind == QuickPaletteSlot::Kind::Favorite;
    }));
}
}  // namespace

TEST(QuickPaletteContentsTest, holdsTheToolbarTripsThePlanNames) {
    const ToolPresetList presets = ToolPresetList::seedDefaults();
    QuickPaletteSlotsInput input;
    input.currentTool = TOOL_PEN;
    input.previousTool = TOOL_HIGHLIGHTER;
    input.favorites = &presets;

    const std::vector<QuickPaletteSlot> slots = buildQuickPaletteSlots(input);

    // In the order the plan lists them, with the favourites where the plan puts them.
    ASSERT_GE(slots.size(), 6U);
    EXPECT_EQ(slots[0].kind, QuickPaletteSlot::Kind::CurrentTool);
    EXPECT_EQ(slots[0].toolType, TOOL_PEN);
    EXPECT_EQ(slots[1].kind, QuickPaletteSlot::Kind::PreviousTool);
    EXPECT_EQ(slots[1].toolType, TOOL_HIGHLIGHTER);
    EXPECT_EQ(slots[2].kind, QuickPaletteSlot::Kind::Eraser);
    EXPECT_EQ(slots[3].kind, QuickPaletteSlot::Kind::Lasso);
    EXPECT_EQ(slots[slots.size() - 2].kind, QuickPaletteSlot::Kind::Undo);
    EXPECT_EQ(slots.back().kind, QuickPaletteSlot::Kind::Hand);
    EXPECT_EQ(slots.back().toolType, TOOL_HAND);
}

TEST(QuickPaletteContentsTest, showsTheSharedFavoritesAndNoMoreThanTheLimit) {
    const ToolPresetList presets = ToolPresetList::seedDefaults();
    QuickPaletteSlotsInput input;
    input.currentTool = TOOL_PEN;
    input.favorites = &presets;

    const std::vector<QuickPaletteSlot> slots = buildQuickPaletteSlots(input);
    const std::size_t shown = countFavorites(slots);
    EXPECT_GT(shown, 0U) << "a fresh profile has favourite presets";
    EXPECT_LE(shown, ToolPresetList::MAX_FAVORITES);
    EXPECT_EQ(shown, presets.getFavorites().size());

    // Each favourite names the preset it applies, not its display name.
    for (const QuickPaletteSlot& slot: slots) {
        if (slot.kind == QuickPaletteSlot::Kind::Favorite) {
            EXPECT_FALSE(slot.presetId.empty());
            EXPECT_NE(presets.findById(slot.presetId), nullptr);
            EXPECT_FALSE(slot.name.empty());
        }
    }
}

TEST(QuickPaletteContentsTest, thereIsNoPreviousToolSlotWhenThereIsNothingToGoBackTo) {
    QuickPaletteSlotsInput input;
    input.currentTool = TOOL_PEN;
    input.previousTool = TOOL_NONE;
    const std::vector<QuickPaletteSlot> slots = buildQuickPaletteSlots(input);
    EXPECT_FALSE(hasId(slots, "previous-tool"));

    input.previousTool = TOOL_PEN;  // the same tool as now is not "going back"
    EXPECT_FALSE(hasId(buildQuickPaletteSlots(input), "previous-tool"));
}

TEST(QuickPaletteContentsTest, everySlotCarriesAStableId) {
    const ToolPresetList presets = ToolPresetList::seedDefaults();
    QuickPaletteSlotsInput input;
    input.currentTool = TOOL_PEN;
    input.previousTool = TOOL_ERASER;
    input.favorites = &presets;

    for (const QuickPaletteSlot& slot: buildQuickPaletteSlots(input)) {
        EXPECT_FALSE(slot.id.empty());
        EXPECT_FALSE(slot.name.empty()) << slot.id;
    }
}

TEST(QuickPaletteContentsTest, isOnlyAvailableWhenTheUserHasBoundIt) {
    xoj::gesture::GestureSettings settings = xoj::gesture::GestureSettings::defaults();
    EXPECT_FALSE(quickPaletteAvailable(settings));

    settings.quickPaletteEnabled = true;
    EXPECT_TRUE(quickPaletteAvailable(settings));
}
