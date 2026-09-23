/*
 * Xournal++
 *
 * This file is part of the Xournal UnitTests
 *
 * Plan 006, step 2: the thumbnail cache and the service that fills it.
 *
 * A dashboard of a hundred cards must not open a hundred documents on the main thread, and it must
 * show a preview of the version of the file that is there now. These tests pin both down: the work
 * happens on a worker and the answer arrives on the main context, an answer already known is
 * handed over without a worker, a file that changed is read again, and a request that was
 * cancelled - or a service that went away - is never delivered. That last one includes the request
 * whose worker is already finished with it and whose answer is waiting on the main context: the
 * caller has to be able to cancel it there too, because that is where a window being torn down
 * runs.
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#include <chrono>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <glib.h>
#include <gtest/gtest.h>

#include "dashboard/DashboardModel.h"
#include "dashboard/ThumbnailCache.h"
#include "dashboard/ThumbnailService.h"

#include "config-test.h"
#include "filesystem.h"

using namespace xoj::dashboard;

namespace {

auto freshDir(const char* name) -> fs::path {
    const fs::path dir = fs::temp_directory_path() / name;
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}

/// A copy of a fixture, so a test may modify it without touching the sources.
auto copyFixture(const fs::path& fixture, const fs::path& destination) -> fs::path {
    fs::copy_file(fixture, destination, fs::copy_options::overwrite_existing);
    return destination;
}

/**
 * Runs the default main context until `done` holds, so a test never hangs on a callback that is
 * never delivered - the failure then reads as a test that ran out of time, not as a stuck suite.
 */
auto pump(const std::function<bool()>& done, int timeoutMs = 5000) -> bool {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (!done() && std::chrono::steady_clock::now() < deadline) {
        while (g_main_context_iteration(nullptr, FALSE)) {}
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return done();
}

/**
 * A main context of the test's own, owned by the test thread.
 *
 * A worker that has an answer to hand over asks the context to run it. While someone owns the
 * context, the answer waits its turn on it, and that is the state the application is in whenever
 * the main loop runs: an answer the worker has finished with - and cannot be asked about any more -
 * is sitting on the main context, waiting for the loop to reach it. The tests below need exactly
 * that state, so they own the context themselves and decide when it is drained.
 */
class OwnedContext {
public:
    OwnedContext(): context(g_main_context_new()) {
        if (g_main_context_acquire(this->context) == FALSE) {
            g_main_context_unref(this->context);
            this->context = nullptr;
        }
    }
    ~OwnedContext() {
        if (this->context != nullptr) {
            g_main_context_release(this->context);
            g_main_context_unref(this->context);
        }
    }

    OwnedContext(const OwnedContext&) = delete;
    auto operator=(const OwnedContext&) -> OwnedContext& = delete;

    operator GMainContext*() const { return this->context; }

    /// Whether the test thread got to own the context. Nothing below makes sense without it.
    auto owned() const -> bool { return this->context != nullptr; }

    /// Runs everything that is waiting on the context, as a main loop would.
    void drain() {
        while (g_main_context_iteration(this->context, FALSE)) {}
    }

private:
    GMainContext* context;
};

}  // namespace

TEST(ThumbnailCache, theKeyIsTheCanonicalPathAndTheVersionOfTheFile) {
    const fs::path dir = freshDir("xournalpp-test-units_thumbnailKey");
    const fs::path document = copyFixture(GET_TESTFILE(u8"packaged_xopp/testPreview.xopp"), dir / "notes.xopp");
    const fs::path link = dir / "link.xopp";
    std::error_code error;
    fs::create_symlink(document, link, error);
    ASSERT_FALSE(error);

    const std::string key = ThumbnailCache::keyOf(document);
    ASSERT_FALSE(key.empty());
    EXPECT_EQ(ThumbnailCache::keyOf(link), key) << "the same file through a symlink is the same version";

    // The key describes the version, so any change to the file is a different key.
    fs::last_write_time(document, fs::last_write_time(document) + std::chrono::seconds(5), error);
    ASSERT_FALSE(error);
    EXPECT_NE(ThumbnailCache::keyOf(document), key);

    // A file that is not there has no version and therefore nothing to cache.
    EXPECT_TRUE(ThumbnailCache::keyOf(dir / "gone.xopp").empty());

    fs::remove_all(dir);
}

