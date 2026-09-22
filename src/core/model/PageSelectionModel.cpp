#include "PageSelectionModel.h"

#include <algorithm>  // for sort, unique, remove_if
#include <numeric>    // for iota

namespace xoj::model {

auto PageSelectionModel::isSelected(size_t page) const -> bool {
    return page != npos && std::binary_search(this->selection.begin(), this->selection.end(), page);
}

void PageSelectionModel::insertIntoSelection(size_t page) {
    if (page == npos || isSelected(page)) {
        return;
    }
    auto it = std::lower_bound(this->selection.begin(), this->selection.end(), page);
    this->selection.insert(it, page);
}

void PageSelectionModel::removeFromSelection(size_t page) {
    auto it = std::lower_bound(this->selection.begin(), this->selection.end(), page);
    if (it != this->selection.end() && *it == page) {
        this->selection.erase(it);
    }
}

void PageSelectionModel::replaceWith(size_t page) {
    if (page == npos) {
        this->clear();
        return;
    }
    this->selection.assign(1, page);
    this->anchor = page;
    this->current = page;
}

void PageSelectionModel::setSelection(std::vector<size_t> pages) {
    std::sort(pages.begin(), pages.end());
    pages.erase(std::unique(pages.begin(), pages.end()), pages.end());

    this->selection = std::move(pages);
    this->anchor = this->selection.empty() ? npos : this->selection.back();
}

void PageSelectionModel::toggle(size_t page) {
    if (page == npos) {
        return;
    }
    if (isSelected(page)) {
        removeFromSelection(page);
    } else {
        insertIntoSelection(page);
    }
    // The page just clicked is where the next Shift-click extends from, whether it was added or
    // removed.
    this->anchor = this->selection.empty() ? npos : page;
}

void PageSelectionModel::extendTo(size_t page) {
    if (page == npos) {
        return;
    }
    if (this->anchor == npos) {
        replaceWith(page);
        return;
    }

    size_t first = std::min(this->anchor, page);
    size_t last = std::max(this->anchor, page);

    this->selection.clear();
    this->selection.reserve(last - first + 1);
    for (size_t p = first; p <= last; ++p) { this->selection.push_back(p); }

    this->current = page;
}

void PageSelectionModel::selectAll(size_t pageCount) {
    this->selection.clear();
    this->selection.reserve(pageCount);
    for (size_t p = 0; p < pageCount; ++p) { this->selection.push_back(p); }

    this->anchor = pageCount == 0 ? npos : 0;
}

void PageSelectionModel::clear() {
    this->selection.clear();
    this->anchor = npos;
}

void PageSelectionModel::setCurrentPage(size_t page) {
    if (page == npos) {
        this->current = npos;
        this->clear();
        return;
    }
    if (!isSelected(page)) {
        replaceWith(page);
        return;
    }
    this->current = page;
}

void PageSelectionModel::setCurrentPageWithoutSelecting(size_t page) { this->current = page; }

void PageSelectionModel::reset(size_t pageCount) {
    this->selection.clear();
    this->anchor = npos;
    this->current = npos;

    if (pageCount != 0) {
        replaceWith(0);
    }
}

void PageSelectionModel::pagesInserted(size_t at, size_t count) {
    if (count == 0 || at == npos) {
        return;
    }
    for (size_t& s: this->selection) {
        if (s >= at) {
            s += count;
        }
    }
    if (this->current != npos && this->current >= at) {
        this->current += count;
    }
    if (this->anchor != npos && this->anchor >= at) {
        this->anchor += count;
    }
}

void PageSelectionModel::pageDeleted(size_t at) {
    if (at == npos) {
        return;
    }
    removeFromSelection(at);
    for (size_t& s: this->selection) {
        if (s > at) {
            --s;
        }
    }

    if (this->current == at) {
        this->current = npos;
    } else if (this->current != npos && this->current > at) {
        --this->current;
    }

    if (this->anchor == at) {
        this->anchor = this->selection.empty() ? npos : this->selection.back();
    } else if (this->anchor != npos && this->anchor > at) {
        --this->anchor;
    }
}

void PageSelectionModel::applyPermutation(const std::vector<size_t>& newToOld) {
    std::vector<size_t> oldToNew(newToOld.size(), npos);
    for (size_t newIndex = 0; newIndex < newToOld.size(); ++newIndex) {
        size_t oldIndex = newToOld[newIndex];
        if (oldIndex < oldToNew.size()) {
            oldToNew[oldIndex] = newIndex;
        }
    }

    auto remap = [&oldToNew](size_t index) -> size_t {
        return index != npos && index < oldToNew.size() ? oldToNew[index] : npos;
    };

    std::vector<size_t> remapped;
    remapped.reserve(this->selection.size());
    for (size_t s: this->selection) {
        size_t mapped = remap(s);
        if (mapped != npos) {
            remapped.push_back(mapped);
        }
    }
    std::sort(remapped.begin(), remapped.end());
    this->selection = std::move(remapped);

    this->anchor = remap(this->anchor);
    this->current = remap(this->current);
}

void PageSelectionModel::clampTo(size_t pageCount) {
    this->selection.erase(std::remove_if(this->selection.begin(), this->selection.end(),
                                         [pageCount](size_t p) { return p >= pageCount; }),
                          this->selection.end());
    std::sort(this->selection.begin(), this->selection.end());
    this->selection.erase(std::unique(this->selection.begin(), this->selection.end()), this->selection.end());

    if (this->current >= pageCount) {
        this->current = npos;
    }
    // The anchor only has to point at a page that exists; it is not necessarily selected, which is
    // what lets a Shift-click extend from a page a Ctrl-click has just removed.
    if (this->anchor >= pageCount) {
        this->anchor = this->selection.empty() ? npos : this->selection.back();
    }
}

auto computeMoveOrder(size_t pageCount, const std::vector<size_t>& moved, size_t destination) -> std::vector<size_t> {
    std::vector<size_t> order(pageCount);
    std::iota(order.begin(), order.end(), 0);

    std::vector<size_t> block;
    block.reserve(moved.size());
    for (size_t m: moved) {
        if (m < pageCount) {
            block.push_back(m);
        }
    }
    std::sort(block.begin(), block.end());
    block.erase(std::unique(block.begin(), block.end()), block.end());

    if (block.empty() || block.size() == pageCount) {
        return order;
    }

    std::vector<bool> isMoved(pageCount, false);
    for (size_t m: block) { isMoved[m] = true; }

    std::vector<size_t> others;
    others.reserve(pageCount - block.size());
    for (size_t p = 0; p < pageCount; ++p) {
        if (!isMoved[p]) {
            others.push_back(p);
        }
    }

    // The block goes before the first page that stays behind and currently sits at or after the
    // drop position. `destination == pageCount` therefore appends it.
    size_t insertPos = 0;
    while (insertPos < others.size() && others[insertPos] < destination) {
        ++insertPos;
    }

    std::vector<size_t> result;
    result.reserve(pageCount);
    result.insert(result.end(), others.begin(), others.begin() + static_cast<std::ptrdiff_t>(insertPos));
    result.insert(result.end(), block.begin(), block.end());
    result.insert(result.end(), others.begin() + static_cast<std::ptrdiff_t>(insertPos), others.end());
    return result;
}

auto formatPageRange(const std::vector<size_t>& pages) -> std::string {
    if (pages.empty()) {
        return {};
    }

    std::vector<size_t> sorted = pages;
    std::sort(sorted.begin(), sorted.end());
    sorted.erase(std::unique(sorted.begin(), sorted.end()), sorted.end());

    std::string range;
    size_t first = sorted.front();
    size_t last = sorted.front();

    for (size_t i = 1; i <= sorted.size(); ++i) {
        // npos closes the run that is open, without being a page of its own.
        const size_t page = i < sorted.size() ? sorted[i] : PageSelectionModel::npos;
        if (page == last + 1) {
            last = page;
            continue;
        }

        if (!range.empty()) {
            range += ",";
        }
        // The pages are 1-based in the range syntax.
        if (first == last) {
            range += std::to_string(first + 1);
        } else {
            range += std::to_string(first + 1) + "-" + std::to_string(last + 1);
        }

        if (page != PageSelectionModel::npos) {
            first = last = page;
        }
    }

    return range;
}

auto duplicatedPageIndices(const std::vector<size_t>& pages) -> std::vector<size_t> {
    // Duplicating from the last page backwards: an insertion below a page never moves a page that
    // comes before it, so every copy lands directly below the page it copies.
    std::vector<size_t> copies;
    copies.reserve(pages.size());
    for (size_t page: pages) {
        copies.push_back(page + 1);
    }
    return copies;
}

}  // namespace xoj::model
