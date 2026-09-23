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
 *
 * A request stays cancellable until its answer has been handed over or dropped, which includes the
 * time the answer spends waiting on the main context: the window being torn down and the answer
 * arriving are both main-context work, and the cancellation has to win whichever order they come
 * in.
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

    /**
     * Forget one request: its callback will not be called.
     *
     * A request is cancellable until its answer has been handed over or thrown away, which is
     * longer than the worker takes: an answer the worker has already posted sits on the caller's
     * main context until the main loop reaches it, and that is exactly when a window can be torn
     * down - the window's own teardown runs on the main context too. Cancelling from there has to
     * reach the queued answer, or the answer is handed to a dashboard that no longer exists.
     */
    void cancel(ThumbnailRequestId id);

    /**
     * Forget every outstanding request. Called when the dashboard goes away.
     *
     * With the same meaning as `cancel`, for every request: what is still being read and what has
     * already been posted are both dropped, so nothing can be handed to the caller afterwards.
     */
    void cancelAll();

    /**
     * The preview that has already been read for the file's current version, empty when there is
     * none. A card that knows a preview exists can show it without asking for it again, and the
     * caller finds out whether the bytes are usable - whether they are an image at all - for
     * itself.
     */
    auto cachedPreview(const fs::path& path) const -> std::vector<std::uint8_t>;

    /**
     * How many requests the worker has not finished reading yet.
     *
     * An answer the worker has handed to the main context is not counted: the reading is done with,
     * even though the answer may not have reached the caller yet. That is the number a dashboard
     * reports and its tests check - "is this document still being read?" - and it is not the number
     * of requests the caller can still cancel, which is larger while an answer is on its way.
     */
    auto pendingCount() const -> std::size_t;

    /// Block until the workers have nothing left to read. An answer that is already on the main
    /// context does not hold this up: it is the caller's own business when it reaches it.
    void waitIdle();

private:
    struct Request {
        ThumbnailRequestId id = 0;
        fs::path path;
        Callback deliver;
        std::shared_ptr<std::atomic_bool> cancelled;
    };

    /**
     * What one request is, from the moment it is accepted until its answer has been handed over or
     * thrown away.
     *
     * The requests live here rather than in the service because disposing of an answer must not
     * touch the service: GLib disposes of a source the service queued after the service can be
     * gone - a window is torn down while the answer is still on the main context - and forgetting
     * the request then is the only thing disposal has to do. The answers hold a reference to this,
     * so it outlives the service for as long as an answer of it is still around.
     */
    struct State {
        std::mutex mutex;
        std::condition_variable wake;
        std::condition_variable idle;
        std::deque<Request> queue;
        /**
         * Every request that can still be cancelled: from acceptance until its answer has been
         * delivered or dropped. The worker's own view - what it is reading - is `queue` and `busy`;
         * an entry here may well be one whose worker has already posted its answer.
         */
        std::map<ThumbnailRequestId, std::shared_ptr<std::atomic_bool>> cancellable;
        std::size_t busy = 0;
        bool stopping = false;
        ThumbnailRequestId nextId = 1;
    };

    struct Delivery {
        Callback deliver;
        ThumbnailResult result;
        ThumbnailRequestId id = 0;
        std::shared_ptr<std::atomic_bool> cancelled;
        /// Where the request is registered, so the answer can forget it even after the service is
        /// gone.
        std::shared_ptr<State> state;

        /// The request is finished with: it can no longer be cancelled and the service does not
        /// have to remember it any more.
        void dispose();
    };

    void work();
    /// The worker is done with a request. An answer of it may still be on its way to the caller.
    void finish(ThumbnailRequestId id, bool answerOnItsWay);
    /// Hand a result to the caller's main context.
    void post(Delivery&& delivery);

    static auto runDelivery(gpointer data) -> gboolean;
    static void freeDelivery(gpointer data);

    std::shared_ptr<ThumbnailCache> cache;
    GMainContext* context;
    std::shared_ptr<State> state = std::make_shared<State>();

    std::thread worker;
};

}  // namespace xoj::dashboard
