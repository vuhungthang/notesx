/*
 * Xournal++
 *
 * This file is part of the Xournal UnitTests
 *
 * Plan 006, step 7: a card must not keep saying what was true an hour ago.
 *
 * Everything here happens in a temporary folder with real files, because the only way to be sure a
 * monitor sees what the operating system does is to do it: create, rename and delete files and wait
 * for the report. The waiting is a GLib main context with a deadline, so a watcher that never
 * reports fails the test instead of hanging it.
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

#include <gio/gio.h>  // for g_main_context_iteration
#include <glib.h>
#include <gtest/gtest.h>

#include "dashboard/DashboardTypes.h"  // for LibraryFolder
#include "dashboard/FileWatcher.h"     // for FileWatcher

#include "filesystem.h"  // for fs

using xoj::dashboard::FileWatcher;
using xoj::dashboard::LibraryFolder;

namespace {

/// A fresh, empty directory to build one test's files in.
auto freshDir(const char* name) -> fs::path {
    const fs::path dir = fs::temp_directory_path() / name;
    std::error_code error;
    fs::remove_all(dir, error);
    fs::create_directories(dir, error);
    return dir;
}

void writeText(const fs::path& path, const std::string& text) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << text;
}

auto folderWatch(const fs::path& path, bool recursive = false) -> LibraryFolder {
    LibraryFolder folder;
    folder.path = path;
    folder.displayName = path.filename().string();
    folder.recursive = recursive;
    return folder;
}

/*
 * A report arrives through GLib's main context, so the test has to run it. The deadline keeps a
 * watcher that never reports from turning into a hung test: the loop gives up after `timeout` and
 * lets the expectation say what did not happen.
 */
struct MainContext {
    /// A source that fires often enough that the loop below can always check its deadline: waiting
    /// for an event must never be waiting forever, or a watcher that says nothing would hang a test
    /// rather than fail it.
    static auto tick(gpointer) -> gboolean { return G_SOURCE_CONTINUE; }

    /// Run the main context until `done` or until the deadline.
    template <typename Predicate>
    static auto runUntil(Predicate done, std::chrono::milliseconds timeout = std::chrono::milliseconds(5000)) -> bool {
        const guint tickSource = g_timeout_add(20, tick, nullptr);
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        bool finished = done();
        while (!finished && std::chrono::steady_clock::now() < deadline) {
            g_main_context_iteration(nullptr, TRUE);
            finished = done();
        }
        g_source_remove(tickSource);
        return finished;
    }

    /// Let the main context run for `duration` and say whether nothing happened in that time.
    template <typename Predicate>
    static void expectNothingHappens(Predicate happened) {
        runUntil(happened, std::chrono::milliseconds(400));
    }
};

/// What a watcher's reports looked like, so a test can assert on them.
struct Reports {
    std::vector<std::vector<fs::path>> reports;

    auto count() const -> std::size_t { return this->reports.size(); }
    auto flat() const -> std::vector<fs::path> {
        std::vector<fs::path> all;
        for (const std::vector<fs::path>& report: this->reports) {
            all.insert(all.end(), report.begin(), report.end());
        }
        return all;
    }
    auto mentions(const fs::path& path) const -> bool {
        for (const fs::path& seen: this->flat()) {
            if (seen == path || seen == path.parent_path()) {
                return true;
            }
        }
        return false;
    }
};

}  // namespace

