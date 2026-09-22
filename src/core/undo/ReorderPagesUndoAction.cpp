#include "ReorderPagesUndoAction.h"

#include <utility>  // for move

#include "control/Control.h"  // for Control
#include "util/i18n.h"        // for _

ReorderPagesUndoAction::ReorderPagesUndoAction(std::vector<PageRef> before, std::vector<PageRef> after):
        UndoAction("ReorderPagesUndoAction"), before(std::move(before)), after(std::move(after)) {}

ReorderPagesUndoAction::~ReorderPagesUndoAction() = default;

auto ReorderPagesUndoAction::undo(Control* control) -> bool {
    if (control == nullptr) {
        return false;
    }
    control->applyPageOrder(this->before);
    return true;
}

auto ReorderPagesUndoAction::redo(Control* control) -> bool {
    if (control == nullptr) {
        return false;
    }
    control->applyPageOrder(this->after);
    return true;
}

auto ReorderPagesUndoAction::getPages() -> std::vector<PageRef> { return this->after; }

auto ReorderPagesUndoAction::getText() -> std::string { return _("Pages reordered"); }