TEST(ThumbnailCache, keepsAndReportsWhatWasExtracted) {
    const fs::path dir = freshDir("xournalpp-test-units_thumbnailCache");
    const fs::path document = copyFixture(GET_TESTFILE(u8"packaged_xopp/testPreview.xopp"), dir / "notes.xopp");
    const ThumbnailCache cache(dir / "cache");

    ASSERT_FALSE(fs::exists(cache.getFolder())) << "reading the cache does not create it";
    EXPECT_EQ(cache.entryOf(document), ThumbnailCache::Entry::Missing);
    EXPECT_TRUE(cache.imageFileOf(document).empty());
    EXPECT_TRUE(cache.loadImage(document).empty());

    const std::vector<std::uint8_t> png{1, 2, 3, 4};
    ASSERT_TRUE(cache.storeImage(document, png));
    EXPECT_EQ(cache.entryOf(document), ThumbnailCache::Entry::Image);
    EXPECT_EQ(cache.loadImage(document), png);
    EXPECT_TRUE(fs::is_regular_file(cache.imageFileOf(document)));

    // A negative answer is remembered the same way, so the file is not read again for every refresh.
    ASSERT_TRUE(cache.storeAbsent(document, ThumbnailCache::Entry::NoPreview));
    EXPECT_EQ(cache.entryOf(document), ThumbnailCache::Entry::NoPreview);
    EXPECT_TRUE(cache.loadImage(document).empty());

    ASSERT_TRUE(cache.storeAbsent(document, ThumbnailCache::Entry::Corrupt));
    EXPECT_EQ(cache.entryOf(document), ThumbnailCache::Entry::Corrupt);

    // An image and a negative answer for the same file are mutually exclusive.
    ASSERT_TRUE(cache.storeImage(document, png));
    EXPECT_EQ(cache.entryOf(document), ThumbnailCache::Entry::Image);

    fs::remove_all(dir);
}

TEST(ThumbnailService, extractsAPreviewOnAWorkerAndAnswersOnTheMainContext) {
    const fs::path dir = freshDir("xournalpp-test-units_thumbnailService");
    const fs::path document = copyFixture(GET_TESTFILE(u8"packaged_xopp/testPreview.xopp"), dir / "notes.xopp");

    auto cache = std::make_shared<ThumbnailCache>(dir / "cache");
    ThumbnailService service(cache);

    std::vector<ThumbnailResult> delivered;
    const ThumbnailRequestId id = service.request(document, [&delivered](const ThumbnailResult& result) {
        delivered.emplace_back(result);
        // Called from the main context, which is where this test's assertions can see it.
        EXPECT_EQ(g_main_context_is_owner(nullptr), TRUE);
    });
    ASSERT_NE(id, 0U) << "the request went to a worker";
    EXPECT_EQ(delivered.size(), 0U) << "nothing is delivered before the main context runs";

    ASSERT_TRUE(pump([&delivered] { return !delivered.empty(); })) << "the answer never arrived";
    ASSERT_EQ(delivered.size(), 1U);
    EXPECT_EQ(delivered.front().path, DashboardModel::canonicalize(document));
    EXPECT_EQ(delivered.front().state, DocumentCard::Preview::Available);
    EXPECT_TRUE(delivered.front().error.empty());
    EXPECT_EQ(std::string(delivered.front().png.begin(), delivered.front().png.end()), "CppUnitTestString \n");

    service.waitIdle();
    EXPECT_EQ(cache->entryOf(document), ThumbnailCache::Entry::Image) << "the answer was cached";

    fs::remove_all(dir);
}

TEST(ThumbnailService, aDocumentWithoutAPreviewIsReportedAndRememberedSo) {
    const fs::path dir = freshDir("xournalpp-test-units_thumbnailNoPreview");
    const fs::path document =
            copyFixture(GET_TESTFILE(u8"preview-test-no-preview.unzipped.xoj"), dir / "notes.xoj");

    auto cache = std::make_shared<ThumbnailCache>(dir / "cache");
    ThumbnailService service(cache);

    std::vector<ThumbnailResult> delivered;
    ASSERT_NE(service.request(document, [&delivered](const ThumbnailResult& result) { delivered.emplace_back(result); }), 0U);
    ASSERT_TRUE(pump([&delivered] { return !delivered.empty(); }));

    EXPECT_EQ(delivered.front().state, DocumentCard::Preview::None);
    EXPECT_TRUE(delivered.front().png.empty());
    EXPECT_FALSE(delivered.front().error.empty());

    service.waitIdle();
    EXPECT_EQ(cache->entryOf(document), ThumbnailCache::Entry::NoPreview);

    fs::remove_all(dir);
}

