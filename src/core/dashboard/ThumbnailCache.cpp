#include "ThumbnailCache.h"

#include <chrono>   // for clock_cast, system_clock
#include <cstddef>  // for size_t
#include <fstream>  // for ifstream, ofstream
#include <iterator>  // for istreambuf_iterator
#include <system_error>  // for error_code
#include <utility>       // for move

#include <glib.h>  // for g_compute_checksum_for_string, g_free, G_CHECKSUM_SHA256

#include "dashboard/DashboardTypes.h"  // for canonicalPath
#include "util/PathUtil.h"             // for getCacheSubfolder

using namespace xoj::dashboard;

namespace {

/// Whether a regular file is there, without letting a failure to look throw.
auto isRegularFile(const fs::path& path) -> bool {
    std::error_code error;
    return fs::is_regular_file(path, error) && !error;
}

/// Write `bytes` so that a reader sees either the whole file or nothing: never half of it.
auto writeFile(const fs::path& path, const std::vector<std::uint8_t>& bytes) -> bool {
    std::error_code error;
    fs::create_directories(path.parent_path(), error);
    if (error) {
        return false;
    }

    const fs::path temporary = fs::path(path.string() + ".part");
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) {
            return false;
        }
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!out.good()) {
            out.close();
            fs::remove(temporary, error);
            return false;
        }
    }

    error.clear();
    fs::rename(temporary, path, error);
    if (error) {
        fs::remove(temporary, error);
        return false;
    }
    return true;
}

/// Create an empty file, which is all a negative answer needs to be remembered.
auto touchFile(const fs::path& path) -> bool {
    std::error_code error;
    fs::create_directories(path.parent_path(), error);
    if (error) {
        return false;
    }
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    return out.is_open();
}

}  // namespace

ThumbnailCache::ThumbnailCache(fs::path folder): folder(std::move(folder)) {}

auto ThumbnailCache::defaultFolder() -> fs::path {
    // The cache folder is created when something is written to it, not when the dashboard is built.
    return Util::getCacheSubfolder("thumbnails", false);
}

auto ThumbnailCache::keyOf(const fs::path& canonicalPath, std::uintmax_t size,
                           const std::optional<std::chrono::system_clock::time_point>& modified) -> std::string {
    if (canonicalPath.empty()) {
        return {};
    }

    std::string material = canonicalPath.generic_string();
    material += '\n';
    material += std::to_string(size);
    material += '\n';
    material += modified.has_value() ? std::to_string(modified->time_since_epoch().count()) : std::string("-");

    gchar* digest = g_compute_checksum_for_string(G_CHECKSUM_SHA256, material.c_str(), -1);
    std::string key = digest == nullptr ? std::string() : std::string(digest);
    g_free(digest);
    return key;
}

auto ThumbnailCache::keyOf(const fs::path& path) -> std::string {
    const fs::path canonical = canonicalPath(path);
    if (canonical.empty()) {
        return {};
    }

    std::error_code error;
    const std::uintmax_t size = fs::file_size(canonical, error);
    if (error) {
        return {};
    }

    error.clear();
    const auto writeTime = fs::last_write_time(canonical, error);
    if (error) {
        return {};
    }

    return keyOf(canonical, size, std::chrono::clock_cast<std::chrono::system_clock>(writeTime));
}

auto ThumbnailCache::getFolder() const -> const fs::path& { return this->folder; }

auto ThumbnailCache::entryFileOf(const fs::path& path, const char* suffix) const -> fs::path {
    const std::string key = keyOf(path);
    if (key.empty()) {
        return {};
    }
    return this->folder / (key + suffix);
}

auto ThumbnailCache::entryOf(const fs::path& path) const -> Entry {
    const fs::path image = entryFileOf(path, IMAGE_SUFFIX);
    if (image.empty()) {
        return Entry::Missing;
    }
    if (isRegularFile(image)) {
        return Entry::Image;
    }
    if (isRegularFile(entryFileOf(path, NO_PREVIEW_SUFFIX))) {
        return Entry::NoPreview;
    }
    if (isRegularFile(entryFileOf(path, CORRUPT_SUFFIX))) {
        return Entry::Corrupt;
    }
    return Entry::Missing;
}

auto ThumbnailCache::imageFileOf(const fs::path& path) const -> fs::path {
    const fs::path image = entryFileOf(path, IMAGE_SUFFIX);
    return isRegularFile(image) ? image : fs::path();
}

auto ThumbnailCache::loadImage(const fs::path& path) const -> std::vector<std::uint8_t> {
    const fs::path image = imageFileOf(path);
    if (image.empty()) {
        return {};
    }

    std::ifstream in(image, std::ios::binary);
    if (!in.is_open()) {
        return {};
    }
    const std::vector<char> bytes{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    return std::vector<std::uint8_t>(bytes.begin(), bytes.end());
}

auto ThumbnailCache::storeImage(const fs::path& path, const std::vector<std::uint8_t>& png) const -> bool {
    const fs::path image = entryFileOf(path, IMAGE_SUFFIX);
    if (image.empty() || png.empty()) {
        return false;
    }

    std::error_code error;
    // A file that has a preview is not also remembered as having none.
    fs::remove(entryFileOf(path, NO_PREVIEW_SUFFIX), error);
    fs::remove(entryFileOf(path, CORRUPT_SUFFIX), error);

    return writeFile(image, png);
}

auto ThumbnailCache::storeAbsent(const fs::path& path, Entry entry) const -> bool {
    if (entry != Entry::NoPreview && entry != Entry::Corrupt) {
        return false;
    }

    const fs::path marker = entryFileOf(path, entry == Entry::NoPreview ? NO_PREVIEW_SUFFIX : CORRUPT_SUFFIX);
    if (marker.empty()) {
        return false;
    }

    std::error_code error;
    fs::remove(entryFileOf(path, IMAGE_SUFFIX), error);
    fs::remove(entryFileOf(path, entry == Entry::NoPreview ? CORRUPT_SUFFIX : NO_PREVIEW_SUFFIX), error);

    return touchFile(marker);
}
