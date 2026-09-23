#include "ThumbnailService.h"

#include <cstddef>  // for size_t
#include <utility>  // for move

using namespace xoj::dashboard;

namespace {

/// The cache entry an answer is remembered as.
auto entryFor(DocumentCard::Preview state) -> ThumbnailCache::Entry {
    switch (state) {
        case DocumentCard::Preview::Available:
            return ThumbnailCache::Entry::Image;
        case DocumentCard::Preview::None:
            return ThumbnailCache::Entry::NoPreview;
        case DocumentCard::Preview::Corrupt:
            return ThumbnailCache::Entry::Corrupt;
        case DocumentCard::Preview::Unknown:
            break;
    }
    return ThumbnailCache::Entry::Missing;
}

}  // namespace

auto xoj::dashboard::previewStateOf(xoj::preview::PreviewStatus status) -> DocumentCard::Preview {
    switch (status) {
        case xoj::preview::PreviewStatus::Extracted:
            return DocumentCard::Preview::Available;
        case xoj::preview::PreviewStatus::NoPreview:
        case xoj::preview::PreviewStatus::Unsupported:
            // A PDF, or a document that carries no preview: there is no thumbnail to show, and
            // asking again cannot change that.
            return DocumentCard::Preview::None;
        case xoj::preview::PreviewStatus::Unreadable:
        case xoj::preview::PreviewStatus::Corrupt:
        default:
            return DocumentCard::Preview::Corrupt;
    }
}

ThumbnailService::ThumbnailService(std::shared_ptr<ThumbnailCache> cache, GMainContext* context):
        cache(std::move(cache)), context(context) {
    this->worker = std::thread([this] { work(); });
}

ThumbnailService::~ThumbnailService() {
    {
        const std::lock_guard lock(this->state->mutex);
        this->state->stopping = true;
    }
    /*
     * Anything that is waiting to be handed over is dropped: a callback that reached a window that
     * is being torn down is worse than a card without a preview. This covers the answers that are
     * already on the main context as well as what is still being read.
     */
    this->cancelAll();
    this->state->wake.notify_all();

    if (this->worker.joinable()) {
        this->worker.join();
    }
}

auto ThumbnailService::request(const fs::path& path, Callback deliver) -> ThumbnailRequestId {
    const fs::path canonical = canonicalPath(path);
    if (canonical.empty() || !deliver) {
        return 0;
    }

    /*
     * A cached answer is handed over at once. The caller is the main thread, so this is still a
     * callback on the main thread - it just does not cost a worker or a round trip through the main
     * context, which is what makes a dashboard with a hundred cards cheap to build.
     */
    switch (this->cache->entryOf(canonical)) {
        case ThumbnailCache::Entry::Image: {
            ThumbnailResult result;
            result.path = canonical;
            result.state = DocumentCard::Preview::Available;
            result.png = this->cache->loadImage(canonical);
            if (!result.png.empty()) {
                deliver(result);
                return 0;
            }
            // The cache says there is an image but it cannot be read: read the document again.
            break;
        }
        case ThumbnailCache::Entry::NoPreview: {
            ThumbnailResult result;
            result.path = canonical;
            result.state = DocumentCard::Preview::None;
            deliver(result);
            return 0;
        }
        case ThumbnailCache::Entry::Corrupt: {
            ThumbnailResult result;
            result.path = canonical;
            result.state = DocumentCard::Preview::Corrupt;
            result.error = "the cached preview is not usable";
            deliver(result);
            return 0;
        }
        case ThumbnailCache::Entry::Missing:
            break;
    }

    Request request;
    request.path = canonical;
    request.deliver = std::move(deliver);
    request.cancelled = std::make_shared<std::atomic_bool>(false);
    {
        const std::lock_guard lock(this->state->mutex);
        if (this->state->stopping) {
            return 0;
        }
        request.id = this->state->nextId++;
        this->state->cancellable.emplace(request.id, request.cancelled);
        this->state->queue.emplace_back(std::move(request));
    }
    this->state->wake.notify_one();

    return request.id;
}

void ThumbnailService::cancel(ThumbnailRequestId id) {
    if (id == 0) {
        return;
    }
    const std::lock_guard lock(this->state->mutex);
    const auto known = this->state->cancellable.find(id);
    if (known != this->state->cancellable.end()) {
        known->second->store(true);
    }
}

void ThumbnailService::cancelAll() {
    const std::lock_guard lock(this->state->mutex);
    for (auto& [id, cancelled]: this->state->cancellable) {
        cancelled->store(true);
    }
}

