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
        const std::lock_guard lock(this->mutex);
        this->stopping = true;
        // Anything that is waiting to be handed over is dropped: a callback that reached a window
        // that is being torn down is worse than a card without a preview.
        for (auto& [id, cancelled]: this->outstanding) {
            cancelled->store(true);
        }
    }
    this->wake.notify_all();

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
        const std::lock_guard lock(this->mutex);
        if (this->stopping) {
            return 0;
        }
        request.id = this->nextId++;
        this->outstanding.emplace(request.id, request.cancelled);
        this->queue.emplace_back(std::move(request));
    }
    this->wake.notify_one();

    return request.id;
}

void ThumbnailService::cancel(ThumbnailRequestId id) {
    if (id == 0) {
        return;
    }
    const std::lock_guard lock(this->mutex);
    const auto known = this->outstanding.find(id);
    if (known != this->outstanding.end()) {
        known->second->store(true);
    }
}

void ThumbnailService::cancelAll() {
    const std::lock_guard lock(this->mutex);
    for (auto& [id, cancelled]: this->outstanding) {
        cancelled->store(true);
    }
}

auto ThumbnailService::cachedPreview(const fs::path& path) const -> std::vector<std::uint8_t> {
    return this->cache->loadImage(path);
}

auto ThumbnailService::pendingCount() const -> std::size_t {
    const std::lock_guard lock(this->mutex);
    return this->outstanding.size();
}

void ThumbnailService::waitIdle() {
    std::unique_lock lock(this->mutex);
    this->idle.wait(lock, [this] { return this->queue.empty() && this->busy == 0; });
}

void ThumbnailService::finish(ThumbnailRequestId id) {
    {
        const std::lock_guard lock(this->mutex);
        // The request is done with: it is no longer outstanding, so it cannot be cancelled any
        // more and the dashboard is no longer waiting for it.
        this->outstanding.erase(id);
        this->busy--;
    }
    this->idle.notify_all();
}

void ThumbnailService::work() {
    for (;;) {
        Request request;
        {
            std::unique_lock lock(this->mutex);
            this->wake.wait(lock, [this] { return this->stopping || !this->queue.empty(); });
            if (this->queue.empty()) {
                if (this->stopping) {
                    return;
                }
                continue;
            }
            request = std::move(this->queue.front());
            this->queue.pop_front();
            this->busy++;
        }

        if (!request.cancelled->load()) {
            /*
             * Reading a document cannot be interrupted half way, so a request that is cancelled
             * while it is being read is still read to the end and its answer is still cached - the
             * next dashboard that wants it gets it at once. What cancellation decides is whether
             * the answer is handed to the caller, and it is decided here, at the last moment.
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
                post(std::move(delivery));
            }
        }

        finish(request.id);
    }
}

void ThumbnailService::post(Delivery&& delivery) {
    auto* payload = new Delivery(std::move(delivery));
    /*
     * The result is handed to the main context rather than called here: the caller's callback
     * touches widgets. glib keeps the payload alive until the source runs, and calls the free
     * function if it never does.
     */
    g_main_context_invoke_full(this->context, G_PRIORITY_DEFAULT_IDLE, runDelivery, payload, freeDelivery);
}

auto ThumbnailService::runDelivery(gpointer data) -> gboolean {
    auto* delivery = static_cast<Delivery*>(data);
    if (!delivery->cancelled->load()) {
        delivery->deliver(delivery->result);
    }
    return G_SOURCE_REMOVE;
}

void ThumbnailService::freeDelivery(gpointer data) { delete static_cast<Delivery*>(data); }
