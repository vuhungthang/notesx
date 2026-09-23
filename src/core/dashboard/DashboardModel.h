/*
 * Xournal++
 *
 * The dashboard's index: which files the dashboard shows, in which section, and what can be said
 * about each of them.
 *
 * Plan 006, step 1. The model holds paths, not documents: it is rebuilt from the sources the user
 * owns (the recent list, the pinned list, the watched folders and the recovery inventory) and the
 * only thing that survives a rebuild besides the paths is what has already been paid for, i.e. the
 * extracted previews. Rebuilding never opens a document.
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <cstddef>  // for size_t
#include <map>      // for map
#include <string>   // for string
#include <vector>   // for vector

#include "DashboardTypes.h"  // for DashboardSection, DocumentCard, LibraryFolder, RecoveryCard
#include "control/RecoveryInventory.h"  // for RecoveryCandidate

#include "filesystem.h"  // for path

namespace xoj::dashboard {

/**
 * The dashboard's cards, derived from paths.
 *
 * One file has one card, however many ways the user reached it: a document that is recent, pinned
 * and inside a watched folder is one card that belongs to all three sections, which is what keeps
 * the dashboard from showing the same note three times.
 */
class DashboardModel {
public:
    /// The number of files one watched folder may contribute, so a huge tree cannot stall it.
    static constexpr std::size_t MAX_LIBRARY_FILES = 400;
    /// How deep an explicitly recursive folder is listed. Deeper subtrees are not walked.
    static constexpr int MAX_RECURSION_DEPTH = 4;

    DashboardModel();
    ~DashboardModel();

    /// The canonical form of a path: what the model keys its cards by. Absolute, symlinks resolved
    /// where the filesystem allows it.
    static auto canonicalize(const fs::path& path) -> fs::path;

    /// Recent documents, most recent first. A path that is gone is kept: it is what the user sees
    /// a "locate" state on.
    void setRecentFiles(const std::vector<fs::path>& paths);
    auto getRecentFiles() const -> const std::vector<fs::path>&;

    /// Pinned documents, in the order the user arranged them.
    void setPinnedFiles(const std::vector<fs::path>& paths);
    auto getPinnedFiles() const -> const std::vector<fs::path>&;
    auto isPinned(const fs::path& path) const -> bool;
    /// Pin or unpin one file. Pinning appends, so the pins the user made keep their order.
    void setPinned(const fs::path& path, bool pinned);

    void setLibraryFolders(std::vector<LibraryFolder> folders);
    auto getLibraryFolders() const -> const std::vector<LibraryFolder>&;
    /// Add a folder, unless it is already watched. Returns whether the list changed.
    auto addLibraryFolder(const LibraryFolder& folder) -> bool;
    /// Stop watching a folder. Returns whether the list changed.
    auto removeLibraryFolder(const fs::path& path) -> bool;
    /// Ask for (or stop asking for) the folder's subtree. Returns whether the list changed.
    auto setLibraryFolderRecursive(const fs::path& path, bool recursive) -> bool;

    /// The recovery copies the inventory found, newest first.
    void setRecoveryCandidates(const std::vector<xoj::safety::RecoveryCandidate>& candidates);
    auto getRecoveryCards() const -> const std::vector<RecoveryCard>&;

    /// Drop a path from the recent and the pinned lists. Returns whether it was listed.
    auto forget(const fs::path& path) -> bool;

    /// Remember (or report) what was extracted for a file.
    void setPreview(const fs::path& path, DocumentCard::Preview preview);
    auto getPreview(const fs::path& path) const -> DocumentCard::Preview;

    /**
     * Rebuild the cards from the sources.
     *
     * The watched folders' own state is re-read first, so a folder that was disconnected and comes
     * back is listed again, and one that went away is reported rather than remembered.
     */
    void refresh();

    /// Every card, deduplicated, in no particular order.
    auto getCards() const -> const std::vector<DocumentCard>&;
    /// The cards of one section, in the order the section shows them.
    auto cardsIn(DashboardSection section) const -> std::vector<DocumentCard>;
    /// The sections to show, in order. Recovery is absent when there is nothing to recover.
    auto sections() const -> const std::vector<DashboardSection>&;

    /// The card for a path, or nullptr when the dashboard does not show it.
    auto findCard(const fs::path& path) const -> const DocumentCard*;

private:
    void buildCards();
    void buildSections();
    /// Describe one path and file it under `section`. Returns whether a card was added or updated.
    auto addCard(const fs::path& path, DashboardSection section, bool fromFolder) -> bool;
    /// Find the card of a canonical path, creating it if this is the first time it is seen.
    auto cardIndex(const fs::path& canonical) -> std::size_t;

    void listFolder(const LibraryFolder& folder);

    std::vector<fs::path> recentFiles;
    std::vector<fs::path> pinnedFiles;
    std::vector<LibraryFolder> libraryFolders;
    std::vector<RecoveryCard> recoveryCards;
    std::map<std::string, DocumentCard::Preview> previews;

    std::vector<DocumentCard> cards;
    std::map<std::string, std::size_t> indexByPath;
    std::vector<DashboardSection> shownSections;
    std::map<std::string, std::vector<std::size_t>> sectionCards;
};

}  // namespace xoj::dashboard
