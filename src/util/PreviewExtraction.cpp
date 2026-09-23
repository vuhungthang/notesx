#include "util/PreviewExtraction.h"

#include <cstddef>  // for size_t
#include <utility>  // for move

#include <glib.h>  // for gsize

#include "util/XojPreviewExtractor.h"  // for XojPreviewExtractor, PreviewExtractResult
#include "util/i18n.h"                 // for _F

using namespace xoj::preview;

namespace {

auto statusOf(PreviewExtractResult result) -> PreviewStatus {
    switch (result) {
        case PREVIEW_RESULT_IMAGE_READ:
            return PreviewStatus::Extracted;
        case PREVIEW_RESULT_NO_PREVIEW:
            return PreviewStatus::NoPreview;
        case PREVIEW_RESULT_BAD_FILE_EXTENSION:
            return PreviewStatus::Unsupported;
        case PREVIEW_RESULT_COULD_NOT_OPEN_FILE:
            return PreviewStatus::Unreadable;
        case PREVIEW_RESULT_ERROR_READING_PREVIEW:
        default:
            return PreviewStatus::Corrupt;
    }
}

auto messageFor(PreviewStatus status) -> std::string {
    switch (status) {
        case PreviewStatus::Extracted:
            return {};
        case PreviewStatus::NoPreview:
            return _("This document has no preview");
        case PreviewStatus::Unsupported:
            return _("This kind of file has no preview to read");
        case PreviewStatus::Unreadable:
            return _("The file could not be opened");
        case PreviewStatus::Corrupt:
        default:
            return _("The file could not be read as a Xournal++ document");
    }
}

}  // namespace

auto xoj::preview::extractPreview(const fs::path& path) -> PreviewResult {
    XojPreviewExtractor extractor;
    const PreviewExtractResult result = extractor.readFile(path);

    PreviewResult preview;
    preview.status = statusOf(result);
    if (result != PREVIEW_RESULT_IMAGE_READ) {
        preview.error = messageFor(preview.status);
        return preview;
    }

    gsize length = 0;
    unsigned char* data = extractor.getData(length);
    if (data == nullptr || length == 0) {
        // The extractor said it read an image but handed back nothing: not something a caller can
        // use, and not something to report as a preview.
        preview.status = PreviewStatus::NoPreview;
        preview.error = messageFor(PreviewStatus::NoPreview);
        return preview;
    }

    preview.data.assign(data, data + static_cast<std::size_t>(length));
    return preview;
}