auto ThumbnailService::cachedPreview(const fs::path& path) const -> std::vector<std::uint8_t> {
    return this->cache->loadImage(path);
}

auto ThumbnailService::pendingCount() const -> std::size_t {
    const std::lock_guard lock(this->state->mutex);
    // What the worker has: the requests it has not taken yet and the one it is reading. A request
    // whose answer is already on the main context is not being read any more and is not counted,
    // even though cancelling it still works.
    return this->state->queue.size() + this->state->busy;
}

void ThumbnailService::waitIdle() {
    std::unique_lock lock(this->state->mutex);
    this->state->idle.wait(lock, [this] { return this->state->queue.empty() && this->state->busy == 0; });
}

void ThumbnailService::finish(ThumbnailRequestId id, bool answerOnItsWay) {
    {
        const std::lock_guard lock(this->state->mutex);
        if (!answerOnItsWay) {
            /*
             * There is no answer to wait for and none to cancel: the request is finished with here,
             * whether it was cancelled before the worker reached it or the worker skipped it for
             * having been cancelled.
             */
            this->state->cancellable.erase(id);
        }
        this->state->busy--;
    }
    this->state->idle.notify_all();
}

void ThumbnailService::work() {
    for (;;) {
        Request request;
        {
            std::unique_lock lock(this->state->mutex);
            this->state->wake.wait(lock, [this] { return this->state->stopping || !this->state->queue.empty(); });
            if (this->state->queue.empty()) {
                if (this->state->stopping) {
                    return;
                }
                continue;
            }
            request = std::move(this->state->queue.front());
            this->state->queue.pop_front();
            this->state->busy++;
        }

        bool answerOnItsWay = false;
        if (!request.cancelled->load()) {
            /*
             * Reading a document cannot be interrupted half way, so a request that is cancelled
             * while it is being read is still read to the end and its answer is still cached - the
             * next dashboard that wants it gets it at once. What cancellation decides is whether
             * the answer is handed to the caller, and it is decided here, at the last moment, and
             * again when the answer is about to be handed over.
             */
            const xoj::preview::PreviewResult preview = xoj::preview::extractPreview(request.path);

            ThumbnailResult result;
            result.path = request.path;
            result.state = previewStateOf(preview.status);
            result.error = preview.error;

            if (preview.extracted()) {
                result.png = preview.data;
                this->cache->storeImage(request.path, preview.data);
            } else {
                this->cache->storeAbsent(request.path, entryFor(result.state));
            }

            if (!request.cancelled->load()) {
                Delivery delivery;
                delivery.deliver = std::move(request.deliver);
                delivery.result = std::move(result);
                delivery.cancelled = request.cancelled;
                delivery.id = request.id;
                delivery.state = this->state;
                post(std::move(delivery));
                // The request is kept cancellable until the answer has run or been thrown away:
                // from here the cancellation has to reach the answer on the main context, not the
                // worker that has already finished with the request.
                answerOnItsWay = true;
            }
        }

        finish(request.id, answerOnItsWay);
    }
}

void ThumbnailService::post(Delivery&& delivery) {
    auto* payload = new Delivery(std::move(delivery));
    /*
     * The result is handed to the main context rather than called here: the caller's callback
     * touches widgets. glib keeps the payload alive until the source runs, and calls the free
     * function if it never does - the answer is discarded rather than delivered then, so the
     * request is forgotten there too.
     */
    g_main_context_invoke_full(this->context, G_PRIORITY_DEFAULT_IDLE, runDelivery, payload, freeDelivery);
}

auto ThumbnailService::runDelivery(gpointer data) -> gboolean {
    auto* delivery = static_cast<Delivery*>(data);
    /*
     * The answer is read once: the request is forgotten and then either handed over or dropped, so
     * a cancellation that arrives while this runs cannot leave the answer half-handed-over.
     */
    const bool cancelled = delivery->cancelled->load();
    delivery->dispose();
    if (!cancelled) {
        delivery->deliver(delivery->result);
    }
    return G_SOURCE_REMOVE;
}

void ThumbnailService::freeDelivery(gpointer data) {
    auto* delivery = static_cast<Delivery*>(data);
    // The answer never ran - the context it was posted to went away first. The request is forgotten
    // here rather than left in the service's bookkeeping.
    delivery->dispose();
    delete delivery;
}

void ThumbnailService::Delivery::dispose() {
    const std::lock_guard lock(this->state->mutex);
    this->state->cancellable.erase(this->id);
}
