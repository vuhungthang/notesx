/*
 * Xournal++
 *
 * The name of a toolbar item's category (Plan 007)
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include "AbstractToolItem.h"  // for AbstractToolItem::Category

namespace xoj::gui {

/**
 * Plan 007: the translated name of a toolbar item's category.
 *
 * It used to live inside the toolbar customization dialog, with a comment telling the next author
 * to keep it in step with a new category. The command palette groups the toolbar items by the same
 * categories, so the table is here and both read it.
 */
auto toolItemCategoryLabel(AbstractToolItem::Category category) -> const char*;

}  // namespace xoj::gui
