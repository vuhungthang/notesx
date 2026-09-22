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
#include <vector>

#include <gtest/gtest.h>

#include "model/PageSelectionModel.h"

/*
 * Plan 005, step 1: the page selection model, on its own.
 *
 * These tests pin the semantics the navigator relies on: Ctrl toggles one page, Shift extends from
 * the anchor, a document navigation replaces the selection unless the page is already part of it,
 * and the selection survives insertions, deletions and reorders with the right indices.
 */

namespace {

using xoj::model::PageSelectionModel;

auto selection(const PageSelectionModel& model) -> std::vector<size_t> { return model.getSelection(); }

/// A model on a 10 page document with page 0 selected, which is what a fresh document looks like.
auto freshModel(size_t pageCount = 10) -> PageSelectionModel {
    PageSelectionModel model;
    model.reset(pageCount);
    return model;
}

}  // namespace

TEST(PageSelectionModel, FreshDocumentSelectsFirstPage) {
    PageSelectionModel model = freshModel(4);

    EXPECT_EQ(selection(model), (std::vector<size_t>{0}));
    EXPECT_EQ(model.getCurrentPage(), 0u);
    EXPECT_TRUE(model.isSelected(0));
    EXPECT_TRUE(model.isCurrentPage(0));
    EXPECT_EQ(model.getAnchor(), 0u);
    EXPECT_FALSE(model.empty());
}

TEST(PageSelectionModel, EmptyDocumentHasNoSelection) {
    PageSelectionModel model = freshModel(0);

    EXPECT_TRUE(model.empty());
    EXPECT_EQ(model.getCurrentPage(), PageSelectionModel::npos);
    EXPECT_EQ(model.getAnchor(), PageSelectionModel::npos);
}

TEST(PageSelectionModel, ReplaceWithSelectsExactlyOnePage) {
    PageSelectionModel model = freshModel();

    model.replaceWith(6);

    EXPECT_EQ(selection(model), (std::vector<size_t>{6}));
    EXPECT_EQ(model.getCurrentPage(), 6u);
    EXPECT_FALSE(model.isSelected(0));
    EXPECT_EQ(model.getAnchor(), 6u);
}

TEST(PageSelectionModel, ToggleAddsAndRemovesOnePage) {
    PageSelectionModel model = freshModel();
    model.replaceWith(2);

    model.toggle(5);
    EXPECT_EQ(selection(model), (std::vector<size_t>{2, 5}));
    EXPECT_EQ(model.getCurrentPage(), 2u) << "toggling must not move the current page";

    model.toggle(2);
    EXPECT_EQ(selection(model), (std::vector<size_t>{5}));

    model.toggle(5);
    EXPECT_TRUE(model.empty());
    EXPECT_EQ(model.getAnchor(), PageSelectionModel::npos);
}

TEST(PageSelectionModel, ToggleMovesTheAnchorToTheClickedPage) {
    PageSelectionModel model = freshModel();
    model.replaceWith(1);

    model.toggle(4);
    EXPECT_EQ(model.getAnchor(), 4u);

    // The Shift-click after the toggle extends from the page that was clicked, not from the page
    // the selection started at.
    model.extendTo(6);
    EXPECT_EQ(selection(model), (std::vector<size_t>{4, 5, 6}));
}

TEST(PageSelectionModel, RangeSelectionExtendsForward) {
    PageSelectionModel model = freshModel();
    model.replaceWith(2);

    model.extendTo(5);

    EXPECT_EQ(selection(model), (std::vector<size_t>{2, 3, 4, 5}));
    EXPECT_EQ(model.getCurrentPage(), 5u);
    EXPECT_EQ(model.getAnchor(), 2u) << "the anchor must stay where the range started";
}

TEST(PageSelectionModel, RangeSelectionExtendsBackward) {
    PageSelectionModel model = freshModel();
    model.replaceWith(6);

    model.extendTo(3);

    EXPECT_EQ(selection(model), (std::vector<size_t>{3, 4, 5, 6}));
    EXPECT_EQ(model.getCurrentPage(), 3u);
    EXPECT_EQ(model.getAnchor(), 6u);
}

TEST(PageSelectionModel, RangeSelectionIsIdempotent) {
    PageSelectionModel model = freshModel();
    model.replaceWith(2);
    model.extendTo(5);

    model.extendTo(5);
    EXPECT_EQ(selection(model), (std::vector<size_t>{2, 3, 4, 5}));

    model.extendTo(4);
    EXPECT_EQ(selection(model), (std::vector<size_t>{2, 3, 4}));
}

TEST(PageSelectionModel, RangeSelectionWithoutAnchorSelectsOnePage) {
    PageSelectionModel model;
    model.clear();

    model.extendTo(7);

    EXPECT_EQ(selection(model), (std::vector<size_t>{7}));
    EXPECT_EQ(model.getAnchor(), 7u);
}

