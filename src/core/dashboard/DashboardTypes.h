/*
 * Xournal++
 *
 * The dashboard's data models: one card per file the dashboard shows, one record per folder it
 * lists, one card per recovery copy it can offer.
 *
 * Plan 006, step 1. These are plain values with no GTK and no document: a card is what can be said
 * about a path by looking at it, so the dashboard stays an index over files the user owns rather
 * than a database of its own. Nothing here opens, parses, moves or rewrites a file.
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <array>     // for array
#include <chrono>    // for system_clock
#include <cstddef>   // for size_t
#include <cstdint>   // for uintmax_t
#include <optional>  // for optional
#include <string>    // for string
#include <vector>    // for vector

#include "control/RecoveryInventory.h"  // for RecoveryCandidate, RecoveryValidation

#include "filesystem.h"  // for path

namespace xoj::dashboard {

/**
 * The dashboard's sections, in the order the dashboard shows them.
 *
 * Recovery first because it is the only section that describes work that is at risk, then the
 * documents the user was working on, then the ones they chose to keep in reach, then the folders
 * they index, then the ways to start something new.
 */
enum class DashboardSection {
    /// Recovery copies. Only shown when the inventory found any.
    Recovery,
    /// Recent documents: what the user was working on.
    ContinueWorking,
    /// Documents the user pinned.
    Pinned,
    /// Documents in the folders the user watches.
    Library,
    /// Quick actions. Never empty.
    Templates,
};

/// How many sections there are. Recovery is the only one that may be absent.
constexpr std::size_t DASHBOARD_SECTION_COUNT = 5;

/// The canonical order of the sections. Recovery included, whether or not it is shown.
auto allSections() -> const std::array<DashboardSection, DASHBOARD_SECTION_COUNT>&;

/// A stable identifier for a section, used for widget names and in tests. Never translated.
auto sectionKey(DashboardSection section) -> const char*;

/// The section's title, translated.
auto sectionTitle(DashboardSection section) -> std::string;

/// What an empty section says, so an empty dashboard explains the next action. Translated.
auto sectionHint(DashboardSection section) -> std::string;

/**
 * One file, as the dashboard can describe it without opening it.
 */
struct DocumentCard {
    /// What the editor knows how to open.
    enum class Type { Xournal, Pdf, Unsupported };

    /// Whether the file is where the card says it is.
    enum class Location { Present, Missing, NotARegularFile, Unreadable };

    /**
     * What the dashboard knows about the file's first page.
     *
     * `Unknown` is the state a card starts in: the thumbnail is extracted in the background, and a
     * card that never got one shows a placeholder. `None` is an answer - the file has no preview -
     * and is kept apart from `Unknown` so a card is not asked for a preview twice.
     */
    enum class Preview { Unknown, Available, None, Corrupt };

    /// The canonical path: one file has one card, however the user reached it.
    fs::path path;
    /// The file's name, as the card shows it.
    std::string displayName;

    Type type = Type::Unsupported;
    Location location = Location::Missing;
    std::optional<std::chrono::system_clock::time_point> modifiedTime;
    Preview preview = Preview::Unknown;
    bool pinned = false;

    /// The sections the card belongs to, in the canonical order of the sections.
    std::vector<DashboardSection> sections;

    auto has(DashboardSection section) const -> bool;

    /// Whether the established opening path can be given this file.
    auto openable() const -> bool;

    /// Whether the file is a Xournal++ document rather than a PDF.
    auto isXournalDocument() const -> bool;

    /// Describe `path` by reading metadata only. The path is canonicalised.
    static auto inspect(const fs::path& path) -> DocumentCard;
};

/**
 * One folder the dashboard indexes.
 */
struct LibraryFolder {
    enum class State {
        /// Not looked at yet.
        Unknown,
        /// A readable directory.
        Ok,
        /// Not there any more. A folder that moved is reported, never followed.
        Missing,
        /// There, but not a directory.
        NotADirectory,
        /// There, but it cannot be listed.
        Inaccessible,
    };

    /// The canonical path. Adding or removing a folder changes this list and nothing else.
    fs::path path;
    std::string displayName;
    bool enabled = true;
    /// Shallow by default; a user can ask for the folder's subtree.
    bool recursive = false;
    State state = State::Unknown;

    /// Whether the folder can be listed.
    auto listable() const -> bool;

    /// How a folder is right now: one metadata look, no listing.
    static auto inspect(const fs::path& path) -> State;
};

/**
 * One recovery copy the inventory offered, as the dashboard shows it.
 */
struct RecoveryCard {
    /// The autosave file itself.
    fs::path recoveryPath;
    /// The document the copy was derived from, empty for an unnamed autosave.
    fs::path originalPath;
    bool originalExists = false;
    /// The copy is newer than the document, i.e. it holds work the document does not.
    bool newerThanOriginal = false;
    std::optional<std::chrono::system_clock::time_point> recoveryTime;
    std::uintmax_t size = 0;
    xoj::safety::RecoveryValidation validation = xoj::safety::RecoveryValidation::Missing;
    std::string error;

    /// The name the card shows: the autosave's own name, without the folder.
    std::string displayName;

    /// Whether the card may offer to open the copy. Only a plausible document may be opened.
    auto openable() const -> bool;

    static auto fromCandidate(const xoj::safety::RecoveryCandidate& candidate) -> RecoveryCard;
};

/// The canonical form of a path: absolute, symlinks resolved where that is possible.
auto canonicalPath(const fs::path& path) -> fs::path;

/**
 * Whether a name is a document the dashboard lists, as opposed to an autosave, a swap file or a
 * temporary a save left behind. Used to keep internal artefacts out of the document sections.
 */
auto looksLikeDocumentName(const fs::path& path) -> bool;

/// Whether the editor can open a path by its extension.
auto isSupportedDocument(const fs::path& path) -> bool;

}  // namespace xoj::dashboard
