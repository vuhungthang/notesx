/*
 * Xournal++
 *
 * Watches the files and the folders the dashboard shows, and says when something about them changed.
 *
 * Plan 006, step 7. The dashboard is an index over files the user owns, and those files change
 * outside the application: a file that is renamed, deleted or written by another program must not
 * leave a card saying what was true an hour ago.
 *
 * GLib's monitors do the watching. What this class adds is the shape a user interface needs: one
 * report per burst of changes instead of one per event, watches that follow the cards (a file the
 * dashboard stopped showing is not watched any more), a subtree that is only walked when the user
 * asked for that folder recursively, and a teardown that leaves no watch and no pending report
 * behind. It holds no model, no GTK and no document.
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <chrono>      // for milliseconds
#include <cstddef>     // for size_t
#include <functional>  // for function
#include <map>         // for map
#include <string>      // for string
#include <vector>      // for vector

#include <gio/gio.h>  // for GFile, GFileMonitor
#include <glib.h>     // for guint, gboolean, gpointer

#include "dashboard/DashboardTypes.h"  // for LibraryFolder
#include "util/raii/GObjectSPtr.h"     // for GObjectSPtr

#include "filesystem.h"  // for path

namespace xoj::dashboard {

/**
 * The files and folders the dashboard watches, and when to say that something changed.
 */
class FileWatcher {
public:
    /**
     * What happened, coalesced.
     *
     * @param changedPaths the paths an event was seen for. A path may be a file that was written, a
     *                     file that appeared or disappeared, or a folder whose contents changed.
     */
    using Callback = std::function<void(const std::vector<fs::path>& changedPaths)>;

    /**
     * How long a burst of events is allowed to last before the dashboard is rebuilt.
     *
     * An editor saving a document produces several events in a row; rebuilding for each of them
     * would be the full rescan the plan forbids. A third of a second is long enough to swallow a
     * save and short enough that a user who deletes a file sees the card change.
     */
    static constexpr std::chrono::milliseconds DEFAULT_DEBOUNCE{350};

    FileWatcher();
    ~FileWatcher();

    FileWatcher(const FileWatcher&) = delete;
    auto operator=(const FileWatcher&) -> FileWatcher& = delete;

    void setCallback(Callback callback);
    void setDebounceInterval(std::chrono::milliseconds interval);
    auto getDebounceInterval() const -> std::chrono::milliseconds;

    /**
     * Watch exactly these files and these folders.
     *
     * Replacing the set lets go of everything that is no longer in it: the watches follow the
     * dashboard's cards rather than growing with every refresh. A folder is watched as a folder
     * unless its `recursive` flag asks for its subtree as well, and even then the walk is bounded
     * by the same limits the indexing uses.
     */
    void watch(const std::vector<fs::path>& files, const std::vector<LibraryFolder>& folders);

    /// Report a pending change now rather than when the debounce elapses. Does nothing when there
    /// is nothing pending.
    void flush();

    /// Let every watch go and drop a pending report. Called when the window goes away.
    void stop();

    /// How many monitors are held: one per watched file and per watched folder.
    auto getWatchedCount() const -> std::size_t;
    /// Whether a change has been seen whose report is still waiting for the debounce.
    auto hasPendingReport() const -> bool;
    /// How many changes are waiting to be reported together.
    auto getPendingCount() const -> std::size_t;

    /// How many monitors one folder's subtree may add, so a huge tree cannot be watched file by
    /// file. The same order of magnitude as what the model is willing to index.
    static constexpr std::size_t MAX_FOLDER_WATCHES = 256;

private:
    /// GLib's own handler: a monitor said something about a file.
    static void onMonitorEvent(GFileMonitor* monitor, GFile* file, GFile* other, GFileMonitorEvent event,
                               gpointer data);
    /// The debounce elapsed: report what was collected.
    static auto onDebounceElapsed(gpointer data) -> gboolean;

    /// Watch one path as the kind of thing it is, unless it is watched already.
    void addWatch(std::map<std::string, xoj::util::GObjectSPtr<GFileMonitor>>& fresh, const fs::path& path,
                  bool isFolder, std::size_t& budget);
    /// Watch a folder's subtree, bounded by `budget` monitors.
    void addFolderSubtreeWatches(std::map<std::string, xoj::util::GObjectSPtr<GFileMonitor>>& fresh,
                                 const fs::path& folder, std::size_t& budget);

    void rememberChange(const fs::path& path);
    void armDebounce();
    void report();
    /// Cancel the pending report, without reporting it.
    void disarmDebounce();

    Callback callback;
    std::chrono::milliseconds debounce = DEFAULT_DEBOUNCE;
    /// The monitors that are held, by the canonical path they watch.
    std::map<std::string, xoj::util::GObjectSPtr<GFileMonitor>> monitors;
    /// The changes seen since the last report, in the order they arrived, without duplicates.
    std::vector<fs::path> pending;
    /// The debounce source, 0 when no report is pending.
    guint pendingSource = 0;
};

}  // namespace xoj::dashboard
