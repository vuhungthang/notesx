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
