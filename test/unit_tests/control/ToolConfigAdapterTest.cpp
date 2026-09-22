/*
 * Xournal++
 *
 * This file is part of the Xournal UnitTests
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#include <algorithm>   // for find_if
#include <functional>  // for function
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "control/ToolConfigAdapter.h"
#include "control/ToolHandler.h"
#include "control/ToolPreset.h"
#include "model/StrokeStyle.h"  // for StrokeStyle::parseStyle, formatStyle

/*
 * Plan 003, step 2: one adapter reads the effective tool configuration, applies a preset and
 * notifies every representation once.
 *
 * ToolHandler is used without a Control and without an ActionDatabase: the parts of it these
 * tests exercise (selecting a tool, colour, size, fill, drawing type, eraser mode and the
 * notifications they emit) do not need either. That keeps the test about the notification and
 * application contract instead of about the application's object graph.
 */

namespace {

/// Records what ToolHandler reports, so a change can be counted.
class StubToolListener: public ToolListener {
public:
    void toolColorChanged() override { this->colorChanges++; }
    void changeColorOfSelection() override { this->selectionColorChanges++; }
    void toolSizeChanged() override { this->sizeChanges++; }
    void toolFillChanged() override { this->fillChanges++; }
    void toolLineStyleChanged() override { this->lineStyleChanges++; }
    void toolChanged() override { this->toolChanges++; }

    int colorChanges = 0;
    int selectionColorChanges = 0;
    int sizeChanges = 0;
    int fillChanges = 0;
    int lineStyleChanges = 0;
    int toolChanges = 0;
};

/// Records every update it receives, and optionally writes back to prove that cannot loop.
class RecordingObserver: public ToolConfigObserver {
public:
    void toolConfigChanged(const ToolConfigState& state) override {
        this->states.emplace_back(state);
        if (this->onUpdate) {
            this->onUpdate(this->states.size());
        }
    }

    std::size_t updateCount() const { return this->states.size(); }
    const ToolConfigState& last() const { return this->states.back(); }

    std::vector<ToolConfigState> states;
    std::function<void(std::size_t)> onUpdate;
};

/// A pen preset that touches every part of the pen configuration.
auto penPreset() -> ToolPreset {
    return ToolPreset{.id = "preset-1",
                      .name = "Teal thick rectangle pen",
                      .toolType = TOOL_PEN,
                      .color = Color(0xff008080U),
                      .size = TOOL_SIZE_THICK,
                      .drawingType = DRAWING_TYPE_RECTANGLE,
                      .fill = 200};
}

/// The parts of the configuration the adapter is expected to read back.
void expectStateMatches(const ToolConfigState& state, const ToolPreset& preset) {
    EXPECT_EQ(state.toolType, preset.toolType);
    ASSERT_TRUE(state.hasColor);
    ASSERT_TRUE(preset.color.has_value());
    EXPECT_EQ(state.color, *preset.color);
    ASSERT_TRUE(state.hasSize);
    ASSERT_TRUE(preset.size.has_value());
    EXPECT_EQ(state.size, *preset.size);
    ASSERT_TRUE(state.hasDrawingType);
    ASSERT_TRUE(preset.drawingType.has_value());
    EXPECT_EQ(state.drawingType, *preset.drawingType);
    ASSERT_TRUE(state.hasFill);
    ASSERT_TRUE(preset.fill.has_value());
    EXPECT_EQ(state.fill, *preset.fill);
}

}  // namespace

TEST(ToolConfigAdapterTest, testReadsTheEffectiveConfigurationOfTheActiveTool) {
    StubToolListener listener;
    ToolHandler handler(&listener, nullptr, nullptr);
    ToolConfigAdapter adapter(handler);

    handler.selectTool(TOOL_PEN);
    handler.fireToolChanged();

    ToolConfigState pen = adapter.getState();
    EXPECT_EQ(pen.toolType, TOOL_PEN);
    EXPECT_TRUE(pen.hasColor);
    EXPECT_TRUE(pen.hasSize);
    EXPECT_TRUE(pen.hasFill);
    EXPECT_TRUE(pen.hasDrawingType);
    EXPECT_TRUE(pen.hasLineStyle);
    EXPECT_FALSE(pen.hasEraserType);

    handler.selectTool(TOOL_ERASER);
    handler.fireToolChanged();

    ToolConfigState eraser = adapter.getState();
    EXPECT_EQ(eraser.toolType, TOOL_ERASER);
    EXPECT_FALSE(eraser.hasColor);
    EXPECT_TRUE(eraser.hasSize);
    EXPECT_FALSE(eraser.hasFill);
    EXPECT_FALSE(eraser.hasDrawingType);
    EXPECT_FALSE(eraser.hasLineStyle);
    EXPECT_TRUE(eraser.hasEraserType);
    EXPECT_EQ(eraser.eraserType, ERASER_TYPE_DEFAULT);
}

