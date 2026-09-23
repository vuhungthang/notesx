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

#include <cstddef>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>
#include <gtk/gtk.h>

#include "../dialog/GtkTest.h"
#include "control/Control.h"
#include "control/ScrollHandler.h"
#include "gui/GladeSearchpath.h"
#include "gui/MainWindow.h"
#include "gui/XournalView.h"
#include "model/Document.h"
#include "model/PageRef.h"
#include "model/XojPage.h"
#include "undo/UndoRedoHandler.h"

#include "config-test.h"

/*
 * Plan 005, step 4: the multi page operations of the page navigator, on a real Control.
 *
 * The navigator's arithmetic is covered by the unit tests of PageSelectionModel, but the operations
 * themselves are not arithmetic: they edit the document, and every edit fires page events that the
 * canvas, the layout and the navigator all react to - scrolling, rebuilding page lists and
 * selecting pages. The unit tests cannot see any of that, and the three defects found in review all
 * lived in it:
 *
 *  - deleting several pages read the selection model's own vector while the deletions were firing
 *    events that emptied it, which aborted under the hardened libstdc++ of the build;
 *  - reordering pages lost the selection and the current page to the page events the move fires;
 *  - duplicating several pages selected the wrong copies.
 *
 * So these tests drive the operations through a Control, a document and a canvas built the way the
 * application builds them, and compare what the user sees afterwards: the pages, the selection and
 * the page the editor shows, each named by the page it was before the operation.
 */

namespace {

constexpr size_t PAGES = 6;
constexpr size_t NPOS = static_cast<size_t>(-1);

/// Lets GTK finish what it queued, so the canvas and the navigator settle into the visible state.
void settle() {
    for (int i = 0; i < 4; i++) {
        while (g_main_context_iteration(nullptr, FALSE)) {}
        g_usleep(20000);
    }
}

/// Runs what the main context has ready until it has nothing left.
///
/// A widget that queues a one-shot callback and is gone before it runs leaves a source in the main
/// context holding a pointer to the dead widget, which the next test's first settle() then calls.
/// Draining here, while the window is still alive, is what keeps a window that is being torn down
/// from leaving anything of itself behind for the next one.
void drain() {
    for (int i = 0; i < 50 && g_main_context_pending(nullptr); i++) {
        while (g_main_context_iteration(nullptr, FALSE)) {}
        g_usleep(10000);
    }
}

/// A page of the document, named by where it was before the operation: "x" is a copy a test made.
auto name(size_t original) -> std::string { return original == NPOS ? std::string("x") : std::to_string(original); }

/// Everything a scenario compares: the pages, the selection and what the editor shows.
struct State {
    size_t pageCount = 0;
    /// The document's pages, in order, as the pages they were before the operation.
    std::string order;
    /// The selected pages, as the pages they were before the operation.
    std::string selection;
    /// The model's current page: its index, and the page it was before the operation.
    size_t modelCurrent = NPOS;
    size_t modelCurrentPage = NPOS;
    /// What the editor shows, the same way.
    size_t viewCurrent = NPOS;
    size_t viewCurrentPage = NPOS;

    auto str() const -> std::string {
        std::ostringstream out;
        out << "pages=" << pageCount << " order=" << order << " selection=" << selection << " modelCurrent=page"
            << modelCurrent << "(" << name(modelCurrentPage) << ") viewCurrent=page" << viewCurrent << "("
            << name(viewCurrentPage) << ")";
        return out.str();
    }
};

class PageOperations;

/// One case of the matrix: what the user selected, and what the operation has to leave behind.
struct Scenario {
    const char* name;
    void (*run)(PageOperations& operations);
};

/// Named by the case, not by the bytes of the struct.
void PrintTo(const Scenario& scenario, std::ostream* out) { *out << scenario.name; }

/**
 * A window with a Control and a document of six pages, exactly like the application builds one.
 *
 * The pages are inserted through `Control::insertPage()`, so the document, the canvas, the layout
 * and the navigator all see them, and the page the user is on is reached the way a card click
 * reaches it: `pageSelectionClicked()` (which is what the navigator calls), then the scroll, then
 * the page selected event.
 */
class PageOperations: public GtkTest, public ::testing::WithParamInterface<Scenario> {
public:
    /// The original page sitting at the document index `index`, or NPOS when it is a copy.
    auto originalAt(size_t index) const -> size_t {
        if (index == NPOS || index >= this->doc->getPageCount()) {
            return NPOS;
        }
        const PageRef page = this->doc->getPage(index);
        for (size_t i = 0; i < this->originals.size(); i++) {
            if (this->originals[i] == page) {
                return i;
            }
        }
        return NPOS;
    }