TEST(ThumbnailService, aCorruptDocumentIsReportedAsSuch) {
    const fs::path dir = freshDir("xournalpp-test-units_thumbnailCorrupt");
    const fs::path document = copyFixture(GET_TESTFILE(u8"preview-test-invalid.xoj"), dir / "notes.xoj");

    auto cache = std::make_shared<ThumbnailCache>(dir / "cache");
    ThumbnailService service(cache);

    std::vector<ThumbnailResult> delivered;
    ASSERT_NE(service.request(document, [&delivered](const ThumbnailResult& result) { delivered.emplace_back(result); }), 0U);
    ASSERT_TRUE(pump([&delivered] { return !delivered.empty(); }));

    EXPECT_EQ(delivered.front().state, DocumentCard::Preview::Corrupt);
    EXPECT_FALSE(delivered.front().error.empty());

    service.waitIdle();
    EXPECT_EQ(cache->entryOf(document), ThumbnailCache::Entry::Corrupt);

    fs::remove_all(dir);
}

TEST(ThumbnailService, aRequestForAFileThatIsGoneIsAnsweredRatherThanDropped) {
    const fs::path dir = freshDir("xournalpp-test-units_thumbnailMissing");

    auto cache = std::make_shared<ThumbnailCache>(dir / "cache");
    ThumbnailService service(cache);

    std::vector<ThumbnailResult> delivered;
    service.request(dir / "gone.xopp", [&delivered](const ThumbnailResult& result) { delivered.emplace_back(result); });
    ASSERT_TRUE(pump([&delivered] { return !delivered.empty(); }));

    EXPECT_EQ(delivered.front().state, DocumentCard::Preview::Corrupt);
    EXPECT_FALSE(delivered.front().error.empty());

    fs::remove_all(dir);
}

TEST(ThumbnailService, anAnswerAlreadyKnownIsHandedOverWithoutAWorker) {
    const fs::path dir = freshDir("xournalpp-test-units_thumbnailCached");
    const fs::path document = copyFixture(GET_TESTFILE(u8"preview-test-invalid.xoj"), dir / "notes.xopp");

    auto cache = std::make_shared<ThumbnailCache>(dir / "cache");
    // The document is corrupt, so a preview for it can only come from the cache: if the service
    // were to read the file, the answer would be `Corrupt` and never `Available`.
    const std::vector<std::uint8_t> png{'P', 'N', 'G'};
    ASSERT_TRUE(cache->storeImage(document, png));

    ThumbnailService service(cache);
    bool deliveredInsideRequest = false;
    ThumbnailResult result;
    const ThumbnailRequestId id = service.request(document, [&](const ThumbnailResult& answer) {
        deliveredInsideRequest = true;
        result = answer;
    });

    EXPECT_EQ(id, 0U) << "0 means the answer was already known";
    EXPECT_TRUE(deliveredInsideRequest) << "a cached answer does not wait for the main context";
    EXPECT_EQ(result.state, DocumentCard::Preview::Available);
    EXPECT_EQ(result.png, png);

    fs::remove_all(dir);
}

TEST(ThumbnailService, aFileThatChangedIsReadAgain) {
    const fs::path dir = freshDir("xournalpp-test-units_thumbnailInvalidation");
    const fs::path document =
            copyFixture(GET_TESTFILE(u8"preview-test-no-preview.unzipped.xoj"), dir / "notes.xopp");

    auto cache = std::make_shared<ThumbnailCache>(dir / "cache");
    ThumbnailService service(cache);

    std::vector<ThumbnailResult> delivered;
    service.request(document, [&delivered](const ThumbnailResult& result) { delivered.emplace_back(result); });
    ASSERT_TRUE(pump([&delivered] { return !delivered.empty(); }));
    ASSERT_EQ(delivered.front().state, DocumentCard::Preview::None);
    service.waitIdle();

    // The user writes to the document. Nothing is invalidated by hand: the key describes the
    // version, so the next request is a new question.
    copyFixture(GET_TESTFILE(u8"packaged_xopp/testPreview.xopp"), document);
    EXPECT_EQ(cache->entryOf(document), ThumbnailCache::Entry::Missing);

    delivered.clear();
    service.request(document, [&delivered](const ThumbnailResult& result) { delivered.emplace_back(result); });
    ASSERT_TRUE(pump([&delivered] { return !delivered.empty(); }));
    EXPECT_EQ(delivered.front().state, DocumentCard::Preview::Available);
    EXPECT_EQ(std::string(delivered.front().png.begin(), delivered.front().png.end()), "CppUnitTestString \n");

    fs::remove_all(dir);
}

TEST(ThumbnailService, aCancelledRequestIsNeverDelivered) {
    const fs::path dir = freshDir("xournalpp-test-units_thumbnailCancelled");
    const fs::path document = copyFixture(GET_TESTFILE(u8"packaged_xopp/testPreview.xopp"), dir / "notes.xopp");

    auto cache = std::make_shared<ThumbnailCache>(dir / "cache");
    ThumbnailService service(cache);

    std::vector<ThumbnailResult> delivered;
    const ThumbnailRequestId id =
            service.request(document, [&delivered](const ThumbnailResult& result) { delivered.emplace_back(result); });
    ASSERT_NE(id, 0U);
    service.cancel(id);

    // Give the worker and the main context every chance to deliver it anyway.
    pump([&delivered] { return !delivered.empty(); }, 400);
    EXPECT_TRUE(delivered.empty()) << "a card that went away must not be called back";

    // Whether the file was read before the cancellation reached the worker depends on the worker;
    // what is not racy is that the request is finished with and the caller is never called.
    service.waitIdle();
    EXPECT_EQ(service.pendingCount(), 0U);

    fs::remove_all(dir);
}

