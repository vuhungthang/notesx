/*
 * Xournal++
 *
 * The words a page card in the page navigator shows and announces
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <cstddef>  // for size_t
#include <string>   // for string

namespace xoj::pagecard {

/**
 * What a screen reader announces for one page of the page navigator.
 *
 * The state of the card is in the name and not only in its colours, so a user who cannot see the
 * card still knows which page the editor is on, which pages are selected, and whether the
 * thumbnail in front of them is finished.
 *
 * @param pageNumber The page as the user counts it, 1-based
 * @param selected The page is part of the navigator's selection
 * @param current The page the editor shows
 * @param loading The thumbnail is still being rendered
 * @param error The thumbnail could not be rendered
 * @return For instance "Page 12, selected, current page"
 */
auto buildAccessibleName(size_t pageNumber, bool selected, bool current, bool loading, bool error) -> std::string;

/**
 * The metadata line of a page card, shown next to the thumbnail in list mode.
 *
 * @param pageNumber The page as the user counts it, 1-based
 * @param pageName The name the page carries, if it has one; empty when it does not
 * @param error The thumbnail could not be rendered
 * @return For instance "Page 12 · Chapter 3" or "Page 12 · preview unavailable"
 */
auto buildMetadata(size_t pageNumber, const std::string& pageName, bool error) -> std::string;

}  // namespace xoj::pagecard
