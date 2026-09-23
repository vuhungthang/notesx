/*
 * Xournal++
 *
 * The Plan 008 gesture commit seam, without a Control (Plan 008, steps 3 and 5)
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "control/gestures/GestureCommit.h"
#include "control/gestures/GestureSettings.h"
#include "control/gestures/GestureStroke.h"
#include "model/Document.h"
#include "model/Element.h"
#include "model/Layer.h"
#include "model/Point.h"
#include "model/Stroke.h"
#include "model/XojPage.h"
#include "undo/UndoRedoHandler.h"

#include "GestureFixture.h"

namespace {

namespace fs = std::filesystem;

using namespace xoj::gesture;

/// A pen stroke through the given points, ready to be put in a layer.
auto makeStroke(const std::vector<std::pair<double, double>>& points) -> std::unique_ptr<Stroke> {
    auto stroke = std::make_unique<Stroke>();
    stroke->setToolType(StrokeTool::PEN);
    stroke->setWidth(1.0);
    for (const auto& [x, y]: points) {
        stroke->addPoint(Point(x, y));
    }
    return stroke;
}

/// A short straight stroke from (x, y) to (x + dx, y + dy).
auto makeSegment(double x, double y, double dx, double dy) -> std::unique_ptr<Stroke> {
    return makeStroke({{x, y}, {x + dx / 2, y + dy / 2}, {x + dx, y + dy}});
}

auto loadFixture(const char* relative) -> GestureStroke {
    const auto path = fs::path(GET_TESTFILE(u8"gestures")) / fs::path(relative);
    auto fixture = xoj::gesture::test::loadGestureFixture(path);
    EXPECT_TRUE(fixture.has_value()) << "missing fixture " << relative;
    return fixture ? fixture->stroke : GestureStroke();
}

/// A page with a single layer, the way a fresh document's first page is.
auto makePage() -> PageRef { return std::make_shared<XojPage>(595.28, 841.89); }

}  // namespace

/*
 * The recognizer is only asked about the gestures the settings leave on. A disabled gesture produces
 * no candidate at all, so there is nothing for the policy to consider - which is the first of the two
 * places a gesture is gated.
 */
TEST(GestureCommitTest, recognizeAsksNothingWhenBothGesturesAreOff) {
    const GestureSettings settings = GestureSettings::defaults();
    const GestureCommit commit(settings);

    EXPECT_FALSE(commit.anyEnabled());
    EXPECT_FALSE(commit.recognize(loadFixture("scribble/positive-dense-scrub.txt")).has_value());
    EXPECT_FALSE(commit.recognize(loadFixture("circle/positive-large-circle.txt")).has_value());
}

/*
 * A disabled scribble must not be recognised even though the stroke is a scribble and the circle
 * gesture is on: the recognizer is gated by the gesture the candidate belongs to.
 */
TEST(GestureCommitTest, scribbleIsNotRecognisedWhileItsGestureIsOff) {
    GestureSettings settings = GestureSettings::defaults();
    settings.circleToSelectEnabled = true;  // on, but the stroke is not a circle
    const GestureCommit commit(settings);

    EXPECT_FALSE(commit.recognize(loadFixture("scribble/positive-dense-scrub.txt")).has_value());
}

TEST(GestureCommitTest, recogniseFindsAnEnabledScribble) {
    GestureSettings settings = GestureSettings::defaults();
    settings.scribbleToEraseEnabled = true;
    const GestureCommit commit(settings);

    ASSERT_TRUE(commit.anyEnabled());
    const auto candidate = commit.recognize(loadFixture("scribble/positive-dense-scrub.txt"));
    ASSERT_TRUE(candidate.has_value());
    EXPECT_EQ(candidate->kind, GestureKind::Scribble);
    EXPECT_GE(candidate->confidence, 0.5);
}

TEST(GestureCommitTest, recognizeFindsAnEnabledCircle) {
    GestureSettings settings = GestureSettings::defaults();
    settings.circleToSelectEnabled = true;
    const GestureCommit commit(settings);

    const auto candidate = commit.recognize(loadFixture("circle/positive-large-circle.txt"));
    ASSERT_TRUE(candidate.has_value());
    EXPECT_EQ(candidate->kind, GestureKind::Circle);
}

/*
 * The documented erase rule: a stroke is erased whole if its bounding box overlaps the gesture's
 * region, and left alone otherwise. Never a partial erase, and nothing but strokes.
 */
TEST(GestureCommitTest, strokesIntersectingPicksWholeStrokesByBoundingBox) {
    const PageRef page = makePage();
    Layer* layer = page->getSelectedLayer();

    auto inside = makeSegment(260.0, 300.0, 30.0, 10.0);  // entirely inside the region
    auto partial = makeSegment(240.0, 290.0, 40.0, 0.0);  // overlaps the region's left edge
    auto outside = makeSegment(10.0, 10.0, 20.0, 20.0);   // nowhere near it
    const Element* insidePtr = inside.get();
    const Element* partialPtr = partial.get();
    const Element* outsidePtr = outside.get();
    layer->addElement(std::move(inside));
    layer->addElement(std::move(partial));
    layer->addElement(std::move(outside));

    const GestureBounds region{250.0, 252.0, 350.0, 348.0};
    const std::vector<const Stroke*> hits = strokesIntersecting(layer, region);

    EXPECT_EQ(hits.size(), 2U);
    EXPECT_NE(std::find(hits.begin(), hits.end(), insidePtr), hits.end());
    EXPECT_NE(std::find(hits.begin(), hits.end(), partialPtr), hits.end());
    EXPECT_EQ(std::find(hits.begin(), hits.end(), outsidePtr), hits.end());
}

/*
 * One call removes every covered stroke and leaves exactly one undo record for all of them, so the
 * history shows a single step for the whole gesture. A call that removes nothing records nothing.
 */
TEST(GestureCommitTest, eraseStrokesMakesOneRecordForAllOfThem) {
    const PageRef page = makePage();
    Layer* layer = page->getSelectedLayer();
    Document doc(nullptr);
    UndoRedoHandler undo(nullptr);

    auto first = makeSegment(260.0, 300.0, 30.0, 0.0);
    auto second = makeSegment(300.0, 320.0, 30.0, 0.0);
    const Stroke* firstPtr = first.get();
    const Stroke* secondPtr = second.get();
    layer->addElement(std::move(first));
    layer->addElement(std::move(second));

    const std::size_t removed = eraseStrokesAsOneUndoGroup(page, layer, {firstPtr, secondPtr}, &undo, doc);

    EXPECT_EQ(removed, 2U);
    EXPECT_EQ(layer->getElementsView().size(), 0U) << "both strokes were removed";
    EXPECT_TRUE(undo.canUndo()) << "the whole gesture is one undoable step";
    EXPECT_FALSE(undo.canRedo());
    EXPECT_EQ(undo.undoDescription(), "Undo: Erase stroke") << "the whole gesture is one erase step";
}

TEST(GestureCommitTest, erasingNothingRecordsNothing) {
    const PageRef page = makePage();
    Layer* layer = page->getSelectedLayer();
    Document doc(nullptr);
    UndoRedoHandler undo(nullptr);

    const std::size_t removed = eraseStrokesAsOneUndoGroup(page, layer, {}, &undo, doc);

    EXPECT_EQ(removed, 0U);
    EXPECT_FALSE(undo.canUndo()) << "an undo step that does nothing must not be recorded";
}