    /// The index the original page `original` has now.
    auto indexOf(size_t original) const -> size_t {
        return original < this->originals.size() ? this->doc->indexOf(this->originals[original]) : NPOS;
    }

    auto state() const -> State {
        State state;
        state.pageCount = this->doc->getPageCount();

        std::ostringstream order;
        order << "[";
        for (size_t i = 0; i < state.pageCount; i++) {
            order << (i == 0 ? "" : ",") << name(this->originalAt(i));
        }
        order << "]";
        state.order = order.str();

        const std::vector<size_t>& selection = this->control->getPageSelection().getSelection();
        std::ostringstream selected;
        selected << "{";
        for (size_t i = 0; i < selection.size(); i++) {
            selected << (i == 0 ? "" : ",") << name(this->originalAt(selection[i]));
        }
        selected << "}";
        state.selection = selected.str();

        state.modelCurrent = this->control->getPageSelection().getCurrentPage();
        state.modelCurrentPage = this->originalAt(state.modelCurrent);
        state.viewCurrent = this->control->getCurrentPageNo();
        state.viewCurrentPage = this->originalAt(state.viewCurrent);
        return state;
    }

    /// The same click a card click makes: SidebarPreviewPages::onCardClicked().
    void click(size_t page, bool controlPressed = false, bool shiftPressed = false) {
        this->control->pageSelectionClicked(page, controlPressed, shiftPressed);
        if (this->control->getCurrentPageNo() != page) {
            this->control->getScrollHandler()->jumpToPage(this->originals[page]);
        }
        this->control->firePageSelected(page);
        settle();
    }

    /// Clicks the pages one by one, the way a user builds a selection.
    void select(const std::vector<size_t>& pages) {
        for (size_t i = 0; i < pages.size(); i++) {
            this->click(pages[i], i != 0, false);
        }
    }

    /// Puts the editor on `page` without touching the selection, as a scroll the navigator did not
    /// make does.
    void putEditorOn(size_t page) {
        this->control->getPageSelection().setCurrentPageWithoutSelecting(page);
        this->control->getScrollHandler()->scrollToPage(page);
        settle();
    }

    void selectAll() { this->control->getPageSelection().selectAll(this->doc->getPageCount()); }

    void deleteSelection() { this->control->deleteSelectedPages(); }
    /// Exactly what the toolbar button and the menu item run.
    void runDeletePageAction() { this->control->deletePage(); }
    void duplicateSelection() { this->control->duplicateSelectedPages(); }
    void moveToBeginning() { this->control->moveSelectedPagesTowardsBeginning(); }
    void moveToEnd() { this->control->moveSelectedPagesTowardsEnd(); }

    void undo() {
        this->control->getUndoRedoHandler()->undo();
        settle();
    }
    void redo() {
        this->control->getUndoRedoHandler()->redo();
        settle();
    }

    /// The page counts and indices of a State have to make sense on their own, whatever the
    /// operation was meant to do.
    void expectConsistent(const State& state) const {
        EXPECT_GT(state.pageCount, 0U) << "a document always keeps at least one page: " << state.str();
        EXPECT_LT(state.modelCurrent, state.pageCount) << "the model's current page stays in range: " << state.str();
        EXPECT_LT(state.viewCurrent, state.pageCount) << "the editor stays in range: " << state.str();
        EXPECT_EQ(state.modelCurrent, state.viewCurrent)
                << "the model and the editor show the same page: " << state.str();
    }

protected:
    std::unique_ptr<GladeSearchpath> glade;
    std::unique_ptr<Control> control;
    std::unique_ptr<MainWindow> win;
    std::vector<PageRef> originals;
    Document* doc = nullptr;

