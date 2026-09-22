#include "RecoveryInventory.h"

#include <algorithm>  // for sort, unique
#include <cstring>    // for strlen
#include <fstream>    // for ifstream
#include <string_view>
#include <system_error>  // for error_code
#include <utility>       // for move

#include <glib.h>  // for g_warning

#include "util/PathUtil.h"  // for clearExtensions, getCacheSubfolder, isChildOrEquivalent

using namespace xoj::safety;

namespace {

/// Whether `name` is one of the files a save or an autosave leaves behind while it swaps files.
auto isTemporaryName(const std::string& name) -> bool {
    return name.ends_with(RecoveryInventory::TEMP_SUFFIX) || name.ends_with(RecoveryInventory::SWAP_SUFFIX);
}

/**
 * Whether the first bytes of a file are those of a Xournal++ document.
 *
 * A `.xopp` is a zip archive and older files are gzipped or plain XML, so the check is the
 * cheap one: it recognises the formats without reading any further. A truncated archive still
 * passes, which is why this is called a plausibility check and not a validation of contents.
 */
auto looksLikeXournalDocument(const char* head, std::streamsize size) -> bool {
    if (size >= 4 && head[0] == 'P' && head[1] == 'K' && head[2] == '\x03' && head[3] == '\x04') {
        return true;  // zip, the current .xopp format
    }
    if (size >= 2 && head[0] == '\x1f' && head[1] == '\x8b') {
        return true;  // gzip, an older .xopp
    }
    const std::string_view text(head, static_cast<std::size_t>(size));
    return text.starts_with("<?xml") || text.starts_with("<xournal");
}

/// The names `base` may have had before its extension was stripped, most likely first.
auto originalCandidates(const fs::path& base)
        -> std::array<fs::path, 1 + RecoveryInventory::DOCUMENT_EXTENSIONS.size()> {
    std::array<fs::path, 1 + RecoveryInventory::DOCUMENT_EXTENSIONS.size()> names{};
    names[0] = base;
    for (std::size_t i = 0; i < RecoveryInventory::DOCUMENT_EXTENSIONS.size(); i++) {
        names[i + 1] = fs::path(base.string() + "." + RecoveryInventory::DOCUMENT_EXTENSIONS[i]);
    }
    return names;
}

/// Newest first, then by path. A candidate whose time could not be read goes last.
void sortCandidates(std::vector<RecoveryCandidate>& candidates) {
    std::sort(candidates.begin(), candidates.end(), [](const RecoveryCandidate& a, const RecoveryCandidate& b) {
        if (a.recoveryTime != b.recoveryTime) {
            return b.recoveryTime < a.recoveryTime;
        }
        return a.recoveryPath < b.recoveryPath;
    });
}

}  // namespace

auto RecoveryInventory::getAutosaveFolder() -> fs::path { return Util::getCacheSubfolder("autosaves"); }

auto RecoveryInventory::getOriginalFor(const fs::path& recoveryFile) -> fs::path {
    const std::string name = recoveryFile.filename().string();
    const auto suffixLength = static_cast<std::string::size_type>(std::strlen(AUTOSAVE_SUFFIX));
    if (!name.ends_with(AUTOSAVE_SUFFIX) || name.size() <= suffixLength) {
        return {};
    }

    std::string stem = name.substr(0, name.size() - suffixLength);
    if (stem.size() < 2 || stem.front() != '.') {
        // An unnamed autosave - `<pid>.xopp` in the autosave folder - belongs to no document.
        return {};
    }

    stem.erase(0, 1);
    return recoveryFile.parent_path() / stem;
}

auto RecoveryInventory::getRecoveryFor(const fs::path& document) -> fs::path {
    if (document.empty()) {
        return {};
    }

    // The same name AutosaveJob derives: a hidden file next to the document, with the
    // Xournal++ extensions stripped and the autosave suffix added.
    fs::path name = document.filename();
    Util::clearExtensions(name);

    return document.parent_path() / (std::string(".") + name.string() + AUTOSAVE_SUFFIX);
}

auto RecoveryInventory::looksLikeAutosave(const fs::path& path) -> bool {
    const std::string name = path.filename().string();
    return !name.empty() && !isTemporaryName(name) && name.ends_with(AUTOSAVE_SUFFIX);
}