TEST(PageSelectionModel, SelectAllAndClear) {
    PageSelectionModel model = freshModel(5);

    model.selectAll(5);
    EXPECT_EQ(selection(model), (std::vector<size_t>{0, 1, 2, 3, 4}));
    EXPECT_EQ(model.count(), 5u);

    model.clear();
    EXPECT_TRUE(model.empty());
    EXPECT_EQ(model.getAnchor(), PageSelectionModel::npos);
    EXPECT_EQ(model.getCurrentPage(), 0u) << "clearing the selection leaves the current page alone";
}

TEST(PageSelectionModel, NavigationReplacesTheSelection) {
    PageSelectionModel model = freshModel();
    model.replaceWith(2);
    model.extendTo(4);

    model.setCurrentPage(8);

    EXPECT_EQ(selection(model), (std::vector<size_t>{8}));
    EXPECT_EQ(model.getCurrentPage(), 8u);
}

TEST(PageSelectionModel, NavigationKeepsAMultiSelectionContainingThePage) {
    PageSelectionModel model = freshModel();
    model.replaceWith(2);
    model.toggle(4);
    model.toggle(7);

    model.setCurrentPage(4);

    EXPECT_EQ(selection(model), (std::vector<size_t>{2, 4, 7}));
    EXPECT_EQ(model.getCurrentPage(), 4u);
}

TEST(PageSelectionModel, CurrentPageIsNotPartOfTheSelection) {
    PageSelectionModel model = freshModel();
    model.replaceWith(3);

    model.setCurrentPageWithoutSelecting(9);

    EXPECT_EQ(model.getCurrentPage(), 9u);
    EXPECT_FALSE(model.isSelected(9));
    EXPECT_EQ(selection(model), (std::vector<size_t>{3}));
}

TEST(PageSelectionModel, InsertionInsideTheSelectionShiftsIt) {
    PageSelectionModel model = freshModel(6);
    model.replaceWith(2);
    model.toggle(4);
    model.setCurrentPageWithoutSelecting(4);

    model.pagesInserted(3, 2);

    EXPECT_EQ(selection(model), (std::vector<size_t>{2, 6}));
    EXPECT_EQ(model.getCurrentPage(), 6u);
}

TEST(PageSelectionModel, InsertionAfterTheSelectionLeavesItAlone) {
    PageSelectionModel model = freshModel(6);
    model.replaceWith(1);
    model.toggle(2);

    model.pagesInserted(5, 3);

    EXPECT_EQ(selection(model), (std::vector<size_t>{1, 2}));
    EXPECT_EQ(model.getCurrentPage(), 1u);
}

TEST(PageSelectionModel, InsertionBeforeTheSelectionShiftsIt) {
    PageSelectionModel model = freshModel(6);
    model.replaceWith(3);
    model.toggle(5);

    model.pagesInserted(0, 1);

    EXPECT_EQ(selection(model), (std::vector<size_t>{4, 6}));
    EXPECT_EQ(model.getCurrentPage(), 4u);
    EXPECT_EQ(model.getAnchor(), 6u) << "the anchor follows the page it pointed at";
}

TEST(PageSelectionModel, DeletionInsideTheSelectionDropsThePage) {
    PageSelectionModel model = freshModel(6);
    model.replaceWith(1);
    model.toggle(3);
    model.toggle(5);

    model.pageDeleted(3);

    EXPECT_EQ(selection(model), (std::vector<size_t>{1, 4}));
}

TEST(PageSelectionModel, DeletionBeforeTheSelectionShiftsIt) {
    PageSelectionModel model = freshModel(6);
    model.replaceWith(2);
    model.toggle(4);

    model.pageDeleted(0);

    EXPECT_EQ(selection(model), (std::vector<size_t>{1, 3}));
    EXPECT_EQ(model.getCurrentPage(), 1u);
    EXPECT_EQ(model.getAnchor(), 3u) << "the anchor follows the page it pointed at";
}

TEST(PageSelectionModel, DeletionAfterTheSelectionLeavesItAlone) {
    PageSelectionModel model = freshModel(6);
    model.replaceWith(1);
    model.toggle(2);

    model.pageDeleted(5);

    EXPECT_EQ(selection(model), (std::vector<size_t>{1, 2}));
}

TEST(PageSelectionModel, DeletingTheCurrentPageForgetsIt) {
    PageSelectionModel model = freshModel(6);
    model.replaceWith(2);
    model.toggle(4);
    model.setCurrentPageWithoutSelecting(4);

    model.pageDeleted(4);

    EXPECT_EQ(selection(model), (std::vector<size_t>{2}));
    EXPECT_EQ(model.getCurrentPage(), PageSelectionModel::npos);
    EXPECT_EQ(model.getAnchor(), 2u);
}