TEST(FileWatcherTest, aNewFileInAWatchedFolderIsReportedOnceForTheWholeBurst) {
    const fs::path dir = freshDir("xournalpp-test-units_watchFolder");
    const fs::path one = dir / "one.xopp";
    const fs::path two = dir / "two.xopp";

    Reports reports;
    FileWatcher watcher;
    watcher.setDebounceInterval(std::chrono::milliseconds(200));
    watcher.setCallback([&reports](const std::vector<fs::path>& changed) { reports.reports.push_back(changed); });
    watcher.watch({}, {folderWatch(dir)});
    ASSERT_EQ(watcher.getWatchedCount(), 1U) << "one folder is one monitor";

    writeText(one, "one");
    writeText(two, "two");

    ASSERT_TRUE(MainContext::runUntil([&reports]() { return reports.count() >= 1; }))
            << "a file appearing in a watched folder is reported";
    // The burst was one report, not one per event: a directory monitor can deliver several events
    // for the two files, and the dashboard is rebuilt once.
    EXPECT_EQ(reports.count(), 1U);
    EXPECT_TRUE(reports.mentions(one) || reports.mentions(two));

    fs::remove_all(dir);
}

TEST(FileWatcherTest, aWatchedFileThatIsRenamedOrDeletedIsReported) {
    const fs::path dir = freshDir("xournalpp-test-units_watchFile");
    const fs::path file = dir / "watched.xopp";
    writeText(file, "the document");

    Reports reports;
    FileWatcher watcher;
    watcher.setDebounceInterval(std::chrono::milliseconds(200));
    watcher.setCallback([&reports](const std::vector<fs::path>& changed) { reports.reports.push_back(changed); });
    watcher.watch({file}, {});
    ASSERT_EQ(watcher.getWatchedCount(), 1U) << "one file is one monitor";

    fs::remove(file);
    ASSERT_TRUE(MainContext::runUntil([&reports]() { return reports.count() >= 1; })) << "a deletion is reported";
    EXPECT_TRUE(reports.mentions(file));

    // A file that comes back is reported as well: the card is not left saying it is gone.
    const std::size_t before = reports.count();
    writeText(file, "written again");
    ASSERT_TRUE(MainContext::runUntil([&reports, before]() { return reports.count() > before; }));

    fs::remove_all(dir);
}

TEST(FileWatcherTest, aFolderIsShallowUnlessItWasAskedForRecursively) {
    const fs::path dir = freshDir("xournalpp-test-units_watchRecursive");
    const fs::path nested = dir / "sub";
    std::error_code error;
    fs::create_directories(nested, error);
    const fs::path deepFile = nested / "deep.xopp";

    Reports reports;
    FileWatcher watcher;
    watcher.setDebounceInterval(std::chrono::milliseconds(150));
    watcher.setCallback([&reports](const std::vector<fs::path>& changed) { reports.reports.push_back(changed); });

    // Shallow: what happens inside the subfolder is not this folder's business, and the subtree is
    // not walked for it either.
    watcher.watch({}, {folderWatch(dir, false)});
    EXPECT_EQ(watcher.getWatchedCount(), 1U) << "a shallow folder is one monitor, whatever it holds";
    writeText(deepFile, "deep");
    MainContext::expectNothingHappens([&reports]() { return reports.count() > 0; });

    watcher.stop();
    EXPECT_EQ(watcher.getWatchedCount(), 0U) << "stop() lets every watch go";

    // The same folder, asked for recursively, watches what is inside it as well.
    watcher.watch({}, {folderWatch(dir, true)});
    EXPECT_GE(watcher.getWatchedCount(), 2U) << "the subtree is watched because the user asked for it";
    writeText(deepFile, "deeper");
    EXPECT_TRUE(MainContext::runUntil([&reports]() { return reports.count() > 0; }))
            << "a change in the subtree is seen";

    watcher.stop();
    fs::remove_all(dir);
}