TEST(ToolConfigAdapterTest, testApplyingAPresetChangesEveryApplicablePart) {
    StubToolListener listener;
    ToolHandler handler(&listener, nullptr, nullptr);
    ToolConfigAdapter adapter(handler);

    ASSERT_TRUE(adapter.applyPreset(penPreset()));

    EXPECT_EQ(handler.getToolType(), TOOL_PEN);
    EXPECT_EQ(handler.getColor(), Color(0xff008080U));
    EXPECT_EQ(handler.getSize(), TOOL_SIZE_THICK);
    EXPECT_EQ(handler.getDrawingType(), DRAWING_TYPE_RECTANGLE);
    EXPECT_EQ(handler.getFill(), 200);

    expectStateMatches(adapter.getState(), penPreset());
}

TEST(ToolConfigAdapterTest, testApplyingAnEraserPresetKeepsTheEraserMode) {
    StubToolListener listener;
    ToolHandler handler(&listener, nullptr, nullptr);
    ToolConfigAdapter adapter(handler);

    const ToolPreset preset{.id = "preset-2",
                            .name = "Delete stroke eraser",
                            .toolType = TOOL_ERASER,
                            .size = TOOL_SIZE_THICK,
                            .eraserType = ERASER_TYPE_DELETE_STROKE};

    ASSERT_TRUE(adapter.applyPreset(preset));

    EXPECT_EQ(handler.getToolType(), TOOL_ERASER);
    EXPECT_EQ(handler.getEraserSize(), TOOL_SIZE_THICK);
    EXPECT_EQ(handler.getEraserType(), ERASER_TYPE_DELETE_STROKE);
}

TEST(ToolConfigAdapterTest, testAnIncompletePresetIsRejectedWithoutChangingAnything) {
    StubToolListener listener;
    ToolHandler handler(&listener, nullptr, nullptr);
    ToolConfigAdapter adapter(handler);

    const ToolConfigState before = adapter.getState();

    EXPECT_FALSE(adapter.applyPreset(ToolPreset{.id = "preset-3", .name = "No tool"}));
    EXPECT_EQ(adapter.getState(), before);
}

TEST(ToolConfigAdapterTest, testApplyingAPresetNotifiesEveryObserverOnce) {
    StubToolListener listener;
    ToolHandler handler(&listener, nullptr, nullptr);
    ToolConfigAdapter adapter(handler);

    RecordingObserver first;
    RecordingObserver second;
    adapter.addObserver(&first);
    adapter.addObserver(&second);

    ASSERT_TRUE(adapter.applyPreset(penPreset()));

    // One coherent update each, not one per part of the configuration that changed.
    ASSERT_EQ(first.updateCount(), 1U);
    ASSERT_EQ(second.updateCount(), 1U);
    EXPECT_EQ(first.last(), second.last());
    expectStateMatches(first.last(), penPreset());

    // ToolHandler reported the change to Control once per part as well.
    EXPECT_EQ(listener.toolChanges, 1);
    EXPECT_EQ(listener.colorChanges, 1);
    EXPECT_EQ(listener.sizeChanges, 1);
    EXPECT_EQ(listener.fillChanges, 1);
    // Applying a preset must never be written into the current selection.
    EXPECT_EQ(listener.selectionColorChanges, 0);
}

