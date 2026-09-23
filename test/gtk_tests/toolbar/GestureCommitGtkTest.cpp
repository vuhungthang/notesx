/*
 * Xournal++
 *
 * Plan 008 steps 3-5: the gesture commit seam on a real Control and document
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>
#include <gtk/gtk.h>  // for GtkApplication

#include "../../unit_tests/control/GestureFixture.h"  // for loadGestureFixture
#include "../dialog/GtkTest.h"
#include "control/Control.h"                     // for Control
#include "control/ToolEnums.h"                   // for TOOL_PEN
#include "control/ToolHandler.h"                 // for ToolHandler
#include "control/gestures/GestureCommit.h"      // for commitStylusGesture
#include "control/gestures/GestureSettings.h"    // for GestureSettings
#include "control/gestures/GestureStroke.h"      // for GestureStroke
#include "control/settings/Settings.h"           // for Settings
#include "control/tools/StrokeHandler.h"         // for StrokeHandler (the seam's caller)
#include "gui/GladeSearchpath.h"                 // for GladeSearchpath
#include "gui/MainWindow.h"                      // for MainWindow
#include "gui/TipService.h"                      // for TipService (Plan 008, step 7)
#include "gui/inputdevices/PositionInputData.h"  // for PositionInputData
#include "model/Document.h"                      // for Document
#include "model/Layer.h"                         // for Layer
#include "model/Point.h"                         // for Point
#include "model/Stroke.h"                        // for Stroke
#include "model/XojPage.h"                       // for XojPage
#include "undo/UndoRedoHandler.h"                // for UndoRedoHandler

#include "config-test.h"

/*
 * Plan 008, steps 3-5: what a recognised gesture does to the document.
 *
 * The recognizers and policy are covered without a document by the unit tests; what can only be
 * checked here is the seam itself: that a gesture that passes the policy suppresses its own ink,
 * erases the whole strokes it covers as exactly one undo group, that one Undo restores them, that
 * a gesture which covers nothing leaves the ink alone, and that a gesture whose setting is off
 * leaves the ordinary ink path untouched.
 *
 * These tests drive the same function the input handler calls (commitStylusGesture) against a real
 * Control, Document, Layer and UndoRedoHandler, and one of them drives the input handler itself
 * (StrokeHandler::onButtonReleaseEvent) so the wiring at the call site is covered too.
 */

using xoj::gesture::GestureCommitOutcome;
using xoj::gesture::GestureCommitResult;
using xoj::gesture::GestureDecision;
using xoj::gesture::GestureIgnoreReason;
using xoj::gesture::GestureKind;
using xoj::gesture::GestureSettings;
using xoj::gesture::GestureStroke;