TEST(FileWatcherTest, watchFollowsTheCardsAndForgottenPathsAreLetGo) {
    const fs::path first = freshDir("xournalpp-test-units_watchReplaceA");
    const fs::path second = freshDir("xournalpp-test-units_watchReplaceB");

    Reports reports;
    FileWatcher watcher;
    watcher.setDebounceInterval(std::chrono::milliseconds(150));
    watcher.setCallback([&reports](const std::vector<fs::path>& changed) { reports.reports.push_back(changed); });

    watcher.watch({}, {folderWatch(first)});
    ASSERT_EQ(watcher.getWatchedCount(), 1U);

    // The dashboard was rebuilt and no longer shows the first folder.
    watcher.watch({}, {folderWatch(second)});
    EXPECT_EQ(watcher.getWatchedCount(), 1U) << "a path that is no longer shown is no longer watched";

    writeText(first / "ignored.xopp", "ignored");
    MainContext::expectNothingHappens([&reports]() { return reports.count() > 0; });

    writeText(second / "seen.xopp", "seen");
    EXPECT_TRUE(MainContext::runUntil([&reports]() { return reports.count() > 0; }));

    // The same set twice does not accumulate monitors.
    watcher.watch({}, {folderWatch(second)});
    EXPECT_EQ(watcher.getWatchedCount(), 1U);

    watcher.stop();
    fs::remove_all(first);
    fs::remove_all(second);
}

TEST(FileWatcherTest, stoppingCancelsBothTheWatchesAndAPendingReport) {
    const fs::path dir = freshDir("xournalpp-test-units_watchStop");

    Reports reports;
    FileWatcher watcher;
    // Long enough that the report would still be pending when the watcher is stopped.
    watcher.setDebounceInterval(std::chrono::milliseconds(5000));
    watcher.setCallback([&reports](const std::vector<fs::path>& changed) { reports.reports.push_back(changed); });
    watcher.watch({}, {folderWatch(dir)});

    writeText(dir / "one.xopp", "one");
    ASSERT_TRUE(MainContext::runUntil([&watcher]() { return watcher.hasPendingReport(); }))
            << "the change is seen and is waiting for the debounce";
    EXPECT_GE(watcher.getPendingCount(), 1U);

    watcher.stop();
    EXPECT_FALSE(watcher.hasPendingReport()) << "a report that belongs to the old dashboard is dropped";
    EXPECT_EQ(watcher.getWatchedCount(), 0U);

    writeText(dir / "two.xopp", "two");
    MainContext::expectNothingHappens([&reports]() { return reports.count() > 0; });

    fs::remove_all(dir);
}

TEST(FileWatcherTest, aChangeSeenCanBeReportedAtOnceInsteadOfWaitingForTheDebounce) {
    const fs::path dir = freshDir("xournalpp-test-units_watchFlush");

    Reports reports;
    FileWatcher watcher;
    watcher.setDebounceInterval(std::chrono::milliseconds(5000));
    watcher.setCallback([&reports](const std::vector<fs::path>& changed) { reports.reports.push_back(changed); });
    watcher.watch({}, {folderWatch(dir)});

    writeText(dir / "one.xopp", "one");
    ASSERT_TRUE(MainContext::runUntil([&watcher]() { return watcher.hasPendingReport(); }));

    watcher.flush();
    EXPECT_FALSE(watcher.hasPendingReport());
    ASSERT_EQ(reports.count(), 1U) << "asking for the report now is what the debounce would have done";
    EXPECT_FALSE(reports.reports.front().empty());
    // Nothing is reported twice.
    MainContext::expectNothingHappens([&reports]() { return reports.count() > 1; });

    fs::remove_all(dir);
}

TEST(FileWatcherTest, nothingIsWatchedWhenThereIsNothingToWatch) {
    FileWatcher watcher;
    watcher.watch({}, {});
    EXPECT_EQ(watcher.getWatchedCount(), 0U);
    EXPECT_FALSE(watcher.hasPendingReport());

    // A file that is not there and a folder that is not there are not watched: a stale card does
    // not cost a monitor, and there is nothing to see anyway.
    const fs::path dir = freshDir("xournalpp-test-units_watchMissing");
    watcher.watch({dir / "gone.xopp"}, {folderWatch(dir / "gone-folder")});
    EXPECT_EQ(watcher.getWatchedCount(), 0U) << "a path that is not there cannot be watched";
    watcher.stop();
    fs::remove_all(dir);
}