TEST(ToolConfigAdapterTest, testBothEntryPathsProduceTheSameCoherentUpdate) {
    // Path one: the adapter applies a whole preset.
    StubToolListener presetListener;
    ToolHandler presetHandler(&presetListener, nullptr, nullptr);
    ToolConfigAdapter presetAdapter(presetHandler);
    RecordingObserver presetObserver;
    presetAdapter.addObserver(&presetObserver);
    ASSERT_TRUE(presetAdapter.applyPreset(penPreset()));

    // Path two: a keyboard shortcut or a stylus button changes the same parts one by one. The
    // change is written through the same ToolHandler paths, coalesced the same way.
    StubToolListener stepListener;
    ToolHandler stepHandler(&stepListener, nullptr, nullptr);
    ToolConfigAdapter stepAdapter(stepHandler);
    RecordingObserver stepObserver;
    stepAdapter.addObserver(&stepObserver);
    {
        ToolHandler::CoalescedUpdate update(stepHandler);
        stepHandler.selectTool(TOOL_PEN);
        stepHandler.fireToolChanged();
        stepHandler.setColor(Color(0xff008080U), false);
        stepHandler.setSize(TOOL_SIZE_THICK);
        stepHandler.setDrawingType(DRAWING_TYPE_RECTANGLE);
        stepHandler.setPenFill(200);
        stepHandler.setPenFillEnabled(true);
    }

    // Both paths end in the same configuration and report it exactly once.
    EXPECT_EQ(presetAdapter.getState(), stepAdapter.getState());
    ASSERT_EQ(presetObserver.updateCount(), 1U);
    ASSERT_EQ(stepObserver.updateCount(), 1U);
    EXPECT_EQ(presetObserver.last(), stepObserver.last());
}

TEST(ToolConfigAdapterTest, testAnObserverThatChangesTheConfigurationDoesNotLoop) {
    StubToolListener listener;
    ToolHandler handler(&listener, nullptr, nullptr);
    ToolConfigAdapter adapter(handler);

    RecordingObserver observer;
    // The first update makes the observer change the size, which is a change of its own. It must
    // be reported in a second round rather than by recursing, and it must stop there.
    observer.onUpdate = [&handler](std::size_t round) {
        if (round == 1) {
            handler.setSize(TOOL_SIZE_VERY_THICK);
        }
    };
    adapter.addObserver(&observer);

    ASSERT_TRUE(adapter.applyPreset(penPreset()));

    EXPECT_EQ(observer.updateCount(), 2U);
    EXPECT_EQ(observer.last().size, TOOL_SIZE_VERY_THICK);
    EXPECT_EQ(handler.getSize(), TOOL_SIZE_VERY_THICK);
}

TEST(ToolConfigAdapterTest, testRemovedObserversAreNotNotified) {
    StubToolListener listener;
    ToolHandler handler(&listener, nullptr, nullptr);
    ToolConfigAdapter adapter(handler);

    RecordingObserver observer;
    adapter.addObserver(&observer);
    adapter.removeObserver(&observer);

    ASSERT_TRUE(adapter.applyPreset(penPreset()));

    EXPECT_EQ(observer.updateCount(), 0U);
}

TEST(ToolConfigAdapterTest, testCaptureAndApplyRoundTrip) {
    StubToolListener listener;
    ToolHandler handler(&listener, nullptr, nullptr);
    ToolConfigAdapter adapter(handler);

    ASSERT_TRUE(adapter.applyPreset(penPreset()));
    const ToolConfigState expected = adapter.getState();

    const ToolPreset captured = adapter.capturePreset("My pen");
    EXPECT_EQ(captured.name, "My pen");
    EXPECT_TRUE(captured.isValid());
    ASSERT_TRUE(captured.color.has_value());
    EXPECT_EQ(*captured.color, expected.color);
    ASSERT_TRUE(captured.size.has_value());
    EXPECT_EQ(*captured.size, expected.size);
    ASSERT_TRUE(captured.drawingType.has_value());
    EXPECT_EQ(*captured.drawingType, expected.drawingType);
    ASSERT_TRUE(captured.fill.has_value());
    EXPECT_EQ(*captured.fill, expected.fill);

    // Change everything, then go back through the captured preset.
    handler.selectTool(TOOL_HIGHLIGHTER);
    handler.fireToolChanged();
    handler.setSize(TOOL_SIZE_VERY_FINE);

    ASSERT_TRUE(adapter.applyPreset(captured));
    EXPECT_EQ(adapter.getState(), expected);
}

