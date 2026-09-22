/*
 * Xournal++
 *
 * The set of pages selected in the page navigator.
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <cstddef>  // for size_t
#include <string>   // for string
#include <vector>   // for vector

namespace xoj::model {

/**
 * Which pages the page navigator has selected, and which one is the current page.
 *
 * This is deliberately independent of GTK: the selection rules (Ctrl toggles one page, Shift
 * extends a range from an anchor, a document navigation replaces the selection) and the way the
 * selection survives the document being edited (pages inserted, deleted, reordered) are the part
 * of the navigator that is worth testing on its own.
 *
 * The model works with *positions*: it knows nothing about pages other than their index, so its
 * owner is responsible for feeding it every structural change of the document. Page identity is
 * kept by whoever reorders: `applyPermutation()` remaps the selection by position, which is what
 * keeps the same pages selected across a reorder.
 *
 * Invariants:
 *  - `selection` is sorted ascending and holds no duplicate.
 *  - `anchor` is the page a Shift-click extends from; it is `npos` when there is no selection.
 *  - `current` is the page the editor shows. It is not necessarily selected, and a selection is
 *    not necessarily contiguous.
 */
class PageSelectionModel {
public:
    /// No page. Matches the convention used throughout the code base.
    static constexpr size_t npos = static_cast<size_t>(-1);

public:
    /// The selected pages, ascending.
    auto getSelection() const -> const std::vector<size_t>& { return this->selection; }

    auto count() const -> size_t { return this->selection.size(); }
    auto empty() const -> bool { return this->selection.empty(); }

    auto isSelected(size_t page) const -> bool;

    /// The page the editor shows, or `npos`.
    auto getCurrentPage() const -> size_t { return this->current; }

    auto isCurrentPage(size_t page) const -> bool { return page != npos && page == this->current; }

    /// The page a Shift-click extends from, or `npos`.
    auto getAnchor() const -> size_t { return this->anchor; }

    /**
     * The last page of the selection, which is what a "lead" page means for actions applied to a
     * non-contiguous selection.
     */
    auto getLeadPage() const -> size_t { return this->selection.empty() ? npos : this->selection.back(); }

public:
    /// Select exactly `page`, replacing whatever was selected.
    void replaceWith(size_t page);

    /**
     * Select exactly `pages`, replacing whatever was selected.
     *
     * Used after an operation that creates pages and wants the new ones selected, such as
     * duplicating a selection: the caller knows the indices it just created, and this keeps the
     * model's invariants (ascending, no duplicates) rather than trusting them.
     */
    void setSelection(std::vector<size_t> pages);

    /// Ctrl-click: add `page` to the selection, or remove it if it was selected.
    void toggle(size_t page);

    /// Shift-click: select everything between the anchor and `page`, inclusive.
    void extendTo(size_t page);

    /// Select every page of a document of `pageCount` pages.
    void selectAll(size_t pageCount);

    /// Drop the whole selection. The current page is left alone.
    void clear();

    /**
     * The editor moved to `page` (scrolling, a jump, an inserted page, ...).
     *
     * A page that is already part of the selection keeps it: that is how a Ctrl/Shift selection
     * survives the editor following the page it just made current. Any other page becomes the
     * only selected page, which is the plain navigation behavior.
     */
    void setCurrentPage(size_t page);

    /**
     * Move the current page without touching the selection.
     *
     * Used while the navigator itself drives a multi-page operation: it must be able to follow the
     * pages it moved without collapsing the selection it is about to re-apply.
     */
    void setCurrentPageWithoutSelecting(size_t page);

public:
    /// Forget everything and start over on a document of `pageCount` pages.
    void reset(size_t pageCount);

    /// `count` pages were inserted at `at`.
    void pagesInserted(size_t at, size_t count);

    /// The page at `at` was deleted.
    void pageDeleted(size_t at);

    /**
     * The document was reordered.
     *
     * @param newToOld newToOld[newIndex] is the old index of the page now at `newIndex`. It must be
     *                 a permutation of the pages the model knows about.
     */
    void applyPermutation(const std::vector<size_t>& newToOld);

    /// Reorder the selection so that it stays ascending, dropping anything out of range.
    void clampTo(size_t pageCount);

private:
    void insertIntoSelection(size_t page);
    void removeFromSelection(size_t page);

private:
    std::vector<size_t> selection;
    size_t anchor = npos;
    size_t current = npos;
};

/**
 * The page order a document ends up in when the pages at `moved` are moved together so that the
 * first of them lands where `destination` currently is.
 *
 * `moved` are positions in the current document; they are treated as a block whose relative order
 * is preserved. `destination` is an insertion point in the current document: the block is placed
 * before the first page that stays behind and currently sits at or after `destination`, so
 * `destination == pageCount` appends the block.
 *
 * @return newToOld, where newToOld[i] is the current index of the page that ends up at i. Returns
 *         the identity order when the move changes nothing.
 */
auto computeMoveOrder(size_t pageCount, const std::vector<size_t>& moved, size_t destination) -> std::vector<size_t>;

/**
 * The page range string that names exactly `pages`.
 *
 * The syntax is the one the export dialog and `ElementRange::parse()` use, and the pages are
 * 1-based in it, so a selection of the pages at positions 0, 2, 3 and 6 reads "1,3-4,7".
 *
 * @param pages Positions, ascending and without duplicates. Anything else is normalized first.
 * @return The range, or an empty string when nothing is selected.
 */
auto formatPageRange(const std::vector<size_t>& pages) -> std::string;

/**
 * Where the copies land when every page of `pages` is duplicated directly below itself.
 *
 * The copies are inserted from the last page backwards, so the indices of the pages that are
 * still to be duplicated do not move while the copies are being made.
 *
 * @param pages Positions, ascending and without duplicates
 * @return The positions of the copies, ascending
 */
auto duplicatedPageIndices(const std::vector<size_t>& pages) -> std::vector<size_t>;

}  // namespace xoj::model
