/*
 * Xournal++
 *
 * The editor's document-safety row: one compact line under the top toolbar that answers
 * "is my work safe?" without interrupting anything.
 *
 * Plan 004, step 3. The row is a view of `xoj::safety::SafetySnapshot` and nothing else: it
 * never decides whether a document is saved, it renders what the safety model says. Every state
 * is carried by an icon, a text label and a semantic colour class, so no state is left to colour
 * alone, and the row keeps one height in every state so a status change never moves the canvas.
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <functional>  // for function
#include <string>      // for string

#include <glib.h>  // for guint
#include <gtk/gtk.h>

#include "control/DocumentSafetyState.h"  // for SafetySnapshot, SafetyState
#include "util/raii/GObjectSPtr.h"        // for WidgetSPtr

class SafetyStatusBar {
public:
    /// What the row asks its owner to do. The row itself has no Control and no dialog.
    struct Callbacks {
        /// Show `details` to the user; `retryable` asks for a way to run the operation again.
        std::function<void(const std::string& details, bool retryable)> showDetails;
        /// Run the failed operation again.
        std::function<void()> retry;
        /// Re-read the safety state. Used when a transient confirmation runs out.
        std::function<void()> refresh;
    };

    explicit SafetyStatusBar(Callbacks callbacks);
    ~SafetyStatusBar();

    SafetyStatusBar(const SafetyStatusBar&) = delete;
    auto operator=(const SafetyStatusBar&) -> SafetyStatusBar& = delete;

    /// The row. Owned by this object; add it to a container, do not destroy it.
    GtkWidget* getWidget() const;

    /// Render `snapshot`.
    void update(const xoj::safety::SafetySnapshot& snapshot);

    /// The text the row shows. An operation's own state never hides that the document is modified.
    static auto getStateText(const xoj::safety::SafetySnapshot& snapshot) -> std::string;

    /// The icon the row shows. Stock icon names, so the row keeps an icon whatever the theme.
    static auto getStateIconName(xoj::safety::SafetyState state) -> const char*;

    /// The Plan 001 semantic class that carries the state's colour and border.
    static auto getStateCssClass(xoj::safety::SafetyState state) -> const char*;

    /// Whether the failed operation can be run again from the row.
    static auto isRetryable(const xoj::safety::SafetySnapshot& snapshot) -> bool;

    /// The timestamps, paths and last error behind the row, one per line.
    static auto getDetailsText(const xoj::safety::SafetySnapshot& snapshot) -> std::string;

private:
    static auto onDetailsClicked(GtkButton* button, gpointer data) -> void;
    static auto onRetryClicked(GtkButton* button, gpointer data) -> void;
    static auto onConfirmationTimeout(gpointer data) -> gboolean;

    void scheduleConfirmationTimeout(const xoj::safety::SafetySnapshot& snapshot);

    Callbacks callbacks;

    /// The last snapshot rendered, so the Details control describes what the user is looking at.
    xoj::safety::SafetySnapshot lastSnapshot;

    xoj::util::WidgetSPtr row;
    GtkImage* icon = nullptr;
    GtkLabel* label = nullptr;
    GtkRevealer* bannerRevealer = nullptr;
    GtkLabel* bannerLabel = nullptr;
    GtkButton* retryButton = nullptr;
    GtkButton* detailsButton = nullptr;

    /// The source that ends a transient confirmation, 0 when none is pending.
    guint confirmationTimeout = 0;
};