TEST(ToolConfigAdapterTest, testACapturedEraserPresetCarriesNoColour) {
    StubToolListener listener;
    ToolHandler handler(&listener, nullptr, nullptr);
    ToolConfigAdapter adapter(handler);

    handler.selectTool(TOOL_ERASER);
    handler.fireToolChanged();
    handler.setEraserType(ERASER_TYPE_WHITEOUT);

    const ToolPreset captured = adapter.capturePreset("Whiteout");

    EXPECT_EQ(captured.toolType, TOOL_ERASER);
    EXPECT_FALSE(captured.color.has_value());
    EXPECT_FALSE(captured.fill.has_value());
    EXPECT_FALSE(captured.drawingType.has_value());
    ASSERT_TRUE(captured.eraserType.has_value());
    EXPECT_EQ(*captured.eraserType, ERASER_TYPE_WHITEOUT);
}

TEST(ToolConfigAdapterTest, testASingleChangeIsReportedOnce) {
    StubToolListener listener;
    ToolHandler handler(&listener, nullptr, nullptr);
    ToolConfigAdapter adapter(handler);

    RecordingObserver observer;
    adapter.addObserver(&observer);

    handler.setColor(Colors::red, false);

    ASSERT_EQ(observer.updateCount(), 1U);
    EXPECT_EQ(observer.last().color, Colors::red);
    EXPECT_EQ(listener.colorChanges, 1);
}

TEST(ToolConfigAdapterTest, testWidthPreviewsUseTheThicknessOfTheSizeTheyShow) {
    StubToolListener listener;
    ToolHandler handler(&listener, nullptr, nullptr);
    ToolConfigAdapter adapter(handler);

    // The thicknesses are the ones ToolHandler reports, and they grow with the size.
    const double veryFine = adapter.getThickness(TOOL_PEN, TOOL_SIZE_VERY_FINE);
    const double medium = adapter.getThickness(TOOL_PEN, TOOL_SIZE_MEDIUM);
    const double veryThick = adapter.getThickness(TOOL_PEN, TOOL_SIZE_VERY_THICK);

    EXPECT_GT(veryFine, 0.0);
    EXPECT_GT(medium, veryFine);
    EXPECT_GT(veryThick, medium);

    // The eraser and the highlighter have their own tables.
    EXPECT_GT(adapter.getThickness(TOOL_HIGHLIGHTER, TOOL_SIZE_MEDIUM), 0.0);
    EXPECT_GT(adapter.getThickness(TOOL_ERASER, TOOL_SIZE_MEDIUM), 0.0);
}

TEST(ToolConfigAdapterTest, testWidthPreviewsRefuseToolsWithoutAThicknessTable) {
    StubToolListener listener;
    ToolHandler handler(&listener, nullptr, nullptr);
    ToolConfigAdapter adapter(handler);

    // Asking ToolHandler for the thickness of these tools asserts, so the adapter has to answer
    // on its own rather than forward the question.
    EXPECT_EQ(adapter.getThickness(TOOL_TEXT, TOOL_SIZE_MEDIUM), 0.0);
    EXPECT_EQ(adapter.getThickness(TOOL_NONE, TOOL_SIZE_MEDIUM), 0.0);
    EXPECT_EQ(adapter.getThickness(TOOL_PEN, TOOL_SIZE_NONE), 0.0);
}

TEST(ToolConfigAdapterTest, testSelectingAToolThroughTheAdapterIsReportedOnce) {
    StubToolListener listener;
    ToolHandler handler(&listener, nullptr, nullptr);
    ToolConfigAdapter adapter(handler);

    RecordingObserver observer;
    adapter.addObserver(&observer);

    ASSERT_EQ(adapter.getState().toolType, TOOL_PEN);
    adapter.selectTool(TOOL_ERASER);

    EXPECT_EQ(adapter.getState().toolType, TOOL_ERASER);
    ASSERT_EQ(observer.updateCount(), 1U);
    EXPECT_EQ(observer.last().toolType, TOOL_ERASER);
    EXPECT_EQ(listener.toolChanges, 1);
}