TEST(ThumbnailService, aServiceThatGoesAwayDropsWhatIsStillOutstanding) {
    const fs::path dir = freshDir("xournalpp-test-units_thumbnailShutdown");
    const fs::path first = copyFixture(GET_TESTFILE(u8"packaged_xopp/testPreview.xopp"), dir / "first.xopp");
    const fs::path second = copyFixture(GET_TESTFILE(u8"packaged_xopp/testPreview2.xopp"), dir / "second.xopp");

    auto cache = std::make_shared<ThumbnailCache>(dir / "cache");
    std::vector<ThumbnailResult> delivered;
    {
        ThumbnailService service(cache);
        service.request(first, [&delivered](const ThumbnailResult& result) { delivered.emplace_back(result); });
        service.request(second, [&delivered](const ThumbnailResult& result) { delivered.emplace_back(result); });
    }

    // The window is gone: whatever the workers were doing, nothing may be handed to it now.
    pump([&delivered] { return !delivered.empty(); }, 600);
    EXPECT_TRUE(delivered.empty());

    fs::remove_all(dir);
}

/*
 * The gap between "the worker has read the document" and "the caller has been given the answer".
 *
 * The worker posts its answer to the caller's main context and is then finished with the request;
 * whether the answer is ever handed over is not the worker's business any more. Cancelling in that
 * gap - a card that left the model, a dashboard the user left, a window being torn down - has to
 * reach the answer on the main context, or it is handed over to a dashboard that may not exist any
 * more. The whole point of the service is that the caller can ask and then change its mind.
 */
TEST(ThumbnailService, anAnswerTheWorkerHasPostedIsStillCancellable) {
    const fs::path dir = freshDir("xournalpp-test-units_thumbnailQueuedCancel");
    const fs::path document = copyFixture(GET_TESTFILE(u8"packaged_xopp/testPreview.xopp"), dir / "notes.xopp");

    auto cache = std::make_shared<ThumbnailCache>(dir / "cache");
    OwnedContext context;
    ASSERT_TRUE(context.owned());

    std::vector<ThumbnailResult> delivered;
    {
        ThumbnailService service(cache, context);
        const ThumbnailRequestId id = service.request(
                document, [&delivered](const ThumbnailResult& result) { delivered.emplace_back(result); });
        ASSERT_NE(id, 0U) << "the request went to a worker";

        // The worker reads the document and posts its answer; nothing has handed it over yet.
        service.waitIdle();
        EXPECT_EQ(service.pendingCount(), 0U) << "the worker is done with the request";
        EXPECT_TRUE(delivered.empty()) << "the answer is waiting on the main context";

        service.cancel(id);
        context.drain();
    }

    EXPECT_TRUE(delivered.empty()) << "an answer cancelled while it was on the main context must not be handed over";

    fs::remove_all(dir);
}

/**
 * The same gap, with the service taken away first - which is what a window's teardown does: the
 * answer is already on the main context when the service is destroyed. Disposing of it afterwards
 * must not touch the service that queued it, and must not hand it over.
 */
TEST(ThumbnailService, anAnswerQueuedWhenTheServiceGoesIsDropped) {
    const fs::path dir = freshDir("xournalpp-test-units_thumbnailQueuedShutdown");
    const fs::path document = copyFixture(GET_TESTFILE(u8"packaged_xopp/testPreview.xopp"), dir / "notes.xopp");

    auto cache = std::make_shared<ThumbnailCache>(dir / "cache");
    OwnedContext context;
    ASSERT_TRUE(context.owned());

    std::vector<ThumbnailResult> delivered;
    {
        ThumbnailService service(cache, context);
        ASSERT_NE(service.request(document,
                                  [&delivered](const ThumbnailResult& result) { delivered.emplace_back(result); }),
                  0U);
        service.waitIdle();
        EXPECT_TRUE(delivered.empty()) << "the answer is waiting on the main context";
    }

    // The service is gone; the answer is not. Draining the context runs the source, and that has to
    // end in the answer being thrown away rather than handed over.
    context.drain();
    EXPECT_TRUE(delivered.empty()) << "an answer that outlived the service must not be handed over";

    fs::remove_all(dir);
}
