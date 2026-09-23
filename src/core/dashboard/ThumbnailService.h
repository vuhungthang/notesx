/*
 * Xournal++
 *
 * Reading document previews for the dashboard, in the background and on demand.
 *
 * Plan 006, step 2. The dashboard asks for a card's preview and is called back later, on the main
 * context it was built on, so listing a folder of notes never blocks on opening them. Extraction
 * happens on a worker thread; a version of a document that has already been read is answered from
 * the cache without a worker at all. Every request can be cancelled - a card that scrolls away, a
 * section the user collapsed, the window closing - and a cancelled request is never delivered.
 *
 * The reading itself is `xoj::preview::extractPreview`, the same code the desktop thumbnailer
 * calls, so the two cannot disagree about what a document's preview is.
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <atomic>              // for atomic_bool
#include <condition_variable>  // for condition_variable
#include <cstddef>             // for size_t
#include <cstdint>             // for uint8_t, uint64_t
#include <deque>               // for deque
#include <functional>          // for function
#include <map>                 // for map
#include <memory>              // for shared_ptr
#include <mutex>               // for mutex
#include <string>              // for string
#include <thread>              // for thread
#include <vector>              // for vector

#include <glib.h>  // for GMainContext

#include "util/PreviewExtraction.h"  // for PreviewStatus

#include "DashboardTypes.h"  // for DocumentCard
#include "ThumbnailCache.h"  // for ThumbnailCache
#include "filesystem.h"      // for path

namespace xoj::dashboard {

/// Identifies one request. 0 is never handed out: it means "already answered".
using ThumbnailRequestId = std::uint64_t;

/// What came of one request.
struct ThumbnailResult {
    /// The canonical path of the file.
    fs::path path;
    DocumentCard::Preview state = DocumentCard::Preview::Unknown;
    /// The preview's bytes, a PNG. Empty unless the state is `Available`.
    std::vector<std::uint8_t> png;
    /// Why there is no usable preview, for the card's details. Empty on success.
    std::string error;

    auto available() const -> bool { return state == DocumentCard::Preview::Available; }
};

/// The card state a reading maps to.
auto previewStateOf(xoj::preview::PreviewStatus status) -> DocumentCard::Preview;

/**
 * Reads previews in the background and hands the answers to the caller's main context.
 *
 * A request whose answer is already in the cache is delivered before `request()` returns - the
 * caller is the main thread, so that is still the main thread. Everything else is delivered from
 * the given `GMainContext` (the default one when null), which is where a widget may be touched.
 */
class ThumbnailService {
public:
    using Callback = std::function<void(const ThumbnailResult&)>;

    /**
     * @param cache the shared cache. The service writes to it; a cache may be shared with another
     *              service, which is what lets a second window start warm.
     * @param context where the callbacks run. Null means the default main context.
     */
    explicit ThumbnailService(std::shared_ptr<ThumbnailCache> cache, GMainContext* context = nullptr);
    ~ThumbnailService();

    ThumbnailService(const ThumbnailService&) = delete;
    auto operator=(const ThumbnailService&) -> ThumbnailService& = delete;

    /**
     * Ask for a file's preview.
     *
     * @return the id, or 0 when the answer was already known and `deliver` has been called.
     */
    auto request(const fs::path& path, Callback deliver) -> ThumbnailRequestId;

    /// Forget one request: its callback will not be called.
    void cancel(ThumbnailRequestId id);

    /// Forget every outstanding request. Called when the dashboard goes away.
    void cancelAll();

    /**
     * The preview that has already been read for the file's current version, empty when there is
     * none. A card that knows a preview exists can show it without asking for it again, and the
     * caller finds out whether the bytes are usable - whether they are an image at all - for
     * itself.
     */
    auto cachedPreview(const fs::path& path) const -> std::vector<std::uint8_t>;

    /// How many requests have been accepted and not finished. For tests and for the UI to report.
    auto pendingCount() const -> std::size_t;

    /// Block until the workers have nothing left to do.
    void waitIdle();

private:
    struct Request {
        ThumbnailRequestId id = 0;
        fs::path path;
        Callback deliver;
        std::shared_ptr<std::atomic_bool> cancelled;
    };

    struct Delivery {
        Callback deliver;
        ThumbnailResult result;
        std::shared_ptr<std::atomic_bool> cancelled;
    };

    void work();
    void finish(ThumbnailRequestId id);
    /// Hand a result to the caller's main context.
    void post(Delivery&& delivery);

    static auto runDelivery(gpointer data) -> gboolean;
    static void freeDelivery(gpointer data);

    std::shared_ptr<ThumbnailCache> cache;
    GMainContext* context;

    std::thread worker;
    mutable std::mutex mutex;
    std::condition_variable wake;
    std::condition_variable idle;
    std::deque<Request> queue;
    std::map<ThumbnailRequestId, std::shared_ptr<std::atomic_bool>> outstanding;
    std::size_t busy = 0;
    bool stopping = false;
    ThumbnailRequestId nextId = 1;
};

}  // namespace xoj::dashboard
