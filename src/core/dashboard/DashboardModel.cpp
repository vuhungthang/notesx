#include "DashboardModel.h"

#include <algorithm>  // for stable_sort, find, remove_if
#include <cstddef>    // for size_t
#include <system_error>  // for error_code
#include <utility>       // for move

#include <glib.h>  // for g_warning

using namespace xoj::dashboard;

namespace {

/// The key one path is filed under. One file, one card, however it was reached.
auto keyOf(const fs::path& canonical) -> std::string { return canonical.generic_string(); }

/// A folder that is not listed for a reason worth naming.
auto isWorthListing(const LibraryFolder& folder) -> bool { return folder.listable(); }

/// Whether the name of a directory entry is one the dashboard does not walk into.
auto isHiddenDirectory(const fs::path& path) -> bool {
    const std::string name = path.filename().string();
    return !name.empty() && name.front() == '.';
}

}  // namespace

DashboardModel::DashboardModel() { refresh(); }

DashboardModel::~DashboardModel() = default;

auto DashboardModel::canonicalize(const fs::path& path) -> fs::path { return canonicalPath(path); }

void DashboardModel::setRecentFiles(const std::vector<fs::path>& paths) { this->recentFiles = paths; }

auto DashboardModel::getRecentFiles() const -> const std::vector<fs::path>& { return this->recentFiles; }

void DashboardModel::setPinnedFiles(const std::vector<fs::path>& paths) { this->pinnedFiles = paths; }

auto DashboardModel::getPinnedFiles() const -> const std::vector<fs::path>& { return this->pinnedFiles; }

auto DashboardModel::isPinned(const fs::path& path) const -> bool {
    const fs::path canonical = canonicalize(path);
    const auto known = [&canonical](const fs::path& pinned) { return canonicalize(pinned) == canonical; };
    return std::any_of(this->pinnedFiles.begin(), this->pinnedFiles.end(), known);
}

void DashboardModel::setPinned(const fs::path& path, bool pinned) {
    const fs::path canonical = canonicalize(path);
    const auto known = [&canonical](const fs::path& other) { return canonicalize(other) == canonical; };
    this->pinnedFiles.erase(std::remove_if(this->pinnedFiles.begin(), this->pinnedFiles.end(), known),
                            this->pinnedFiles.end());

    if (pinned) {
        this->pinnedFiles.emplace_back(canonical);
    }
}

void DashboardModel::setLibraryFolders(std::vector<LibraryFolder> folders) {
    this->libraryFolders = std::move(folders);
    for (LibraryFolder& folder: this->libraryFolders) {
        folder.path = canonicalize(folder.path);
        if (folder.displayName.empty()) {
            folder.displayName = folder.path.filename().string();
        }
    }
}

auto DashboardModel::getLibraryFolders() const -> const std::vector<LibraryFolder>& { return this->libraryFolders; }

auto DashboardModel::addLibraryFolder(const LibraryFolder& folder) -> bool {
    LibraryFolder canonical = folder;
    canonical.path = canonicalize(folder.path);
    if (canonical.path.empty() || canonical.displayName.empty()) {
        canonical.displayName = canonical.path.filename().string();
    }

    const auto known = [&canonical](const LibraryFolder& other) { return other.path == canonical.path; };
    if (std::any_of(this->libraryFolders.begin(), this->libraryFolders.end(), known)) {
        return false;
    }

    this->libraryFolders.emplace_back(std::move(canonical));
    return true;
}

auto DashboardModel::removeLibraryFolder(const fs::path& path) -> bool {
    const fs::path canonical = canonicalize(path);
    const auto known = [&canonical](const LibraryFolder& other) { return other.path == canonical; };
    const auto first = std::remove_if(this->libraryFolders.begin(), this->libraryFolders.end(), known);
    if (first == this->libraryFolders.end()) {
        return false;
    }
    this->libraryFolders.erase(first, this->libraryFolders.end());
    return true;
}

auto DashboardModel::setLibraryFolderRecursive(const fs::path& path, bool recursive) -> bool {
    const fs::path canonical = canonicalize(path);
    for (LibraryFolder& folder: this->libraryFolders) {
        if (folder.path != canonical) {
            continue;
        }
        if (folder.recursive == recursive) {
            return false;
        }
        folder.recursive = recursive;
        return true;
    }
    return false;
}

void DashboardModel::setRecoveryCandidates(const std::vector<xoj::safety::RecoveryCandidate>& candidates) {
    this->recoveryCards.clear();
    this->recoveryCards.reserve(candidates.size());
    for (const xoj::safety::RecoveryCandidate& candidate: candidates) {
        this->recoveryCards.emplace_back(RecoveryCard::fromCandidate(candidate));
    }
}

