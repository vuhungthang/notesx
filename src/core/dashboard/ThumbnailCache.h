/*
 * Xournal++
 *
 * Where the dashboard keeps the previews it has read.
 *
 * Plan 006, step 2. The cache is keyed by the canonical path of a file together with its size and
 * modification time, so a version of a document has one entry and no invalidation pass is needed:
 * a file that changed is a different key, and a key that is never asked for again is harmless. A
 * negative answer - the document has no preview, or could not be read - is cached the same way, so
 * the expensive part happens once per version of a file rather than once per refresh.
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <chrono>    // for system_clock
#include <cstdint>   // for uint8_t
#include <optional>  // for optional
#include <string>    // for string
#include <vector>    // for vector

#include "filesystem.h"  // for path

namespace xoj::dashboard {

class ThumbnailCache {
public:
    /// What the cache holds for one version of a file.
    enum class Entry {
        /// Nothing: this version has not been looked at. It has to be read.
        Missing,
        /// An extracted preview.
        Image,
        /// The document was read and carries no preview.
        NoPreview,
        /// The document could not be read as a document.
        Corrupt,
    };

    /// The suffixes of the files an entry is kept in.
    static constexpr const char* IMAGE_SUFFIX = ".png";
    static constexpr const char* NO_PREVIEW_SUFFIX = ".none";
    static constexpr const char* CORRUPT_SUFFIX = ".corrupt";

    /// `folder` is created when it is first written to, never when it is only read.
    explicit ThumbnailCache(fs::path folder);

    /// The folder the application caches thumbnails in.
    static auto defaultFolder() -> fs::path;

    /**
     * The key of one version of a file, or an empty string when the file cannot be described - it
     * is gone, or it cannot be read - in which case there is nothing to cache.
     */
    static auto keyOf(const fs::path& path) -> std::string;

    /// The key of a path described by hand, which is what the tests and the recovery card use.
    static auto keyOf(const fs::path& canonicalPath, std::uintmax_t size,
                      const std::optional<std::chrono::system_clock::time_point>& modified) -> std::string;

    auto getFolder() const -> const fs::path&;

    /// What is held for the file's current version.
    auto entryOf(const fs::path& path) const -> Entry;

    /// The file holding the extracted preview, empty when there is none.
    auto imageFileOf(const fs::path& path) const -> fs::path;

    /// The extracted preview's bytes, empty when there is none to read.
    auto loadImage(const fs::path& path) const -> std::vector<std::uint8_t>;

    /// Keep an extracted preview. Returns whether it was written.
    auto storeImage(const fs::path& path, const std::vector<std::uint8_t>& png) const -> bool;

    /// Remember that this version has no preview, or is not a document. Returns whether it was written.
    auto storeAbsent(const fs::path& path, Entry entry) const -> bool;

private:
    /// The file an entry is kept in, whether or not it exists.
    auto entryFileOf(const fs::path& path, const char* suffix) const -> fs::path;

    fs::path folder;
};

}  // namespace xoj::dashboard
