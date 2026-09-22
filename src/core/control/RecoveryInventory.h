/*
 * Xournal++
 *
 * A read-only view of the recovery copies Xournal++ has written: what they are, which
 * document they came from, whether that document is still there and whether the copy is
 * newer than it.
 *
 * Plan 004, step 4. The inventory exists so that recovery can be offered from a list
 * instead of from a startup prompt. It reads metadata and the first bytes of a candidate
 * and nothing else: listing candidates must never parse a document, and an invalid
 * candidate must be reported rather than thrown over.
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <array>    // for array
#include <chrono>   // for system_clock
#include <cstdint>  // for uintmax_t
#include <optional>
#include <string>  // for string
#include <vector>  // for vector

#include "filesystem.h"  // for path

namespace xoj::safety {

/**
 * How far a candidate could be checked without reading the whole file.
 *
 * Only `Ok` means the file is a plausible Xournal++ document. The rest are reasons a
 * recovery card must not offer to open it.
 */
enum class RecoveryValidation {
    /// A readable regular file that starts like a Xournal++ document.
    Ok,
    /// The file is not there any more.
    Missing,
    /// A directory, a device or a dangling link.
    NotARegularFile,
    /// There, but it cannot be read.
    Unreadable,
    /// Readable, but it does not start like a Xournal++ document.
    UnrecognizedFormat,
};

/// One recovery copy, described without opening it.
struct RecoveryCandidate {
    /// The autosave file itself.
    fs::path recoveryPath;
    /// The document the copy was derived from, empty for an unnamed autosave.
    fs::path originalPath;
    bool originalExists = false;
    /// The copy was written after the document was last modified.
    bool newerThanOriginal = false;

    std::optional<std::chrono::system_clock::time_point> recoveryTime;
    std::optional<std::chrono::system_clock::time_point> originalTime;

    /// The size of the copy in bytes, 0 when it could not be read.
    std::uintmax_t size = 0;

    RecoveryValidation validation = RecoveryValidation::Missing;
    /// The reason for a validation other than `Ok`, for a log or a details view.
    std::string error;
};

/**
 * Locates and describes autosave files using the existing naming conventions.
 *
 * Nothing here throws: a folder that cannot be read yields no candidates, and a file that
 * cannot be inspected yields a candidate whose `validation` says why.
 */
class RecoveryInventory {
public:
    /// Named autosaves are hidden files next to the document they came from.
    static constexpr const char* AUTOSAVE_SUFFIX = ".autosave.xopp";
    /// The temporary files a save or an autosave leaves behind while it swaps files.
    static constexpr const char* SWAP_SUFFIX = ".swap";
    static constexpr const char* TEMP_SUFFIX = "~";

    /**
     * The document extensions an autosave's name may have been derived from. A recovery copy
     * is written as `.<name>.autosave.xopp`, and `Util::clearExtensions` only strips the two
     * Xournal++ extensions, so `notes.xopp` and `notes.xoj` both become `.notes.autosave.xopp`
     * and the original has to be looked for under either name.
     */
    static constexpr std::array<const char*, 2> DOCUMENT_EXTENSIONS = {"xopp", "xoj"};

    /// The folder unnamed autosaves are written to.
    static auto getAutosaveFolder() -> fs::path;

    /// The document an autosave file was derived from, without its extension; empty for an
    /// unnamed autosave.
    static auto getOriginalFor(const fs::path& recoveryFile) -> fs::path;

    /// The recovery file a document's autosave is written to, by the same convention.
    static auto getRecoveryFor(const fs::path& document) -> fs::path;

    /// Whether `path` is named like an autosave file (and not like one of its temporaries).
    static auto looksLikeAutosave(const fs::path& path) -> bool;

    /**
     * Describe one autosave file. Returns `std::nullopt` only when the path cannot be examined
     * at all, which a caller should treat the same way as a `Missing` candidate.
     */
    static auto inspect(const fs::path& recoveryFile) -> std::optional<RecoveryCandidate>;

    /// Describe every autosave file in one folder, newest first.
    static auto scanFolder(const fs::path& folder) -> std::vector<RecoveryCandidate>;

    /// Describe every autosave file in `folders`, newest first, without duplicates.
    static auto scan(const std::vector<fs::path>& folders) -> std::vector<RecoveryCandidate>;

    /// The folders that hold recovery copies by convention: the autosave cache folder.
    static auto defaultSearchFolders() -> std::vector<fs::path>;
};

}  // namespace xoj::safety