TEST(PageSelectionModel, ReorderRemapsTheSelectionToTheSamePages) {
    PageSelectionModel model = freshModel(5);
    model.replaceWith(1);
    model.toggle(3);

    // Pages 1 and 3 moved to the front: new order is 1, 3, 0, 2, 4.
    model.applyPermutation({1, 3, 0, 2, 4});

    EXPECT_EQ(selection(model), (std::vector<size_t>{0, 1}));
    EXPECT_EQ(model.getCurrentPage(), 0u);
}

TEST(PageSelectionModel, ReorderKeepsSelectionAscendingForADownwardMove) {
    PageSelectionModel model = freshModel(6);
    model.replaceWith(0);
    model.toggle(2);

    // Pages 0 and 2 move to the end: new order is 1, 3, 4, 5, 0, 2.
    model.applyPermutation({1, 3, 4, 5, 0, 2});

    EXPECT_EQ(selection(model), (std::vector<size_t>{4, 5}));
    EXPECT_EQ(model.getCurrentPage(), 4u);
}

TEST(PageSelectionModel, ClampDropsPagesThatNoLongerExist) {
    PageSelectionModel model = freshModel(8);
    model.replaceWith(2);
    model.toggle(6);

    model.clampTo(4);

    EXPECT_EQ(selection(model), (std::vector<size_t>{2}));
    EXPECT_EQ(model.getAnchor(), 2u);
    EXPECT_EQ(model.getCurrentPage(), 2u);
}

TEST(PageSelectionModel, MoveOrderMovesOnePageUpwards) {
    // Page 3 dropped before page 1.
    EXPECT_EQ(xoj::model::computeMoveOrder(6, {3}, 1), (std::vector<size_t>{0, 3, 1, 2, 4, 5}));
}

TEST(PageSelectionModel, MoveOrderMovesOnePageDownwards) {
    // Page 1 dropped before page 3.
    EXPECT_EQ(xoj::model::computeMoveOrder(6, {1}, 3), (std::vector<size_t>{0, 2, 1, 3, 4, 5}));
}

TEST(PageSelectionModel, MoveOrderIsIdentityWhenNothingChanges) {
    EXPECT_EQ(xoj::model::computeMoveOrder(5, {1, 2}, 1), (std::vector<size_t>{0, 1, 2, 3, 4}));
    EXPECT_EQ(xoj::model::computeMoveOrder(5, {1, 2}, 3), (std::vector<size_t>{0, 1, 2, 3, 4}));
    EXPECT_EQ(xoj::model::computeMoveOrder(5, {}, 2), (std::vector<size_t>{0, 1, 2, 3, 4}));
    EXPECT_EQ(xoj::model::computeMoveOrder(5, {2}, 2), (std::vector<size_t>{0, 1, 2, 3, 4}));
}

TEST(PageSelectionModel, MoveOrderPreservesRelativeOrderOfANoncontiguousSelection) {
    // Pages 1 and 4 moved to the front together: 1 stays before 4.
    EXPECT_EQ(xoj::model::computeMoveOrder(6, {1, 4}, 0), (std::vector<size_t>{1, 4, 0, 2, 3, 5}));

    // ... and to the end.
    EXPECT_EQ(xoj::model::computeMoveOrder(6, {1, 4}, 6), (std::vector<size_t>{0, 2, 3, 5, 1, 4}));

    // ... and into the middle.
    EXPECT_EQ(xoj::model::computeMoveOrder(6, {1, 4}, 3), (std::vector<size_t>{0, 2, 1, 4, 3, 5}));
}

TEST(PageSelectionModel, MoveOrderClampsADestinationPastTheEnd) {
    EXPECT_EQ(xoj::model::computeMoveOrder(4, {0}, 99), (std::vector<size_t>{1, 2, 3, 0}));
}

TEST(PageSelectionModel, MoveOrderIgnoresOutOfRangePages) {
    EXPECT_EQ(xoj::model::computeMoveOrder(4, {1, 9}, 0), (std::vector<size_t>{1, 0, 2, 3}));
}

TEST(PageSelectionModel, MoveOrderOfEveryPageIsIdentity) {
    EXPECT_EQ(xoj::model::computeMoveOrder(3, {0, 1, 2}, 0), (std::vector<size_t>{0, 1, 2}));
}

TEST(PageSelectionModel, MoveOrderRoundTripsThroughTheSelectionModel) {
    PageSelectionModel model = freshModel(6);
    model.replaceWith(1);
    model.toggle(4);

    auto order = xoj::model::computeMoveOrder(6, {1, 4}, 0);
    model.applyPermutation(order);

    EXPECT_EQ(selection(model), (std::vector<size_t>{0, 1}));

    // Undo puts the pages back and the selection follows them home.
    std::vector<size_t> inverse(order.size());
    for (size_t newIndex = 0; newIndex < order.size(); ++newIndex) { inverse[order[newIndex]] = newIndex; }
    model.applyPermutation(inverse);

    EXPECT_EQ(selection(model), (std::vector<size_t>{1, 4}));
}
