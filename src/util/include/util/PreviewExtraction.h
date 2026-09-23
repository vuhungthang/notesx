/*
 * Xournal++
 *
 * Reading the preview a Xournal++ document carries, as one call that both the application and the
 * desktop thumbnailer can make.
 *
 * Plan 006, step 2. The extractor below is the reusable half of `xournalpp-thumbnailer`: the
 * executable keeps what makes a thumbnail a desktop thumbnail (the icon it stamps onto the preview
 * and the file it writes) and shares the reading, the format checks and the reasons for failure
 * with the application's thumbnail service. Nothing here depends on GTK or on the application, so
 * the thumbnailer stays a thin, standalone program.
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <cstdint>  // for uint8_t
#include <string>   // for string
#include <vector>   // for vector

#include "filesystem.h"  // for path

namespace xoj::preview {

/**
 * What reading a file's preview ended in.
 *
 * The three ways of having no image are kept apart because the dashboard shows them differently: a
 * document without a preview and a file that is not a document both get a placeholder and are not
 * asked again, while a corrupt document gets an error state.
 */
enum class PreviewStatus {
    /// A preview was read.
    Extracted,
    /// The document is a Xournal++ document but carries no preview.
    NoPreview,
    /// Not a file kind the extractor reads.
    Unsupported,
    /// Not there, or there and it cannot be opened.
    Unreadable,
    /// Readable, but not a Xournal++ document that could be read.
    Corrupt,
};

/// A preview, or the reason there is none.
struct PreviewResult {
    PreviewStatus status = PreviewStatus::Unreadable;
    /// The preview's bytes, which are a PNG. Empty unless the status is `Extracted`.
    std::vector<std::uint8_t> data;
    /// Why there is no preview, for a log or a card's details. Empty on success.
    std::string error;

    auto extracted() const -> bool { return status == PreviewStatus::Extracted; }
};

/**
 * Read the preview out of `path`.
 *
 * Never throws and never writes anything: a file that cannot be read, one that is not a Xournal++
 * document and one that is a truncated archive all come back as a result with a status.
 */
auto extractPreview(const fs::path& path) -> PreviewResult;

}  // namespace xoj::preview
