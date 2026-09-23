/*
 * Xournal++
 *
 * Plan 008, step 1: the tool the quick palette offers to return to
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#include <gtest/gtest.h>

#include "control/ToolEnums.h"
#include "control/ToolHandler.h"

/*
 * The quick palette's previous-tool slot needs a source for "the tool the user was on before this
 * one", which this tree did not keep. ToolHandler now records it as the effective active tool
 * changes. These tests exercise the record on a ToolHandler with no Control, as ToolConfigAdapterTest
 * does, since none of the paths that matter here need one.
 *
 * The record has to be safe as well as correct: it is written on every tool change, so it must not
 * call back into anything (no recursion) and must never hold TOOL_NONE or the current tool, because
 * a slot offering to go back to a tool the user is already on, or to no tool, is worse than no slot.
 */

namespace {

/// ToolHandler requires a ToolListener; these tests only care about the previous-tool record.
class QuietListener: public ToolListener {
public:
    void toolColorChanged() override {}
    void changeColorOfSelection() override {}
    void toolSizeChanged() override {}
    void toolFillChanged() override {}
    void toolLineStyleChanged() override {}
    void toolChanged() override {}
};

}  // namespace

TEST(ToolHandlerPreviousToolTest, aFreshHandlerHasNoPreviousTool) {
    QuietListener listener;
    ToolHandler handler(&listener, nullptr, nullptr);

    EXPECT_EQ(handler.getPreviousToolType(), TOOL_NONE) << "nothing to go back to before any change";
}

TEST(ToolHandlerPreviousToolTest, selectingADifferentToolRemembersTheOneLeft) {
    QuietListener listener;
    ToolHandler handler(&listener, nullptr, nullptr);

    handler.selectTool(TOOL_PEN);  // the one it starts on: no change to remember
    EXPECT_EQ(handler.getPreviousToolType(), TOOL_NONE);

    handler.selectTool(TOOL_ERASER);
    EXPECT_EQ(handler.getPreviousToolType(), TOOL_PEN);

    handler.selectTool(TOOL_HIGHLIGHTER);
    EXPECT_EQ(handler.getPreviousToolType(), TOOL_ERASER);
}

TEST(ToolHandlerPreviousToolTest, selectingTheSameToolAgainDoesNotChangeTheRecord) {
    QuietListener listener;
    ToolHandler handler(&listener, nullptr, nullptr);

    handler.selectTool(TOOL_ERASER);
    ASSERT_EQ(handler.getPreviousToolType(), TOOL_PEN);

    handler.selectTool(TOOL_ERASER);  // already the current tool
    EXPECT_EQ(handler.getPreviousToolType(), TOOL_PEN) << "re-selecting the current tool is not a change";
    EXPECT_NE(handler.getPreviousToolType(), handler.getToolType()) << "never the current tool";
}

TEST(ToolHandlerPreviousToolTest, theEffectiveToolChangesAreRememberedToo) {
    QuietListener listener;
    ToolHandler handler(&listener, nullptr, nullptr);

    handler.selectTool(TOOL_HIGHLIGHTER);
    ASSERT_EQ(handler.getPreviousToolType(), TOOL_PEN);

    // A stylus button points the active tool at another tool without changing the toolbar's choice.
    ASSERT_TRUE(handler.pointActiveToolToToolType(TOOL_ERASER));
    EXPECT_EQ(handler.getPreviousToolType(), TOOL_HIGHLIGHTER);
    EXPECT_EQ(handler.getToolType(), TOOL_ERASER);

    // And pointing back at the toolbar's tool remembers the button tool that was in the way.
    ASSERT_TRUE(handler.pointActiveToolToToolbarTool());
    EXPECT_EQ(handler.getPreviousToolType(), TOOL_ERASER);
    EXPECT_EQ(handler.getToolType(), TOOL_HIGHLIGHTER);
}

TEST(ToolHandlerPreviousToolTest, pointingAtTheToolAlreadyActiveRemembersNothing) {
    QuietListener listener;
    ToolHandler handler(&listener, nullptr, nullptr);

    handler.selectTool(TOOL_PEN);
    EXPECT_EQ(handler.getPreviousToolType(), TOOL_NONE);

    // The active tool is already the pen: nothing changed, so nothing is remembered.
    EXPECT_FALSE(handler.pointActiveToolToToolType(TOOL_PEN));
    EXPECT_EQ(handler.getPreviousToolType(), TOOL_NONE);
}