TEST(ToolConfigAdapterTest, testSelectingTheToolThatIsAlreadyActiveChangesNothing) {
    StubToolListener listener;
    ToolHandler handler(&listener, nullptr, nullptr);
    ToolConfigAdapter adapter(handler);

    RecordingObserver observer;
    adapter.addObserver(&observer);

    adapter.selectTool(TOOL_PEN);

    EXPECT_EQ(adapter.getState().toolType, TOOL_PEN);
    EXPECT_EQ(observer.updateCount(), 0U);
    EXPECT_EQ(listener.toolChanges, 0);
}

TEST(ToolConfigAdapterTest, testTheLineStyleIsPartOfAPreset) {
    StubToolListener listener;
    ToolHandler handler(&listener, nullptr, nullptr);
    ToolConfigAdapter adapter(handler);

    handler.setLineStyle(StrokeStyle::parseStyle("dash"));
    handler.fireToolChanged();

    const ToolPreset captured = adapter.capturePreset("Dashed pen");
    ASSERT_TRUE(captured.lineStyle.has_value());
    EXPECT_EQ(*captured.lineStyle, "dash");
    EXPECT_EQ(adapter.getState().lineStyle, "dash");

    // Applying it puts the line style back through the same ToolHandler path.
    ASSERT_TRUE(adapter.applyPreset(
            ToolPreset{.name = "Plain pen", .toolType = TOOL_PEN, .lineStyle = std::string("plain")}));
    EXPECT_EQ(adapter.getState().lineStyle, "plain");
    EXPECT_EQ(StrokeStyle::formatStyle(handler.getLineStyle()), "plain");
}

TEST(ToolConfigAdapterTest, testAnEraserPresetCarriesNoLineStyle) {
    StubToolListener listener;
    ToolHandler handler(&listener, nullptr, nullptr);
    ToolConfigAdapter adapter(handler);

    handler.selectTool(TOOL_ERASER);
    handler.fireToolChanged();

    EXPECT_FALSE(adapter.capturePreset("Eraser").lineStyle.has_value());
}

TEST(ToolConfigAdapterTest, testAnObserverMayRemoveAnotherObserverWhileBeingNotified) {
    StubToolListener listener;
    ToolHandler handler(&listener, nullptr, nullptr);
    ToolConfigAdapter adapter(handler);

    // The first observer destroys the second one. This is what happens when a change rebuilds a
    // toolbar: the popover that lives in the old one goes away while the notification is running.
    RecordingObserver victim;
    RecordingObserver remover;
    auto victimPtr = &victim;
    remover.onUpdate = [&adapter, victimPtr](std::size_t) {
        if (adapter.getState().toolType == TOOL_ERASER) {
            adapter.removeObserver(victimPtr);
        }
    };

    adapter.addObserver(&victim);
    adapter.addObserver(&remover);

    handler.setColor(Colors::red, false);
    EXPECT_EQ(victim.updateCount(), 1U);
    EXPECT_EQ(remover.updateCount(), 1U);

    handler.selectTool(TOOL_ERASER);
    handler.fireToolChanged();

    EXPECT_EQ(remover.updateCount(), 2U);
    // The victim is notified in the round in which it is removed - it comes first in the list -
    // but it must not be reached again after that.
    const std::size_t victimCountWhenRemoved = victim.updateCount();

    handler.setColor(Colors::green, false);

    EXPECT_EQ(victim.updateCount(), victimCountWhenRemoved) << "a removed observer must not be notified again";
    EXPECT_EQ(remover.updateCount(), 3U);
}

TEST(ToolConfigAdapterTest, testTheNotificationRoundsAreBounded) {
    StubToolListener listener;
    ToolHandler handler(&listener, nullptr, nullptr);
    ToolConfigAdapter adapter(handler);

    // An observer that never stops changing the configuration is a bug in the observer. The
    // adapter has to give up rather than hang the UI.
    int rounds = 0;
    RecordingObserver observer;
    observer.onUpdate = [&adapter, &rounds](std::size_t) {
        if (++rounds < 50) {
            adapter.selectTool(adapter.getState().toolType == TOOL_PEN ? TOOL_ERASER : TOOL_PEN);
        }
    };
    adapter.addObserver(&observer);

    handler.setColor(Colors::red, false);

    EXPECT_GE(rounds, 1);
    EXPECT_LE(rounds, 4) << "one change may take at most MAX_NOTIFICATION_ROUNDS rounds";
}