    /**
     * The whole test: GtkTest runs it inside a GtkApplication, which is what a Control and a
     * MainWindow need, and the scenario of the parameter runs there too - there is no GtkApplication
     * left by the time the test body would run.
     */
    void runTest(GtkApplication* app) final {
        this->glade = std::make_unique<GladeSearchpath>();
        this->glade->addSearchDirectory(GET_UI_FOLDER);
        // The page templates the Control's page type handler reads. Without them it tells the user
        // about the missing file, through a message box, in the middle of a test.
        this->glade->addSearchDirectory(GET_PAGE_TEMPLATE_FOLDER);

        this->control = std::make_unique<Control>(G_APPLICATION(app), this->glade.get(), true);
        this->win = std::make_unique<MainWindow>(this->glade.get(), this->control.get(), GTK_APPLICATION(app));
        this->control->initWindow(this->win.get());
        this->win->populate(this->glade.get());
        this->win->show(nullptr);
        settle();

        // Six pages, built the way the application builds them on "new page".
        for (size_t i = 0; i < PAGES; i++) {
            auto page = std::make_shared<XojPage>(595.28, 841.89);
            this->originals.emplace_back(page);
            this->control->insertPage(page, i, false);
        }
        settle();
        this->win->getXournal()->layoutPages();
        settle();

        // The inserts added undo actions; the scenarios start from a clean history.
        this->control->getUndoRedoHandler()->clearContents();
        this->control->getPageSelection().reset(PAGES);
        this->control->getScrollHandler()->scrollToPage(0);
        settle();

        this->doc = this->control->getDocument();
        ASSERT_EQ(this->doc->getPageCount(), PAGES) << "the test document was built";
        ASSERT_EQ(this->state().selection, "{0}") << "the document starts on its first page";

        this->GetParam().run(*this);

        // Everything this window queued has to have run before it goes: see drain().
        drain();

        // The window takes its theme subscriptions off the settings as it goes: the GtkSettings
        // outlive it and hold it as the handler data, so a subscription left behind has the next
        // window in this process - the next case, on the same settings - call into a window that is
        // gone. Nothing else here removes it, so what the settings still hold is what is checked.
        GtkSettings* settings = gtk_widget_get_settings(GTK_WIDGET(this->win->getWindow()));
        MainWindow* gone = this->win.get();

        this->win.reset();

        EXPECT_EQ(g_signal_handler_find(settings, G_SIGNAL_MATCH_DATA, 0, 0, nullptr, nullptr, gone), gulong(0))
                << "the destroyed window left a subscription on the theme settings";
        this->control.reset();
        this->glade.reset();
    }
};

/*
 * Deleting several pages at once. Every deletion fires a page event, and the selection model is one
 * of the listeners that reacts to it - which is what the delete loop used to read its own indices
 * out of.
 */

/// The noncontiguous case from the review: the loop must not read the vector it is emptying.
void deleteNoncontiguousSelection(PageOperations& ops) {
    ops.select({1, 2, 4});
    ASSERT_EQ(ops.state().selection, "{1,2,4}");

    ops.deleteSelection();
    const State state = ops.state();
    EXPECT_EQ(state.pageCount, 3U) << state.str();
    EXPECT_EQ(state.order, "[0,3,5]") << state.str();
    EXPECT_EQ(state.selection, "{3}") << "the page that took the first deleted one's place: " << state.str();
    EXPECT_EQ(state.viewCurrentPage, 3U) << "the editor lands on that same page: " << state.str();
    ops.expectConsistent(state);

    // One undo puts all three back, in their original order.
    ops.undo();
    const State undone = ops.state();
    EXPECT_EQ(undone.pageCount, 6U) << undone.str();
    EXPECT_EQ(undone.order, "[0,1,2,3,4,5]") << undone.str();
    ops.expectConsistent(undone);
}

/// The first pages of the document, including page 0, which cannot be deleted below itself.
void deleteFirstPages(PageOperations& ops) {
    ops.select({0, 1});
    ops.deleteSelection();

    const State state = ops.state();
    EXPECT_EQ(state.pageCount, 4U) << state.str();
    EXPECT_EQ(state.order, "[2,3,4,5]") << state.str();
    EXPECT_EQ(state.viewCurrentPage, 2U) << state.str();
    ops.expectConsistent(state);
}

/// The last pages, where the landing index is past the end of what is left.
void deleteLastPages(PageOperations& ops) {
    ops.select({4, 5});
    ops.deleteSelection();

    const State state = ops.state();
    EXPECT_EQ(state.pageCount, 4U) << state.str();
    EXPECT_EQ(state.order, "[0,1,2,3]") << state.str();
    EXPECT_EQ(state.viewCurrentPage, 3U) << state.str();
    ops.expectConsistent(state);
}

/// Every other page: the deletions do not touch each other's indices.
void deleteEveryOtherPage(PageOperations& ops) {
    ops.select({1, 3, 5});
    ops.deleteSelection();

    const State state = ops.state();
    EXPECT_EQ(state.pageCount, 3U) << state.str();
    EXPECT_EQ(state.order, "[0,2,4]") << state.str();
    ops.expectConsistent(state);
}

/// The same operation through the page action, which is what the toolbar and the menu run.
void deleteSelectionThroughThePageAction(PageOperations& ops) {
    ops.select({1, 2, 4});
    ops.runDeletePageAction();

    const State state = ops.state();
    EXPECT_EQ(state.pageCount, 3U) << state.str();
    EXPECT_EQ(state.order, "[0,3,5]") << state.str();
    ops.expectConsistent(state);
}

/// Everything but one page: allowed, and one page is what is left.
void deleteAllButOne(PageOperations& ops) {
    ops.select({0, 1, 2, 3, 4});
    ops.deleteSelection();

    const State state = ops.state();
    EXPECT_EQ(state.pageCount, 1U) << state.str();
    EXPECT_EQ(state.order, "[5]") << state.str();
    EXPECT_EQ(state.viewCurrentPage, 5U) << state.str();
    ops.expectConsistent(state);
}

/// Every page: refused, because a document always keeps at least one page.
void deleteEveryPageIsRefused(PageOperations& ops) {
    ops.selectAll();
    ASSERT_EQ(ops.state().selection, "{0,1,2,3,4,5}");

    ops.deleteSelection();
    const State state = ops.state();
    EXPECT_EQ(state.pageCount, 6U) << "the document survives: " << state.str();
    EXPECT_EQ(state.order, "[0,1,2,3,4,5]") << state.str();
    ops.expectConsistent(state);
}

/*
 * Duplicating several pages at once. The copies are inserted from the last page backwards, and each
 * insertion moves the copies made after it - which is what the returned indices have to account
 * for.
 */

/// The noncontiguous case from the review: the copies of 2 and 4 are at 3 and 6.
void duplicateNoncontiguousSelection(PageOperations& ops) {
    ops.select({2, 4});
    ops.duplicateSelection();

    const State state = ops.state();
    EXPECT_EQ(state.pageCount, 8U) << state.str();
    EXPECT_EQ(state.order, "[0,1,2,x,3,4,x,5]") << "a copy directly below each original: " << state.str();
    EXPECT_EQ(state.selection, "{x,x}") << "the copies are what is selected: " << state.str();
    EXPECT_EQ(ops.originalAt(3), NPOS) << "the copy of page 2 is at index 3: " << state.str();
    EXPECT_EQ(ops.originalAt(6), NPOS) << "the copy of page 4 is at index 6: " << state.str();
    EXPECT_EQ(state.viewCurrentPage, 4U) << "the editor stays on the page the user was on: " << state.str();
    ops.expectConsistent(state);

    ops.undo();
    const State undone = ops.state();
    EXPECT_EQ(undone.pageCount, 6U) << "one undo removes both copies: " << undone.str();
    EXPECT_EQ(undone.order, "[0,1,2,3,4,5]") << undone.str();
}

/// Adjacent pages: the second copy is moved down by the first one.
void duplicateAdjacentSelection(PageOperations& ops) {
    ops.select({1, 2});
    ops.duplicateSelection();

    const State state = ops.state();
    EXPECT_EQ(state.pageCount, 8U) << state.str();
    EXPECT_EQ(state.order, "[0,1,x,2,x,3,4,5]") << state.str();
    EXPECT_EQ(state.selection, "{x,x}") << state.str();
    EXPECT_EQ(ops.originalAt(2), NPOS) << state.str();
    EXPECT_EQ(ops.originalAt(4), NPOS) << state.str();
    ops.expectConsistent(state);
}

/// One page: the plain case, where the copy really is at `page + 1`.
void duplicateOnePage(PageOperations& ops) {
    ops.select({3});
    ops.duplicateSelection();

    const State state = ops.state();
    EXPECT_EQ(state.pageCount, 7U) << state.str();
    EXPECT_EQ(state.order, "[0,1,2,3,x,4,5]") << state.str();
    EXPECT_EQ(state.selection, "{x}") << state.str();
    ops.expectConsistent(state);
}

/// Every page: the document doubles, one copy below each page.
void duplicateEveryPage(PageOperations& ops) {
    ops.selectAll();
    ops.duplicateSelection();

    const State state = ops.state();
    EXPECT_EQ(state.pageCount, 12U) << state.str();
    EXPECT_EQ(state.order, "[0,x,1,x,2,x,3,x,4,x,5,x]") << state.str();
    EXPECT_EQ(state.selection, "{x,x,x,x,x,x}") << "every copy is selected: " << state.str();
    for (size_t copy: {size_t(1), size_t(3), size_t(5), size_t(7), size_t(9), size_t(11)}) {
        EXPECT_EQ(ops.originalAt(copy), NPOS) << "index " << copy << " is a copy: " << state.str();
    }
    ops.expectConsistent(state);
}

/*
 * Moving the selected pages. The move is emitted as a delete/insert pair per page, and the canvas
 * scrolls on every one of them - the selection and the page the editor shows have to survive that.
 */

/// The noncontiguous case from the review: two pages moved to the front together.
void moveNoncontiguousSelectionToBeginning(PageOperations& ops) {
    ops.select({2, 5});
    ASSERT_EQ(ops.state().modelCurrentPage, 5U);

    ops.moveToBeginning();
    const State state = ops.state();
    EXPECT_EQ(state.order, "[0,2,5,1,3,4]") << "the two pages move together, keeping their order: " << state.str();
    EXPECT_EQ(state.selection, "{2,5}") << "the same pages stay selected: " << state.str();
    EXPECT_EQ(state.modelCurrentPage, 5U) << "the current page keeps its identity: " << state.str();
    EXPECT_EQ(state.viewCurrentPage, 5U) << "and the editor shows it: " << state.str();
    ops.expectConsistent(state);
}

/// The same selection moved to the end.
void moveNoncontiguousSelectionToEnd(PageOperations& ops) {
    ops.select({1, 3});
    ASSERT_EQ(ops.state().modelCurrentPage, 3U);

    ops.moveToEnd();
    const State state = ops.state();
    EXPECT_EQ(state.order, "[0,2,4,1,3,5]") << state.str();
    EXPECT_EQ(state.selection, "{1,3}") << state.str();
    EXPECT_EQ(state.modelCurrentPage, 3U) << state.str();
    EXPECT_EQ(state.viewCurrentPage, 3U) << state.str();
    ops.expectConsistent(state);
}

/// A contiguous range, built with a Shift-click.
void moveContiguousSelectionToBeginning(PageOperations& ops) {
    ops.click(1);
    ops.click(3, false, true);
    ASSERT_EQ(ops.state().selection, "{1,2,3}");

    ops.moveToBeginning();
    const State state = ops.state();
    EXPECT_EQ(state.order, "[1,2,3,0,4,5]") << state.str();
    EXPECT_EQ(state.selection, "{1,2,3}") << state.str();
    EXPECT_EQ(state.modelCurrentPage, 3U) << state.str();
    ops.expectConsistent(state);
}

/// One selected page moved to the end, with the editor somewhere else entirely.
void moveSelectionWithTheEditorElsewhereToEnd(PageOperations& ops) {
    ops.select({0});
    ops.putEditorOn(4);
    const State before = ops.state();
    ASSERT_EQ(before.selection, "{0}") << before.str();
    ASSERT_EQ(before.modelCurrentPage, 4U) << before.str();

    ops.moveToEnd();
    const State state = ops.state();
    EXPECT_EQ(state.order, "[1,0,2,3,4,5]") << state.str();
    EXPECT_EQ(state.selection, "{0}") << "the selected page is still selected: " << state.str();
    EXPECT_EQ(state.modelCurrentPage, 4U) << "the page the user was on did not move: " << state.str();
    EXPECT_EQ(state.viewCurrentPage, 4U) << state.str();
    ops.expectConsistent(state);
}

/// Undo and redo put back the page order, the selection and the page the editor shows.
void reorderUndoAndRedoRestoreTheNavigatorState(PageOperations& ops) {
    ops.select({2, 5});
    ops.moveToBeginning();
    ASSERT_EQ(ops.state().order, "[0,2,5,1,3,4]");

    ops.undo();
    const State undone = ops.state();
    EXPECT_EQ(undone.order, "[0,1,2,3,4,5]") << "undo restores the page order: " << undone.str();
    EXPECT_EQ(undone.selection, "{2,5}") << "and the pages that were selected: " << undone.str();
    EXPECT_EQ(undone.modelCurrentPage, 5U) << "and the page the user was on: " << undone.str();
    EXPECT_EQ(undone.viewCurrentPage, 5U) << undone.str();
    ops.expectConsistent(undone);

    ops.redo();
    const State redone = ops.state();
    EXPECT_EQ(redone.order, "[0,2,5,1,3,4]") << "redo restores the moved order: " << redone.str();
    EXPECT_EQ(redone.selection, "{2,5}") << redone.str();
    EXPECT_EQ(redone.modelCurrentPage, 5U) << redone.str();
    EXPECT_EQ(redone.viewCurrentPage, 5U) << redone.str();
    ops.expectConsistent(redone);
}

/// The same for a contiguous range, whose current page is one of the moved ones.
void contiguousReorderUndoAndRedoRestoreTheNavigatorState(PageOperations& ops) {
    ops.click(1);
    ops.click(3, false, true);
    ops.moveToEnd();
    const State moved = ops.state();
    ASSERT_EQ(moved.order, "[0,4,1,2,3,5]") << "the range moved one page towards the end: " << moved.str();
    EXPECT_EQ(moved.selection, "{1,2,3}") << moved.str();
    EXPECT_EQ(moved.modelCurrentPage, 3U) << "the page the user was on moved with the range: " << moved.str();
    EXPECT_EQ(moved.viewCurrentPage, 3U) << moved.str();

    ops.undo();
    const State undone = ops.state();
    EXPECT_EQ(undone.order, "[0,1,2,3,4,5]") << undone.str();
    EXPECT_EQ(undone.selection, "{1,2,3}") << undone.str();
    EXPECT_EQ(undone.modelCurrentPage, 3U) << undone.str();
    EXPECT_EQ(undone.viewCurrentPage, 3U) << undone.str();
    ops.expectConsistent(undone);

    ops.redo();
    const State redone = ops.state();
    EXPECT_EQ(redone.order, "[0,4,1,2,3,5]") << redone.str();
    EXPECT_EQ(redone.selection, "{1,2,3}") << redone.str();
    EXPECT_EQ(redone.modelCurrentPage, 3U) << redone.str();
    EXPECT_EQ(redone.viewCurrentPage, 3U) << redone.str();
    ops.expectConsistent(redone);
}

}  // namespace