auto RecoveryInventory::inspect(const fs::path& recoveryFile) -> std::optional<RecoveryCandidate> {
    RecoveryCandidate candidate;
    candidate.recoveryPath = recoveryFile;

    std::error_code error;
    const fs::file_status status = fs::symlink_status(recoveryFile, error);
    if (error) {
        candidate.validation = RecoveryValidation::Missing;
        candidate.error = error.message();
        return candidate;
    }
    if (!fs::exists(status)) {
        candidate.validation = RecoveryValidation::Missing;
        candidate.error = "the file does not exist";
        return candidate;
    }
    if (!fs::is_regular_file(status)) {
        candidate.validation = RecoveryValidation::NotARegularFile;
        candidate.error = "the path is not a regular file";
        return candidate;
    }

    error.clear();
    if (const auto writeTime = fs::last_write_time(recoveryFile, error); !error) {
        candidate.recoveryTime = std::chrono::clock_cast<std::chrono::system_clock>(writeTime);
    }
    error.clear();
    if (const auto size = fs::file_size(recoveryFile, error); !error) {
        candidate.size = size;
    }

    /*
     * The cheapest possible look at the contents: the format's magic bytes. Reading a document
     * here would block the caller on a file the user may not even care about, and a dashboard
     * lists candidates before it opens any of them.
     */
    std::ifstream stream(recoveryFile, std::ios::binary);
    if (!stream.is_open()) {
        candidate.validation = RecoveryValidation::Unreadable;
        candidate.error = "the file could not be opened for reading";
        return candidate;
    }

    char head[4] = {};
    stream.read(head, static_cast<std::streamsize>(sizeof(head)));
    const std::streamsize read = stream.gcount();
    if (read <= 0 && stream.bad()) {
        candidate.validation = RecoveryValidation::Unreadable;
        candidate.error = "the file could not be read";
        return candidate;
    }
    if (!looksLikeXournalDocument(head, read)) {
        candidate.validation = RecoveryValidation::UnrecognizedFormat;
        candidate.error = "the file does not start like a Xournal++ document";
        return candidate;
    }

    candidate.validation = RecoveryValidation::Ok;

    // The document the copy came from, if its name still says so and the file is still there.
    const fs::path base = getOriginalFor(recoveryFile);
    if (!base.empty()) {
        candidate.originalPath = base;
        for (const fs::path& name: originalCandidates(base)) {
            std::error_code originalError;
            if (fs::is_regular_file(name, originalError) && !originalError) {
                candidate.originalPath = name;
                candidate.originalExists = true;
                break;
            }
        }

        if (candidate.originalExists) {
            std::error_code originalError;
            if (const auto writeTime = fs::last_write_time(candidate.originalPath, originalError); !originalError) {
                candidate.originalTime = std::chrono::clock_cast<std::chrono::system_clock>(writeTime);
            }
            candidate.newerThanOriginal = candidate.recoveryTime && candidate.originalTime &&
                                          *candidate.recoveryTime > *candidate.originalTime;
        }
    }

    return candidate;
}

auto RecoveryInventory::scanFolder(const fs::path& folder) -> std::vector<RecoveryCandidate> {
    std::vector<RecoveryCandidate> candidates;

    std::error_code error;
    if (!fs::is_directory(folder, error) || error) {
        if (error) {
            g_warning("Could not look for recovery files in \"%s\": %s", folder.string().c_str(),
                      error.message().c_str());
        }
        return candidates;
    }

    /*
     * A named autosave is recognisable by its name wherever it sits; an unnamed one is only a
     * candidate because it is in the autosave folder, where every file is one.
     */
    const bool inAutosaveFolder = Util::isChildOrEquivalent(folder, getAutosaveFolder());

    fs::directory_iterator it(folder, fs::directory_options::skip_permission_denied, error);
    if (error) {
        g_warning("Could not look for recovery files in \"%s\": %s", folder.string().c_str(), error.message().c_str());
        return candidates;
    }

    const fs::directory_iterator end;
    while (it != end) {
        const fs::path path = it->path();
        const std::string name = path.filename().string();

        if (!name.empty() && !isTemporaryName(name) && (name.ends_with(AUTOSAVE_SUFFIX) || inAutosaveFolder)) {
            if (auto candidate = inspect(path); candidate.has_value()) {
                candidates.emplace_back(std::move(*candidate));
            }
        }

        it.increment(error);
        if (error) {
            // The folder changed under us or a part of it cannot be read. What was found so far
            // is still worth reporting.
            g_warning("Could not read all of \"%s\": %s", folder.string().c_str(), error.message().c_str());
            break;
        }
    }

    sortCandidates(candidates);
    return candidates;
}

auto RecoveryInventory::scan(const std::vector<fs::path>& folders) -> std::vector<RecoveryCandidate> {
    std::vector<RecoveryCandidate> candidates;

    for (const fs::path& folder: folders) {
        for (auto& candidate: scanFolder(folder)) {
            const bool known = std::any_of(candidates.begin(), candidates.end(), [&candidate](const auto& other) {
                return other.recoveryPath == candidate.recoveryPath;
            });
            if (!known) {
                candidates.emplace_back(std::move(candidate));
            }
        }
    }

    sortCandidates(candidates);
    return candidates;
}

auto RecoveryInventory::defaultSearchFolders() -> std::vector<fs::path> { return {getAutosaveFolder()}; }