namespace {

namespace fs = std::filesystem;

/// Lets GTK finish what it queued, so the window and the canvas have settled.
void settle() {
    for (int i = 0; i < 4; i++) {
        while (g_main_context_iteration(nullptr, FALSE)) {}
        g_usleep(20000);
    }
}

auto loadFixture(const char* relative) -> GestureStroke {
    const auto path = fs::path(GET_TESTFILE(u8"gestures")) / fs::path(relative);
    auto fixture = xoj::gesture::test::loadGestureFixture(path);
    EXPECT_TRUE(fixture.has_value()) << "missing fixture " << relative;
    return fixture ? fixture->stroke : GestureStroke();
}

class GestureCommitGtkFixture:
        public GtkTest,
        public ::testing::WithParamInterface<void (*)(GestureCommitGtkFixture&)> {
public:
    /// Turn the scribble-to-erase gesture on or off in the live settings; takes effect at once.
    void enableScribble(bool enabled) {
        GestureSettings settings = this->control->getSettings()->getGestureSettings();
        settings.scribbleToEraseEnabled = enabled;
        this->control->getSettings()->setGestureSettings(settings);
    }
    void enableCircle(bool enabled) {
        GestureSettings settings = this->control->getSettings()->getGestureSettings();
        settings.circleToSelectEnabled = enabled;
        this->control->getSettings()->setGestureSettings(settings);
    }
    void liveSettings(GestureSettings settings) { this->control->getSettings()->setGestureSettings(settings); }

    /**
     * Plan 008, step 7: a profile that may be told about a gesture again, with the notices on.
     *
     * Whether a tip has been shown is remembered in the profile and the profile outlives one case,
     * so a case about a notice says where it starts from; without that it would be reading what the
     * case before it left behind.
     */
    void prepareNotices() {
        Settings* settings = this->control->getSettings();
        settings->resetInterfaceTips();
        settings->setInterfaceTipsEnabled(true);
        GestureSettings gesture = settings->getGestureSettings();
        gesture.feedbackEnabled = true;
        settings->setGestureSettings(gesture);
    }

    /// The window's tip service, where the notices about gestures are offered.
    auto tips() const -> xoj::gui::TipService* { return this->win->getTipService(); }

    auto page() const -> PageRef { return this->control->getDocument()->getPage(0); }
    auto layer() const -> Layer* { return this->page()->getSelectedLayer(); }

    /// How many elements the page's layer holds.
    auto elementCount() const -> std::size_t { return this->layer()->getElementsView().size(); }

    /// Adds a straight pen stroke to the page's layer and returns it.
    auto addStroke(double x, double y, double dx, double dy) -> const Stroke* {
        auto stroke = std::make_unique<Stroke>();
        stroke->setToolType(StrokeTool::PEN);
        stroke->setWidth(1.5);
        stroke->addPoint(Point(x, y));
        stroke->addPoint(Point(x + dx / 2.0, y + dy / 2.0));
        stroke->addPoint(Point(x + dx, y + dy));
        const Stroke* raw = stroke.get();

        Document* doc = this->control->getDocument();
        doc->lock();
        this->layer()->addElement(std::move(stroke));
        doc->unlock();
        return raw;
    }

    /// Drives the real input handler over the fixture's points, at the fixture's own coordinates.
    void drawThroughStrokeHandler(const GestureStroke& stroke) {
        StrokeHandler handler(this->control.get(), this->page());

        PositionInputData pos{};
        pos.pressure = Point::NO_PRESSURE;
        pos.timestamp = 0;
        pos.state = static_cast<GdkModifierType>(0);

        const auto& points = stroke.getPoints();
        ASSERT_FALSE(points.empty());

        pos.x = points.front().x;
        pos.y = points.front().y;
        handler.onButtonPressEvent(pos, 1.0);

        for (const auto& p: points) {
            pos.x = p.x;
            pos.y = p.y;
            handler.onMotionNotifyEvent(pos, 1.0);
        }

        pos.x = points.back().x;
        pos.y = points.back().y;
        handler.onButtonReleaseEvent(pos, 1.0);
        settle();
    }

    auto undo() {
        this->control->getUndoRedoHandler()->undo();
        settle();
    }
    auto redo() {
        this->control->getUndoRedoHandler()->redo();
        settle();
    }
    void clearUndo() { this->control->getUndoRedoHandler()->clearContents(); }

protected:
    void runTest(GtkApplication* app) final {
        this->glade = std::make_unique<GladeSearchpath>();
        this->glade->addSearchDirectory(GET_UI_FOLDER);
        this->glade->addSearchDirectory(GET_PAGE_TEMPLATE_FOLDER);

        this->control = std::make_unique<Control>(G_APPLICATION(app), this->glade.get(), true);
        this->win = std::make_unique<MainWindow>(this->glade.get(), this->control.get(), GTK_APPLICATION(app));
        this->control->initWindow(this->win.get());
        this->win->populate(this->glade.get());
        this->win->show(nullptr);
        settle();

        this->control->insertPage(std::make_shared<XojPage>(595.28, 841.89), 0, false);
        settle();

        // The pen, so a stroke the input handler builds is a pen stroke.
        this->control->getToolHandler()->selectTool(TOOL_PEN);
        this->clearUndo();

        this->GetParam()(*this);
    }

public:
    std::unique_ptr<GladeSearchpath> glade;
    std::unique_ptr<Control> control;
    std::unique_ptr<MainWindow> win;
};

/*
 * The setting off leaves the ordinary ink path untouched: no recognizer looks at the stroke, no
 * action runs, and nothing about the document or the history changes.
 */
void disabledGesturesLeaveTheDocumentAlone(GestureCommitGtkFixture& test) {
    test.liveSettings(GestureSettings::defaults());
    test.clearUndo();
    test.addStroke(260.0, 300.0, 30.0, 10.0);
    ASSERT_EQ(test.elementCount(), 1U);

    const GestureCommitResult result = xoj::gesture::commitStylusGesture(
            test.control->getSettings()->getGestureSettings(), *test.control, test.page(), test.layer(),
            loadFixture("scribble/positive-dense-scrub.txt"));

    EXPECT_EQ(result.outcome, GestureCommitOutcome::OrdinaryInk);
    EXPECT_FALSE(result.recognized) << "a disabled gesture is not even recognised";
    EXPECT_EQ(test.elementCount(), 1U) << "ordinary ink is left exactly as it was";
    EXPECT_FALSE(test.control->getUndoRedoHandler()->canUndo()) << "and nothing was recorded";
}

/*
 * A confirmed scribble erases every whole stroke it covers, leaves everything else, and records the
 * whole thing as one undo step: one Undo brings all the erased strokes back.
 */
void aConfirmedScribbleErasesAsOneUndoGroup(GestureCommitGtkFixture& test) {
    test.liveSettings(GestureSettings::defaults());
    test.enableScribble(true);
    test.clearUndo();

    test.addStroke(260.0, 300.0, 30.0, 10.0);  // covered
    test.addStroke(300.0, 320.0, 30.0, 0.0);   // covered
    test.addStroke(10.0, 10.0, 40.0, 40.0);    // nowhere near the scrub
    ASSERT_EQ(test.elementCount(), 3U);

    const GestureCommitResult result = xoj::gesture::commitStylusGesture(
            test.control->getSettings()->getGestureSettings(), *test.control, test.page(), test.layer(),
            loadFixture("scribble/positive-dense-scrub.txt"));

    EXPECT_EQ(result.outcome, GestureCommitOutcome::Committed);
    ASSERT_TRUE(result.recognized);
    EXPECT_EQ(result.kind, GestureKind::Scribble);
    EXPECT_EQ(result.decision, GestureDecision::Confirm);
    EXPECT_EQ(result.affectedElements, 2U) << "only the covered strokes were erased";
    EXPECT_EQ(test.elementCount(), 1U) << "the stroke outside the scrub is untouched";

    UndoRedoHandler* history = test.control->getUndoRedoHandler();
    ASSERT_TRUE(history->canUndo());
    EXPECT_EQ(history->undoDescription(), "Undo: Erase stroke");

    test.undo();
    EXPECT_EQ(test.elementCount(), 3U) << "one Undo restores every erased stroke at once";
    EXPECT_FALSE(test.control->getUndoRedoHandler()->canUndo()) << "the whole gesture was a single undo step";

    test.redo();
    EXPECT_EQ(test.elementCount(), 1U) << "one Redo removes them all again";
}

/*
 * A gesture that covers nothing the document holds does not run: the ink it was drawn with stays
 * ink, and no undo step is recorded.
 */
void aGestureThatCoversNothingLeavesInkAlone(GestureCommitGtkFixture& test) {
    test.liveSettings(GestureSettings::defaults());
    test.enableScribble(true);
    test.clearUndo();

    test.addStroke(10.0, 10.0, 40.0, 40.0);  // far from the scrub's region
    ASSERT_EQ(test.elementCount(), 1U);

    const GestureCommitResult result = xoj::gesture::commitStylusGesture(
            test.control->getSettings()->getGestureSettings(), *test.control, test.page(), test.layer(),
            loadFixture("scribble/positive-dense-scrub.txt"));

    EXPECT_EQ(result.outcome, GestureCommitOutcome::OrdinaryInk);
    ASSERT_TRUE(result.recognized);
    EXPECT_EQ(result.decision, GestureDecision::Ignore);
    EXPECT_EQ(result.reason, GestureIgnoreReason::NoOverlap);
    EXPECT_EQ(test.elementCount(), 1U) << "nothing was erased";
    EXPECT_FALSE(test.control->getUndoRedoHandler()->canUndo()) << "and nothing was recorded";
}

/*
 * Circle-to-select cannot run: a selection is not a document edit in this application (there is no
 * selection undo action), so the action cannot be one undo group, and an action without an undo
 * record is what the plan forbids. The policy refuses it and the circle stays ink - which is the
 * STOP this slice hit, written as a test so it cannot regress silently.
 */
void circleToSelectIsRefusedForWantOfAnUndoGroup(GestureCommitGtkFixture& test) {
    test.liveSettings(GestureSettings::defaults());
    test.enableCircle(true);
    test.clearUndo();

    test.addStroke(250.0, 250.0, 40.0, 40.0);  // something for the circle to cover
    ASSERT_EQ(test.elementCount(), 1U);

    const GestureCommitResult result = xoj::gesture::commitStylusGesture(
            test.control->getSettings()->getGestureSettings(), *test.control, test.page(), test.layer(),
            loadFixture("circle/positive-large-circle.txt"));

    EXPECT_EQ(result.outcome, GestureCommitOutcome::OrdinaryInk) << "the circle is left as ink";
    ASSERT_TRUE(result.recognized);
    EXPECT_EQ(result.kind, GestureKind::Circle);
    EXPECT_EQ(result.decision, GestureDecision::Ignore);
    EXPECT_EQ(result.reason, GestureIgnoreReason::NoUndoGroup);
    EXPECT_EQ(test.elementCount(), 1U);
    EXPECT_FALSE(test.control->getUndoRedoHandler()->canUndo());
}

/*
 * The call site: the input handler runs the seam before the stroke becomes ink. With the gesture on
 * and a scrub over ink, the scrub's own ink is suppressed and the covered strokes are erased as one
 * undo step; with the gesture off, the very same stroke becomes ordinary ink.
 */
void theInputHandlerSuppressesScrubInkAndErasesAsOneUndoGroup(GestureCommitGtkFixture& test) {
    const GestureStroke scrub = loadFixture("scribble/positive-dense-scrub.txt");

    test.liveSettings(GestureSettings::defaults());
    test.enableScribble(true);
    test.clearUndo();
    test.addStroke(260.0, 300.0, 30.0, 10.0);
    test.addStroke(300.0, 320.0, 30.0, 0.0);
    ASSERT_EQ(test.elementCount(), 2U);

    test.drawThroughStrokeHandler(scrub);

    EXPECT_EQ(test.elementCount(), 0U) << "the covered strokes are erased and the scrub's own ink is not committed";
    UndoRedoHandler* history = test.control->getUndoRedoHandler();
    ASSERT_TRUE(history->canUndo());
    EXPECT_EQ(history->undoDescription(), "Undo: Erase stroke");

    test.undo();
    EXPECT_EQ(test.elementCount(), 2U) << "one Undo restores both strokes";
    EXPECT_FALSE(test.control->getUndoRedoHandler()->canUndo());
}

void theInputHandlerWritesOrdinaryInkWhenTheGestureIsOff(GestureCommitGtkFixture& test) {
    const GestureStroke scrub = loadFixture("scribble/positive-dense-scrub.txt");

    test.liveSettings(GestureSettings::defaults());  // off, as it ships
    test.clearUndo();
    ASSERT_EQ(test.elementCount(), 0U);

    test.drawThroughStrokeHandler(scrub);

    EXPECT_EQ(test.elementCount(), 1U) << "the stroke is ordinary ink when the gesture is off";
    ASSERT_TRUE(test.control->getUndoRedoHandler()->canUndo());
    EXPECT_EQ(test.control->getUndoRedoHandler()->undoDescription(), "Undo: Draw stroke");
}

/*
 * Plan 008, step 7: the first time a gesture acts, the application says so - naming the gesture that
 * just did something, saying it can be undone, and offering to turn that gesture off. The undo was
 * already checked above; here what matters is that the notice appears only where the gesture acted
 * and that its offer is a way to do less, not more.
 */
void theFirstScribbleSaysSoAndOffersToTurnItOff(GestureCommitGtkFixture& test) {
    test.prepareNotices();
    test.enableScribble(true);
    test.clearUndo();
    test.addStroke(260.0, 300.0, 30.0, 10.0);
    ASSERT_EQ(test.elementCount(), 1U);

    test.drawThroughStrokeHandler(loadFixture("scribble/positive-dense-scrub.txt"));

    ASSERT_EQ(test.elementCount(), 0U) << "the gesture acted, so there is an undo record to speak of";
    ASSERT_TRUE(test.control->getUndoRedoHandler()->canUndo());
    ASSERT_NE(test.tips(), nullptr) << "the window offers tips";
    EXPECT_TRUE(test.tips()->isShown()) << "the first successful gesture says so";
    EXPECT_EQ(test.tips()->shownTip(), xoj::gui::TipService::Tip::GestureScribbleToErase);

    // The offer beside the dismissal: turning this gesture off, which is the safe direction.
    GtkWidget* turnOff = test.tips()->getTurnOffButton();
    ASSERT_NE(turnOff, nullptr);
    EXPECT_TRUE(gtk_widget_is_visible(turnOff)) << "a gesture notice offers to turn that gesture off";
    ASSERT_TRUE(test.control->getSettings()->getGestureSettings().scribbleToEraseEnabled);

    gtk_button_clicked(GTK_BUTTON(turnOff));

    EXPECT_FALSE(test.control->getSettings()->getGestureSettings().scribbleToEraseEnabled)
            << "taking the offer turns the gesture off";
    EXPECT_FALSE(test.tips()->isShown()) << "and the notice goes";
    EXPECT_TRUE(test.control->getSettings()->hasSeenTip(
            xoj::gui::TipService::idOf(xoj::gui::TipService::Tip::GestureScribbleToErase)))
            << "and is not shown again";
}

/// A gesture that did not act says nothing: the stroke became ordinary ink and there is no notice.
void aGestureThatDoesNotActSaysNothing(GestureCommitGtkFixture& test) {
    test.prepareNotices();
    test.enableScribble(true);
    test.clearUndo();
    test.addStroke(10.0, 10.0, 40.0, 40.0);  // nowhere near the scrub's region

    test.drawThroughStrokeHandler(loadFixture("scribble/positive-dense-scrub.txt"));

    ASSERT_EQ(test.elementCount(), 2U) << "the scrub became ordinary ink beside the untouched stroke";
    EXPECT_FALSE(test.tips()->isShown()) << "nothing is said about a gesture that did not act";
}

/// And a user who has switched the notices off is not told anything, though the gesture still acts.
void nothingIsSaidWhenTheNoticesAreOff(GestureCommitGtkFixture& test) {
    test.prepareNotices();
    GestureSettings settings = test.control->getSettings()->getGestureSettings();
    settings.feedbackEnabled = false;
    test.liveSettings(settings);
    test.enableScribble(true);
    test.clearUndo();
    test.addStroke(260.0, 300.0, 30.0, 10.0);
    test.addStroke(300.0, 320.0, 30.0, 0.0);

    test.drawThroughStrokeHandler(loadFixture("scribble/positive-dense-scrub.txt"));

    ASSERT_EQ(test.elementCount(), 0U) << "the gesture still acts with the notices off";
    ASSERT_TRUE(test.control->getUndoRedoHandler()->canUndo());
    EXPECT_FALSE(test.tips()->isShown()) << "it just does not say so";
}

/// A notice is offered once: after it has been put away, the same gesture acts silently.
void theNoticeIsOnlyOfferedOnce(GestureCommitGtkFixture& test) {
    test.prepareNotices();
    test.enableScribble(true);
    test.clearUndo();

    test.addStroke(260.0, 300.0, 30.0, 10.0);
    test.drawThroughStrokeHandler(loadFixture("scribble/positive-dense-scrub.txt"));
    ASSERT_TRUE(test.tips()->isShown());
    test.tips()->dismiss();
    settle();
    ASSERT_FALSE(test.tips()->isShown());

    test.addStroke(260.0, 300.0, 30.0, 10.0);
    test.drawThroughStrokeHandler(loadFixture("scribble/positive-dense-scrub.txt"));

    EXPECT_FALSE(test.tips()->isShown()) << "the user has been told about this gesture already";
}

}  // namespace

INSTANTIATE_TEST_SUITE_P(
        GestureCommit, GestureCommitGtkFixture,
        ::testing::Values(&disabledGesturesLeaveTheDocumentAlone, &aConfirmedScribbleErasesAsOneUndoGroup,
                          &aGestureThatCoversNothingLeavesInkAlone, &circleToSelectIsRefusedForWantOfAnUndoGroup,
                          &theInputHandlerSuppressesScrubInkAndErasesAsOneUndoGroup,
                          &theInputHandlerWritesOrdinaryInkWhenTheGestureIsOff,
                          &theFirstScribbleSaysSoAndOffersToTurnItOff, &aGestureThatDoesNotActSaysNothing,
                          &nothingIsSaidWhenTheNoticesAreOff, &theNoticeIsOnlyOfferedOnce));

/// The scenario runs inside the fixture's runTest, where the application is up.
TEST_P(GestureCommitGtkFixture, theScenario) {}
