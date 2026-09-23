#include "FileWatcher.h"

#include <algorithm>     // for find
#include <memory>        // for unique_ptr
#include <system_error>  // for error_code
#include <utility>       // for move

#include "dashboard/DashboardModel.h"  // for MAX_RECURSION_DEPTH
#include "dashboard/DashboardTypes.h"  // for canonicalPath
#include "util/PathUtil.h"             // for GFilename

using namespace xoj::dashboard;

namespace {

/// How a monitor is made: one call per kind of thing, and the rest of the class does not care which.
auto startMonitor(const fs::path& path, bool isFolder) -> GFileMonitor* {
    const Util::GFilename filename(path);
    GFile* file = g_file_new_for_path(filename.c_str());
    if (file == nullptr) {
        return nullptr;
    }

    /*
     * No flags: a move is a change like any other, and both halves of it - the path that was left
     * and the path that was taken - are worth a rebuild. A monitor that fails to start (a folder
     * without the permission to watch it, a kind of file that cannot be watched) is simply not held:
     * a watch is an optimisation, and the dashboard is still correct without it.
     */
    GError* error = nullptr;
    GFileMonitor* monitor = isFolder ? g_file_monitor_directory(file, G_FILE_MONITOR_NONE, nullptr, &error) :
                                       g_file_monitor_file(file, G_FILE_MONITOR_NONE, nullptr, &error);
    g_object_unref(file);

    if (error != nullptr) {
        g_error_free(error);
    }
    return monitor;
}

/// Whether the path can be watched at all: a file that is a file, a folder that is a folder.
auto canBeWatched(const fs::path& path, bool isFolder) -> bool {
    if (path.empty()) {
        return false;
    }
    std::error_code error;
    const bool rightKind = isFolder ? fs::is_directory(path, error) : fs::is_regular_file(path, error);
    return rightKind && !error;
}

}  // namespace

FileWatcher::FileWatcher() = default;

FileWatcher::~FileWatcher() { stop(); }

void FileWatcher::setCallback(Callback cb) { this->callback = std::move(cb); }

void FileWatcher::setDebounceInterval(std::chrono::milliseconds interval) {
    // A debounce of nothing would be a report per event, which is what the plan forbids: the shortest
    // window this class offers is long enough to be one tick of the main loop.
    this->debounce = interval.count() > 0 ? interval : std::chrono::milliseconds(1);
}

auto FileWatcher::getDebounceInterval() const -> std::chrono::milliseconds { return this->debounce; }

void FileWatcher::watch(const std::vector<fs::path>& files, const std::vector<LibraryFolder>& folders) {
    /*
     * The set that is passed in is the set that is watched: everything that is no longer in it is let
     * go, so the watches follow the dashboard's cards instead of growing with every refresh.
     */
    std::map<std::string, xoj::util::GObjectSPtr<GFileMonitor>> fresh;

    std::size_t fileBudget = MAX_FOLDER_WATCHES;
    for (const fs::path& file: files) {
        addWatch(fresh, file, false, fileBudget);
    }

    for (const LibraryFolder& folder: folders) {
        if (!folder.enabled) {
            continue;
        }
        std::size_t budget = MAX_FOLDER_WATCHES;
        addWatch(fresh, folder.path, true, budget);
        if (folder.recursive) {
            // Deep watching is only ever what the user asked for, and even then it is bounded: a
            // folder with a subtree the size of a home directory must not cost a watch per file.
            addFolderSubtreeWatches(fresh, folder.path, budget);
        }
    }

    this->monitors = std::move(fresh);
}

void FileWatcher::addWatch(std::map<std::string, xoj::util::GObjectSPtr<GFileMonitor>>& fresh, const fs::path& path,
                           bool isFolder, std::size_t& budget) {
    if (budget == 0 || !canBeWatched(path, isFolder)) {
        return;
    }

    const std::string key = canonicalPath(path).string();
    if (fresh.count(key) != 0) {
        return;
    }

    GFileMonitor* monitor = startMonitor(path, isFolder);
    if (monitor == nullptr) {
        return;
    }

    g_signal_connect(monitor, "changed", G_CALLBACK(onMonitorEvent), this);
    fresh.emplace(key, xoj::util::GObjectSPtr<GFileMonitor>(monitor, xoj::util::adopt));
    budget--;
}

void FileWatcher::addFolderSubtreeWatches(std::map<std::string, xoj::util::GObjectSPtr<GFileMonitor>>& fresh,
                                          const fs::path& folder, std::size_t& budget) {
    std::error_code error;
    fs::recursive_directory_iterator entry(folder, fs::directory_options::skip_permission_denied, error);
    const fs::recursive_directory_iterator end;
    while (entry != end && budget > 0) {
        // The subtree is walked to the same depth the indexing walks it, and no deeper.
        if (entry.depth() + 1 >= DashboardModel::MAX_RECURSION_DEPTH) {
            entry.disable_recursion_pending();
        }

        if (entry->is_directory(error) && !error) {
            addWatch(fresh, entry->path(), true, budget);
        }

        error.clear();
        entry.increment(error);
        if (error) {
            break;
        }
    }
}

void FileWatcher::flush() {
    disarmDebounce();
    report();
}

void FileWatcher::stop() {
    disarmDebounce();
    this->pending.clear();
    this->monitors.clear();
}

auto FileWatcher::getWatchedCount() const -> std::size_t { return this->monitors.size(); }

auto FileWatcher::hasPendingReport() const -> bool { return this->pendingSource != 0; }

auto FileWatcher::getPendingCount() const -> std::size_t { return this->pending.size(); }

void FileWatcher::onMonitorEvent(GFileMonitor*, GFile* file, GFile*, GFileMonitorEvent, gpointer data) {
    auto* self = static_cast<FileWatcher*>(data);
    if (self == nullptr || file == nullptr) {
        return;
    }

    std::unique_ptr<char, decltype(&g_free)> path(g_file_get_path(file), &g_free);
    if (path == nullptr) {
        return;
    }
    self->rememberChange(fs::path(path.get()));
}

auto FileWatcher::onDebounceElapsed(gpointer data) -> gboolean {
    auto* self = static_cast<FileWatcher*>(data);
    self->pendingSource = 0;
    self->report();
    return G_SOURCE_REMOVE;
}

void FileWatcher::rememberChange(const fs::path& path) {
    if (path.empty()) {
        return;
    }
    // Several events for the same path are one change: a document that is written produces a
    // handful of them, and the dashboard only needs to know that the file is not what it was.
    if (std::find(this->pending.begin(), this->pending.end(), path) == this->pending.end()) {
        this->pending.push_back(path);
    }
    armDebounce();
}

void FileWatcher::armDebounce() {
    if (this->pendingSource != 0) {
        // The window is already open: a burst is one report, and the report happens once the burst
        // has been quiet for the whole interval.
        return;
    }
    this->pendingSource =
            g_timeout_add(static_cast<guint>(this->debounce.count()), FileWatcher::onDebounceElapsed, this);
}

void FileWatcher::report() {
    if (this->pending.empty()) {
        return;
    }
    std::vector<fs::path> changed;
    changed.swap(this->pending);
    if (this->callback) {
        this->callback(changed);
    }
}

void FileWatcher::disarmDebounce() {
    if (this->pendingSource != 0) {
        g_source_remove(this->pendingSource);
        this->pendingSource = 0;
    }
}
