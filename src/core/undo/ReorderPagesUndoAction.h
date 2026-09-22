/*
 * Xournal++
 *
 * Undo action for a reordering of the document's pages
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <string>  // for string
#include <vector>  // for vector

#include "model/PageRef.h"  // for PageRef

#include "UndoAction.h"  // for UndoAction

class Control;

/**
 * A reorder of the pages of the document, as one undoable operation.
 *
 * A move of several pages could be written as a group of swaps, but a group undoes its members in
 * the order they were added, which does not invert a sequence of overlapping swaps. So this action
 * keeps the whole order on both sides of the move and asks `Control::applyPageOrder()` to put the
 * document back into either of them.
 *
 * Every page keeps its contents, its background and its own undo history: only the indices move.
 */
class ReorderPagesUndoAction: public UndoAction {
public:
    /**
     * @param before The page order before the move
     * @param after  The page order after it
     */
    ReorderPagesUndoAction(std::vector<PageRef> before, std::vector<PageRef> after);
    ~ReorderPagesUndoAction() override;

public:
    bool undo(Control* control) override;
    bool redo(Control* control) override;

    std::vector<PageRef> getPages() override;
    std::string getText() override;

private:
    std::vector<PageRef> before;
    std::vector<PageRef> after;
};
