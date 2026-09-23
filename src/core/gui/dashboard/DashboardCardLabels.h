/*
 * Xournal++
 *
 * The words a dashboard card shows and announces.
 *
 * Plan 006, step 4. A card carries its state in text as well as in colour: whether the file is
 * there, whether it can be read, whether the editor could open it, whether there is a preview, and
 * whether the user pinned it. The same words are the card's accessible name, so a user who cannot
 * see the card still knows what it is and why it does or does not open.
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <chrono>  // for system_clock
#include <string>  // for string

#include "dashboard/DashboardTypes.h"  // for DocumentCard, RecoveryCard

namespace xoj::dashboardcard {

using TimePoint = std::chrono::system_clock::time_point;

/**
 * A time as a card says it: an interval while that is the useful thing to know ("2 hours ago") and
 * a date once the file is older than a week.
 *
 * @param time the moment to describe
 * @param now the moment to describe it relative to, so a test does not depend on the clock
 */
auto formatWhen(const TimePoint& time, const TimePoint& now) -> std::string;

/// The name a card shows: the file name without its extension, so cards are readable at a glance.
auto buildTitle(const xoj::dashboard::DocumentCard& card) -> std::string;

/**
 * What a screen reader announces for a document card, for instance
 * "notes.xopp, pinned, modified 2 hours ago, no preview".
 *
 * The state comes before the preview: whether a file can be opened at all is what matters.
 */
auto buildAccessibleName(const xoj::dashboard::DocumentCard& card, const TimePoint& now) -> std::string;

/**
 * The line under a card's name: where the file is and when it changed, or why it cannot be used.
 * For instance "\u2026/Notes \u00b7 modified 2 hours ago" or "the file is not there any more".
 */
auto buildMetadata(const xoj::dashboard::DocumentCard& card, const TimePoint& now) -> std::string;

/// What a screen reader announces for a recovery card.
auto buildRecoveryAccessibleName(const xoj::dashboard::RecoveryCard& card, const TimePoint& now) -> std::string;

/// The line under a recovery card's name: where the copy came from and how old it is.
auto buildRecoveryMetadata(const xoj::dashboard::RecoveryCard& card, const TimePoint& now) -> std::string;

/// Whether the card's own state is a failure the user has to act on, and how to say it.
auto buildStateText(const xoj::dashboard::DocumentCard& card) -> std::string;

/// The place a card's file sits in, shortened for a card's metadata line.
auto buildFolderText(const xoj::dashboard::DocumentCard& card) -> std::string;

}  // namespace xoj::dashboardcard