auto DashboardModel::getRecoveryCards() const -> const std::vector<RecoveryCard>& { return this->recoveryCards; }

auto DashboardModel::forget(const fs::path& path) -> bool {
    const fs::path canonical = canonicalize(path);
    const auto known = [&canonical](const fs::path& other) { return canonicalize(other) == canonical; };

    const auto first = std::remove_if(this->recentFiles.begin(), this->recentFiles.end(), known);
    const bool wasRecent = first != this->recentFiles.end();
    this->recentFiles.erase(first, this->recentFiles.end());

    const bool wasPinned = isPinned(canonical);
    if (wasPinned) {
        setPinned(canonical, false);
    }
    return wasRecent || wasPinned;
}

void DashboardModel::setPreview(const fs::path& path, DocumentCard::Preview preview) {
    const fs::path canonical = canonicalize(path);
    this->previews.insert_or_assign(keyOf(canonical), preview);

    const auto known = this->indexByPath.find(keyOf(canonical));
    if (known != this->indexByPath.end()) {
        this->cards[known->second].preview = preview;
    }
}

auto DashboardModel::getPreview(const fs::path& path) const -> DocumentCard::Preview {
    const auto known = this->previews.find(keyOf(canonicalize(path)));
    return known == this->previews.end() ? DocumentCard::Preview::Unknown : known->second;
}

void DashboardModel::refresh() {
    /*
     * A folder's own state is re-read before its contents: a folder that comes back after being
     * away has to be listed again, and one that is gone has to say so instead of being remembered
     * as it was. This is one metadata look per folder, not a scan.
     */
    for (LibraryFolder& folder: this->libraryFolders) {
        folder.state = LibraryFolder::inspect(folder.path);
    }

    buildCards();
    buildSections();
}

auto DashboardModel::getCards() const -> const std::vector<DocumentCard>& { return this->cards; }

auto DashboardModel::sections() const -> const std::vector<DashboardSection>& { return this->shownSections; }

auto DashboardModel::findCard(const fs::path& path) const -> const DocumentCard* {
    const auto known = this->indexByPath.find(keyOf(canonicalize(path)));
    return known == this->indexByPath.end() ? nullptr : &this->cards[known->second];
}

auto DashboardModel::cardIndex(const fs::path& canonical) -> std::size_t {
    const std::string key = keyOf(canonical);
    if (const auto known = this->indexByPath.find(key); known != this->indexByPath.end()) {
        return known->second;
    }

    DocumentCard card = DocumentCard::inspect(canonical);
    if (const auto preview = this->previews.find(key); preview != this->previews.end()) {
        card.preview = preview->second;
    }

    const std::size_t index = this->cards.size();
    this->cards.emplace_back(std::move(card));
    this->indexByPath.emplace(key, index);
    return index;
}

auto DashboardModel::addCard(const fs::path& path, DashboardSection section, bool fromFolder) -> bool {
    const fs::path canonical = canonicalize(path);
    if (canonical.empty() || !looksLikeDocumentName(canonical)) {
        return false;
    }
    /*
     * A folder listing shows what the editor can open. A path the user pinned or opened by name is
     * listed whatever it is: the card has to exist for them to see why it will not open and to get
     * rid of it.
     */
    if (fromFolder && !isSupportedDocument(canonical)) {
        return false;
    }

    const std::size_t index = cardIndex(canonical);
    DocumentCard& card = this->cards[index];
    if (!card.has(section)) {
        card.sections.push_back(section);
        std::sort(card.sections.begin(), card.sections.end(),
                  [](DashboardSection lhs, DashboardSection rhs) {
                      return static_cast<int>(lhs) < static_cast<int>(rhs);
                  });
    }
    if (section == DashboardSection::Pinned) {
        card.pinned = true;
    }
    return true;
}

