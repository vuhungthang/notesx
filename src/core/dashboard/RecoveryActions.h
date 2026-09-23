/*
 * Xournal++
 *
 * The file work behind the dashboard's recovery cards: writing a recovered copy somewhere the user
 * chose, and deleting one.
 *
 * Plan 006, step 6. Two operations, and both of them touch the recovery copy and nothing else: the
 * document a copy came from is never written to, moved, renamed or removed by anything here. That
 * is where "recovering never overwrites the original automatically" is a property of code rather
 * than a promise the user interface makes.
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <string>  // for string

#include "dashboard/DashboardTypes.h"  // for RecoveryCard

#include "filesystem.h"  // for path

namespace xoj::dashboard {

/**
 * What the dashboard's recovery cards do to files.
 *
 * Deliberately free of GTK and of the application: what can go wrong when a copy is written or
 * deleted is checked with real files in a temporary folder, rather than through a dialog.
 */
class RecoveryActions {
public:
    /**
     * Write a recovered copy to `target`.
     *
     * @param card          the copy and the document it came from
     * @param target        where the copy is written. It is created or replaced; the file it was
     *                      recovered from is only ever this path when the caller says so.
     * @param allowOriginal whether writing over the document the copy came from is what the user
     *                      asked for. Without it, that path is refused: a recovery copy is not a way
     *                      for the dashboard to overwrite somebody's document on its own.
     * @param error         what went wrong, empty on success
     * @return whether the copy was written
     */
    static auto copyTo(const RecoveryCard& card, const fs::path& target, bool allowOriginal, std::string& error)
            -> bool;

    /**
     * Delete a recovered copy.
     *
     * The document it came from is left exactly as it is, whether or not it still exists.
     *
     * @param error why the copy is still there, empty on success
     * @return whether the copy is gone
     */
    static auto removeCopy(const RecoveryCard& card, std::string& error) -> bool;

    /// Whether `target` is the document the copy was recovered from.
    static auto isOriginal(const RecoveryCard& card, const fs::path& target) -> bool;
};

}  // namespace xoj::dashboard