/*
 * The scenario of the parameter is the test: the fixture has to be built inside a GtkApplication,
 * so what a case does and checks runs from `PageOperations::runTest()` and the body below is empty.
 * Every case is a parameter, so a failure names the case it belongs to.
 */
TEST_P(PageOperations, theScenario) {}

INSTANTIATE_TEST_SUITE_P(
        PageOperationScenarios, PageOperations,
        ::testing::Values(
                Scenario{"deleteNoncontiguousSelection", &deleteNoncontiguousSelection},
                Scenario{"deleteFirstPages", &deleteFirstPages}, Scenario{"deleteLastPages", &deleteLastPages},
                Scenario{"deleteEveryOtherPage", &deleteEveryOtherPage},
                Scenario{"deleteSelectionThroughThePageAction", &deleteSelectionThroughThePageAction},
                Scenario{"deleteAllButOne", &deleteAllButOne},
                Scenario{"deleteEveryPageIsRefused", &deleteEveryPageIsRefused},
                Scenario{"duplicateNoncontiguousSelection", &duplicateNoncontiguousSelection},
                Scenario{"duplicateAdjacentSelection", &duplicateAdjacentSelection},
                Scenario{"duplicateOnePage", &duplicateOnePage}, Scenario{"duplicateEveryPage", &duplicateEveryPage},
                Scenario{"moveNoncontiguousSelectionToBeginning", &moveNoncontiguousSelectionToBeginning},
                Scenario{"moveNoncontiguousSelectionToEnd", &moveNoncontiguousSelectionToEnd},
                Scenario{"moveContiguousSelectionToBeginning", &moveContiguousSelectionToBeginning},
                Scenario{"moveSelectionWithTheEditorElsewhereToEnd", &moveSelectionWithTheEditorElsewhereToEnd},
                Scenario{"reorderUndoAndRedoRestoreTheNavigatorState", &reorderUndoAndRedoRestoreTheNavigatorState},
                Scenario{"contiguousReorderUndoAndRedoRestoreTheNavigatorState",
                         &contiguousReorderUndoAndRedoRestoreTheNavigatorState}),
        [](const ::testing::TestParamInfo<Scenario>& info) -> std::string { return info.param.name; });