void DashboardModel::listFolder(const LibraryFolder& folder) {
    if (!isWorthListing(folder)) {
        return;
    }

    std::error_code error;
    std::size_t listed = 0;

    const auto consider = [&](const fs::path& path) {
        if (listed >= MAX_LIBRARY_FILES) {
            return;
        }
        if (addCard(path, DashboardSection::Library, /*fromFolder=*/true)) {
            listed++;
        }
    };

    if (!folder.recursive) {
        fs::directory_iterator it(folder.path, fs::directory_options::skip_permission_denied, error);
        if (error) {
            g_warning("Dashboard: could not list \"%s\": %s", folder.path.string().c_str(), error.message().c_str());
            return;
        }
        const fs::directory_iterator end;
        while (it != end && listed < MAX_LIBRARY_FILES) {
            std::error_code entryError;
            if (it->is_regular_file(entryError) && !entryError) {
                consider(it->path());
            }
            it.increment(error);
            if (error) {
                break;
            }
        }
        return;
    }

    /*
     * An explicitly recursive folder is walked with a hard depth limit and without stepping into
     * hidden directories, so a home folder - or a folder that contains a mounted volume - cannot
     * turn a refresh into an unbounded scan.
     */
    fs::recursive_directory_iterator it(folder.path, fs::directory_options::skip_permission_denied, error);
    if (error) {
        g_warning("Dashboard: could not walk \"%s\": %s", folder.path.string().c_str(), error.message().c_str());
        return;
    }
    const fs::recursive_directory_iterator end;
    while (it != end && listed < MAX_LIBRARY_FILES) {
        std::error_code entryError;
        if (it->is_directory(entryError) && !entryError) {
            if (isHiddenDirectory(it->path()) || it.depth() + 1 >= MAX_RECURSION_DEPTH) {
                it.disable_recursion_pending();
            }
        } else {
            entryError.clear();
            if (it->is_regular_file(entryError) && !entryError) {
                consider(it->path());
            }
        }
        it.increment(error);
        if (error) {
            break;
        }
    }
}

void DashboardModel::buildCards() {
    this->cards.clear();
    this->indexByPath.clear();

    // Pinned first, so an unsupported or missing pin keeps its card even when a folder listing
    // would have ignored it.
    for (const fs::path& path: this->pinnedFiles) {
        addCard(path, DashboardSection::Pinned, /*fromFolder=*/false);
    }
    for (const fs::path& path: this->recentFiles) {
        addCard(path, DashboardSection::ContinueWorking, /*fromFolder=*/false);
    }
    for (const LibraryFolder& folder: this->libraryFolders) {
        listFolder(folder);
    }
}

void DashboardModel::buildSections() {
    this->shownSections.clear();
    this->sectionCards.clear();

    for (DashboardSection section: allSections()) {
        // Recovery is the one section that is only there when it has something to say.
        if (section == DashboardSection::Recovery && this->recoveryCards.empty()) {
            continue;
        }
        this->shownSections.push_back(section);

        std::vector<std::size_t>& indices = this->sectionCards[sectionKey(section)];
        for (std::size_t i = 0; i < this->cards.size(); i++) {
            if (this->cards[i].has(section)) {
                indices.push_back(i);
            }
        }

        switch (section) {
            case DashboardSection::ContinueWorking:
                // Newest first, so "continue working" shows what was worked on last. A file that is
                // gone has no time to sort by and goes to the end.
                std::stable_sort(indices.begin(), indices.end(), [this](std::size_t a, std::size_t b) {
                    const DocumentCard& left = this->cards[a];
                    const DocumentCard& right = this->cards[b];
                    if (left.modifiedTime.has_value() != right.modifiedTime.has_value()) {
                        return left.modifiedTime.has_value();
                    }
                    if (left.modifiedTime.has_value() && *left.modifiedTime != *right.modifiedTime) {
                        return *left.modifiedTime > *right.modifiedTime;
                    }
                    return left.displayName < right.displayName;
                });
                break;
            case DashboardSection::Pinned: {
                // The order the user pinned them in, not the order the files were modified in.
                std::vector<fs::path> order;
                order.reserve(this->pinnedFiles.size());
                for (const fs::path& path: this->pinnedFiles) {
                    order.emplace_back(canonicalize(path));
                }
                const auto rank = [&order](const fs::path& path) -> std::size_t {
                    const auto found = std::find(order.begin(), order.end(), path);
                    return found == order.end() ? order.size() : static_cast<std::size_t>(found - order.begin());
                };
                std::stable_sort(indices.begin(), indices.end(), [this, &rank](std::size_t a, std::size_t b) {
                    return rank(this->cards[a].path) < rank(this->cards[b].path);
                });
                break;
            }
            case DashboardSection::Library:
                std::sort(indices.begin(), indices.end(), [this](std::size_t a, std::size_t b) {
                    const DocumentCard& left = this->cards[a];
                    const DocumentCard& right = this->cards[b];
                    if (left.displayName != right.displayName) {
                        return left.displayName < right.displayName;
                    }
                    return left.path < right.path;
                });
                break;
            case DashboardSection::Recovery:
            case DashboardSection::Templates:
                break;
        }
    }
}

auto DashboardModel::cardsIn(DashboardSection section) const -> std::vector<DocumentCard> {
    std::vector<DocumentCard> result;
    const auto known = this->sectionCards.find(sectionKey(section));
    if (known == this->sectionCards.end()) {
        return result;
    }

    result.reserve(known->second.size());
    for (std::size_t index: known->second) {
        result.emplace_back(this->cards[index]);
    }
    return result;
}
